using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using Deskhub.Interop;

namespace Deskhub;

// =============================================================================
// ElevationHelper — nâng quyền (UAC) cho vai HOST, bản C# của cpp/ElevatedShare.
//
// VÌ SAO CẦN: bơm input tới ứng dụng chạy admin (UIPI nuốt SendInput im lặng) và
// thêm rule firewall đều đòi quyền admin. Chỉ xin ĐÚNG LÚC cần: bật điều khiển,
// hoặc rule firewall chưa có. Chia sẻ chỉ-xem thì không bung UAC.
//
// LUỒNG: instance thường bấm Start → thấy cần quyền → chạy lại chính Deskhub.exe với
// verb "runas" (UAC) mang theo ShareRequest trong dòng lệnh → instance mới vào thẳng
// SharingStatusPage (App.OnLaunched đọc lại args). Xem cpp/ElevatedShare.h cho gốc.
// =============================================================================
public static class ElevationHelper
{
    public static bool IsElevated()
    {
        try { return NativeMethods.IsElevated(); }
        catch { return false; }
    }

    // Cần nâng quyền nếu: CHƯA elevated VÀ (bật điều khiển HOẶC chưa có rule firewall).
    public static bool NeedsElevation(bool allowControl)
    {
        if (IsElevated()) return false;
        try { return allowControl || !NativeMethods.FirewallRulePresent(); }
        catch { return allowControl; }
    }

    // Bung UAC + chạy lại Deskhub.exe elevated mang theo `req`. True = đã khởi chạy
    // (instance gọi nên thoát); False = người dùng huỷ UAC / lỗi (chạy tiếp quyền thường).
    public static bool TryRelaunchElevated(ShareRequest req)
    {
        var exe = Environment.ProcessPath;
        if (string.IsNullOrEmpty(exe)) return false;
        var psi = new ProcessStartInfo
        {
            FileName = exe,
            UseShellExecute = true, // bắt buộc cho verb "runas"
            Verb = "runas",
            Arguments = BuildArgs(req),
        };
        try
        {
            Process.Start(psi);
            return true;
        }
        catch
        {
            return false; // Win32Exception 1223 = người dùng bấm No ở UAC
        }
    }

    // Đọc ShareRequest do instance thường bàn giao qua dòng lệnh. null nếu không phải
    // dạng bàn giao (chạy bình thường → mở Home).
    //
    // Định dạng: --share <port> <fps> <bitrate> <control> <clipboard> <base64(json-ish)>
    // Mảnh cuối là danh sách nguồn: "hwnd:monitor:base64(tên)" nối bằng '|', rồi cả
    // chuỗi đó lại base64 một lần nữa. Nghe rườm rà, nhưng tên cửa sổ chứa được dấu
    // cách, dấu ngoặc kép và ký tự Unicode — bất kỳ cách nào ít lớp hơn đều vỡ ở đúng
    // những tiêu đề mà người ta hay chia sẻ ("Tài liệu — Word").
    public static ShareRequest? ParseArgs(string[] args)
    {
        for (int i = 0; i + 6 < args.Length; i++)
        {
            if (args[i] != "--share") continue;
            try
            {
                ushort port = ushort.Parse(args[i + 1]);
                uint fps = uint.Parse(args[i + 2]);
                uint bitrate = uint.Parse(args[i + 3]);
                bool control = args[i + 4] == "1";
                bool clipboard = args[i + 5] == "1";

                var sources = new List<ShareSource>();
                string packed = Encoding.UTF8.GetString(Convert.FromBase64String(args[i + 6]));
                foreach (var part in packed.Split('|', StringSplitOptions.RemoveEmptyEntries))
                {
                    var f = part.Split(':');
                    if (f.Length != 3) continue;
                    sources.Add(new ShareSource(
                        ulong.Parse(f[0]), ulong.Parse(f[1]),
                        Encoding.UTF8.GetString(Convert.FromBase64String(f[2]))));
                }
                if (sources.Count == 0) return null;
                return new ShareRequest(sources, port, fps, bitrate, control, clipboard);
            }
            catch
            {
                return null;
            }
        }
        return null;
    }

    private static string BuildArgs(ShareRequest r)
    {
        var parts = new List<string>(r.Sources.Count);
        foreach (var s in r.Sources)
        {
            string nameB64 = Convert.ToBase64String(Encoding.UTF8.GetBytes(s.Name));
            parts.Add($"{s.Hwnd}:{s.Monitor}:{nameB64}");
        }
        string packed = Convert.ToBase64String(Encoding.UTF8.GetBytes(string.Join('|', parts)));
        return $"--share {r.Port} {r.Fps} {r.BitrateMbps} " +
               $"{(r.AllowControl ? 1 : 0)} {(r.ShareClipboard ? 1 : 0)} {packed}";
    }
}
