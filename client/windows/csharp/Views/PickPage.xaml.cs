using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Deskhub.Controls;
using Deskhub.Interop;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;

namespace Deskhub.Views;

// =============================================================================
// PickPage — xem PickPage.xaml về bố cục và vì sao màn này tồn tại.
//
// HỎI NGUỒN CHẶN ~3 GIÂY
//   dh_client_list_sources phát lại LIST_SOURCES vài lượt vì UDP mất gói, nên nó
//   chạy trong Task nền y như vòng quét ở HomePage. Rời trang giữa chừng mà không
//   huỷ thì Task còn sống thêm vài giây rồi cập nhật vào phần tử XAML đã bị tháo.
//
// KHÔNG CÓ NGUỒN NÀO → ĐI LUÔN, KHÔNG BÁO LỖI
//   Host đời trước GĐ6 im lặng. Người dùng không sai gì cả, và hành vi đúng là hành
//   vi cũ: xem nguồn 0. Màn này tự chuyển tiếp, không kịp nhìn thấy.
// =============================================================================
public sealed partial class PickPage : Page
{
    private readonly List<HostSource> _sources = new();
    private readonly HashSet<byte> _picked = new();
    private CancellationTokenSource? _query;
    private string _address = "";
    private bool _sendInput = true;

    public PickPage()
    {
        InitializeComponent();
    }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        AppState.Changed += ApplyLanguage;

        if (e.Parameter is ViewerRequest req)
        {
            _address = req.Address;
            _sendInput = req.SendInput;
        }
        AllowInput.IsChecked = _sendInput;
        Aside.Text = _address;

        ApplyLanguage();
        StartQuery();
    }

    protected override void OnNavigatedFrom(NavigationEventArgs e)
    {
        AppState.Changed -= ApplyLanguage;
        _query?.Cancel();
        _query?.Dispose();
        _query = null;
    }

    // --- Chữ ---

    private void ApplyLanguage()
    {
        Eyebrow.Text = L.T("clientMode").ToUpperInvariant();
        Title.Text = L.T("pickTitle");
        SharedLabel.Text = L.T("sharedByHost").ToUpperInvariant();
        Hint.Text = _sources.Count == 0 ? L.T("askingSources") : L.T("pickHint");
        BackText.Text = L.T("back");
        StartText.Text = L.T("startViewing");
        AllowInput.Content = L.T("allowInput");
        RebuildList();
    }

    // --- Hỏi host ---

    private async void StartQuery()
    {
        var cts = new CancellationTokenSource();
        _query = cts;

        List<HostSource> found;
        try
        {
            // Task.Run vì lời gọi này CHẶN tới 3 giây; gọi trên UI thread là treo app.
            found = await Task.Run(() => NativeMethods.ListSources(_address), cts.Token);
        }
        catch (System.OperationCanceledException)
        {
            return;
        }
        catch (System.Exception)
        {
            // Thiếu DLL / không mở được socket. Không có gì người dùng làm được ở màn
            // này, và đường lùi đúng là đường cũ: xem nguồn 0.
            found = new List<HostSource>();
        }

        if (cts.IsCancellationRequested) return;

        _sources.Clear();
        _sources.AddRange(found);

        // Không nguồn nào = host bản cũ. Đi thẳng, đừng để người dùng nhìn màn trống.
        if (_sources.Count == 0)
        {
            GoToViewer(new List<HostSource>());
            return;
        }

        // Tick sẵn nguồn đầu tiên: bấm "Bắt đầu xem" ngay là chạy được, và nó nói cho
        // người dùng biết ô tick dùng để làm gì.
        _picked.Clear();
        _picked.Add(_sources[0].SourceId);

        LiveDot.IsLive = true;
        Hint.Text = L.T("pickHint");
        RebuildList();
    }

    // --- Danh sách ---

    private void RebuildList()
    {
        SourceList.Items.Clear();
        foreach (var s in _sources)
        {
            var item = s; // bắt biến theo từng vòng lặp, không phải biến vòng lặp
            bool on = _picked.Contains(item.SourceId);
            SourceList.Items.Add(SourceRow.Build(
                name: item.Name,
                detail: item.Resolution,
                kindLabel: L.T(item.IsDisplay ? "display" : "window"),
                isDisplay: item.IsDisplay,
                selected: on,
                state: L.T(on ? "streaming" : "idle"),
                tone: on ? StatePill.Tone.Live : StatePill.Tone.Neutral,
                // Ở đây chưa có phiên nào để mà dừng — bỏ tick là đủ.
                canStop: false,
                stopLabel: "",
                onToggle: v => Toggle(item.SourceId, v),
                onStop: () => { },
                // Bấm đâu trong dòng cũng tick: ở màn này chưa có phiên nào để mà lỡ
                // tay cắt, nên không có lý do bắt người dùng nhắm vào ô 18px.
                rowToggles: true));
        }
        UpdateCount();
    }

    private void Toggle(byte sourceId, bool on)
    {
        if (on) _picked.Add(sourceId);
        else _picked.Remove(sourceId);
        UpdateCount();
        RebuildList();
    }

    private void UpdateCount()
    {
        CountText.Text = string.Format(L.T("publishedCountFmt"), _sources.Count, _picked.Count);
        // Chưa tick gì thì không có gì để xem. Nút chính TẮT là lời chỉ dẫn, không
        // phải sự từ chối — xem ghi chú "Disabled = real guidance" của bản thiết kế.
        StartButton.IsEnabled = _picked.Count > 0;
    }

    // --- Nút ---

    private void OnBack(object sender, RoutedEventArgs e)
    {
        if (Frame.CanGoBack) Frame.GoBack();
        else Frame.Navigate(typeof(ConnectPage), _address);
    }

    private void OnStart(object sender, RoutedEventArgs e)
    {
        var picked = _sources.Where(s => _picked.Contains(s.SourceId)).ToList();
        if (picked.Count == 0) return;
        GoToViewer(picked);
    }

    private void GoToViewer(IReadOnlyList<HostSource> picked)
    {
        _query?.Cancel();
        bool sendInput = AllowInput.IsChecked == true;
        Frame.Navigate(typeof(ViewerPage), new ViewerRequest(
            _address, sendInput,
            SourceId: picked.Count > 0 ? picked[0].SourceId : (byte)0,
            Sources: picked.Count > 1 ? picked : null));
    }
}
