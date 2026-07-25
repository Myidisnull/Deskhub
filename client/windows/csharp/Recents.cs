using System;
using System.Collections.Generic;
using System.Linq;
using Windows.Storage;

namespace Deskhub;

// Một máy đã kết nối trước đây, hiện ở mục "Kết nối gần đây" của màn chính.
// `Link` là loại đường (Wi-Fi / Ethernet / Tailscale) nếu biết — bản thiết kế hiện
// nó cạnh địa chỉ; ta chỉ đoán được từ dải IP nên để rỗng khi không chắc.
public sealed record RecentMachine(string Address, string Name, string Link, DateTimeOffset LastUsed);

// =============================================================================
// Recents — danh sách máy đã dùng, lưu giữa các lần chạy.
//
// VÌ SAO CÓ Ở ĐÂY MÀ KHÔNG PHẢI Ở CORE
//   Nó là trí nhớ của MỘT CÀI ĐẶT app, không phải một luật của giao thức: hai máy nói
//   chuyện với nhau không cần biết bên kia nhớ gì. Core chỉ nhận những thứ cả sáu
//   client phải nhất trí.
//
// LƯU BẰNG MỘT CHUỖI, KHÔNG PHẢI JSON
//   Danh sách tối đa 12 dòng, mỗi dòng bốn trường không có ký tự phân tách bên trong
//   (địa chỉ và loại link là ASCII; tên máy đã lọc). Kéo cả bộ tuần tự hoá JSON vào
//   cho ngần ấy dữ liệu là đổi một phụ thuộc lấy không gì.
//
// LIÊN QUAN: AppState.cs (cùng kho LocalSettings, cùng cách chịu lỗi khi unpackaged)
// =============================================================================
public static class Recents
{
    private const string Key = "recentMachines";
    private const int Max = 12;
    private const char RowSep = '\n';
    private const char FieldSep = '\t';

    private static List<RecentMachine>? _cache;

    public static IReadOnlyList<RecentMachine> All => _cache ??= Load();

    // Ghi nhận một lần kết nối. Địa chỉ trùng thì cập nhật tại chỗ và đẩy lên đầu —
    // danh sách sắp theo lần dùng gần nhất, đúng thứ tự người ta đi tìm.
    public static void Remember(string address, string name, string link = "")
    {
        if (string.IsNullOrWhiteSpace(address)) return;
        var list = new List<RecentMachine>(All.Where(m => m.Address != address));
        list.Insert(0, new RecentMachine(address, Clean(name), Clean(link), DateTimeOffset.Now));
        if (list.Count > Max) list.RemoveRange(Max, list.Count - Max);
        _cache = list;
        Save(list);
    }

    public static void ForgetAll()
    {
        _cache = new List<RecentMachine>();
        Save(_cache);
    }

    // Đoán loại đường từ dải IP. Chỉ để hiện nhãn, nên đoán sai không hỏng gì —
    // 100.64.0.0/10 là dải CGNAT mà Tailscale dùng, phần còn lại thì không biết.
    public static string GuessLink(string address)
    {
        var host = address.Split(':')[0];
        var octets = host.Split('.');
        if (octets.Length == 4 && int.TryParse(octets[0], out var a)
            && int.TryParse(octets[1], out var b))
        {
            if (a == 100 && b >= 64 && b <= 127) return "Tailscale";
            if (a == 10 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31))
                return "LAN";
        }
        return "";
    }

    // Bỏ ký tự phân tách khỏi dữ liệu người dùng: một tên cửa sổ có tab trong đó sẽ
    // làm lệch mọi trường phía sau khi đọc lại.
    private static string Clean(string s)
        => string.IsNullOrEmpty(s) ? "" : s.Replace(FieldSep, ' ').Replace(RowSep, ' ');

    private static List<RecentMachine> Load()
    {
        var list = new List<RecentMachine>();
        string? raw;
        try
        {
            raw = ApplicationData.Current.LocalSettings.Values[Key] as string;
        }
        catch (Exception)
        {
            return list; // chạy unpackaged, không có kho settings
        }
        if (string.IsNullOrEmpty(raw)) return list;

        foreach (var row in raw.Split(RowSep, StringSplitOptions.RemoveEmptyEntries))
        {
            var f = row.Split(FieldSep);
            if (f.Length < 4) continue;
            if (!long.TryParse(f[3], out var ticks)) continue;
            list.Add(new RecentMachine(f[0], f[1], f[2], new DateTimeOffset(ticks, TimeSpan.Zero)));
        }
        return list;
    }

    private static void Save(List<RecentMachine> list)
    {
        var raw = string.Join(RowSep, list.Select(m =>
            string.Join(FieldSep, m.Address, m.Name, m.Link, m.LastUsed.UtcTicks)));
        try
        {
            ApplicationData.Current.LocalSettings.Values[Key] = raw;
        }
        catch (Exception)
        {
            // Không lưu được thì danh sách vẫn đúng trong phiên này.
        }
    }
}
