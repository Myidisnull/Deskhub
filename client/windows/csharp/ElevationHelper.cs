using System;
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
    public static ShareRequest? ParseArgs(string[] args)
    {
        for (int i = 0; i + 6 < args.Length; i++)
        {
            if (args[i] != "--share") continue;
            try
            {
                ulong hwnd = ulong.Parse(args[i + 1]);
                ushort port = ushort.Parse(args[i + 2]);
                uint fps = uint.Parse(args[i + 3]);
                uint bitrate = uint.Parse(args[i + 4]);
                bool control = args[i + 5] == "1";
                string name = Encoding.UTF8.GetString(Convert.FromBase64String(args[i + 6]));
                return new ShareRequest(hwnd, name, port, fps, bitrate, control);
            }
            catch
            {
                return null;
            }
        }
        return null;
    }

    // Tên đưa qua base64(UTF-8) để không vỡ dòng lệnh vì dấu cách / ký tự Unicode.
    private static string BuildArgs(ShareRequest r)
    {
        string nameB64 = Convert.ToBase64String(Encoding.UTF8.GetBytes(r.Name));
        return $"--share {r.Hwnd} {r.Port} {r.Fps} {r.BitrateMbps} {(r.AllowControl ? 1 : 0)} {nameB64}";
    }
}
