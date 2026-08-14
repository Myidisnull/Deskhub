[English](SECURITY.md) · [中文](SECURITY.zh.md) · **Tiếng Việt**

# Chính sách bảo mật của Deskhub

_Cập nhật lần cuối: 14 tháng 8, 2026_

> Đây là bản dịch của [`SECURITY.md`](SECURITY.md). Nếu hai bản có khác biệt, **bản tiếng
> Anh là bản chuẩn**.

Dự án mã nguồn mở là **Deskhub**; tên sản phẩm hiển thị trong ứng dụng là **System Runtime**. Chính sách này áp dụng cho cả hai.

## ⚠️ Đọc phần này trước

**Mã hoá phiên là tuỳ chọn và mặc định tắt.** Mọi host vẫn yêu cầu mã 4 chữ số. Khi tắt mã hoá, lưu lượng đi dạng thô: ai bắt được một gói tin đều đọc ra mã và có toàn quyền chuột/bàn phím trên máy đang chia sẻ. Khi bật mã hoá, video, thao tác và clipboard của phiên được mã hoá; gói dò tìm vẫn plaintext. Host sinh khoá phiên để bạn sao chép cho viewer, trừ khi bật *Escrow key to viewers* (cũng mặc định tắt) — khi đó ai đưa đúng passcode đều nhận khoá từ host.

Deskhub vẫn dành cho mạng bạn đã tin, hoặc VPN bên dưới. Nó **không** được xây để sống sót khi phơi ra Internet công cộng.

Chỉ có một quy tắc cứng:

> **Đừng bao giờ port-forward UDP 47777. Đừng bao giờ phơi máy đang chia sẻ trực tiếp ra
> Internet. Muốn truy cập từ xa, hãy dùng VPN — [Tailscale](https://tailscale.com) là thứ
> dự án này được kiểm thử cùng — và kết nối tới địa chỉ `100.x.y.z`.**

Theo đúng quy tắc đó, System Runtime an toàn để dùng. Phá vỡ nó là trao máy cho Internet. Bật mã hoá phiên khi LAN chưa hoàn toàn tin cậy; tắt escrow khi bạn có thể trao khoá ngoài băng. Quy tắc sản phẩm: [`docs/SPECIFICATION.md`](docs/SPECIFICATION.md) §9.

## Mô hình mối đe doạ

### Deskhub bảo vệ được những gì

| | |
|---|---|
| Dữ liệu tới tay nhà phát triển | Không có gì cả. Không máy chủ, không tài khoản, không telemetry, không SDK bên thứ ba. Xem [`PRIVACY.vi.md`](PRIVACY.vi.md). |
| Người xem từ xa tranh máy với bạn | "Host thắng": ngay khi bạn chạm vào chuột hoặc bàn phím thật, thao tác từ xa tạm dừng (đúng như vậy trên cả host Windows, macOS và Linux). |
| Phím bị kẹt ở trạng thái nhấn | Mọi phím mà phía từ xa đang giữ đều được nhả tự động khi phiên kết thúc hoặc người xem chuyển đi chỗ khác. |
| Người lạ không nghe lén được lưu lượng của bạn | Mọi host đều yêu cầu passcode. Các lần sai passcode hoặc khoá phiên từ cùng một nguồn bị giới hạn (**5** lần trong **60 giây**, rồi khoá **30 giây**). Gói dò tìm cũng bị giới hạn tốc độ để phản hồi danh sách màn hình không thành oracle vô hạn. Không gian 4 chữ số vẫn nhỏ: nếu tắt mã hoá hoặc bật escrow, máy quét trên LAN vẫn là rủi ro thật. |
| Phiên mã hoá tuỳ chọn | Khi bật *Encrypt session traffic*, video, thao tác và clipboard của phiên được mã hoá AEAD. Viewer cần khoá phiên trừ khi bật escrow. Host đang mã hoá từ chối cho vào plaintext. Dò tìm vẫn plaintext. |
| Những người xem tranh chuột với nhau | Tối đa 5 người xem một host, nhưng chỉ một người điều khiển: ai vào trước thì thắng, và thao tác của người vào sau bị bỏ qua cho tới khi người vào trước ngừng thao tác một giây. Người thứ 6 bị từ chối với lý do `Busy`. |
| Người xem mà bạn chỉ muốn cho xem màn hình | Chế độ chia sẻ chỉ xem, có trên mọi host, bỏ các gói điều khiển ngay tại host trước khi bất cứ thứ gì được đưa vào máy — nó không dựa vào việc yêu cầu client tự giác. Host Android và iOS luôn ở chế độ chỉ xem, không tắt được. |
| Điện thoại bị bỏ quên trong lúc đang chia sẻ | Chốt chặn là hệ điều hành chứ không phải Deskhub: Android giữ một thông báo thường trực và hỏi lại quyền quay màn hình ở từng lần chia sẻ, còn iOS luôn hiện chỉ báo broadcast. Cả hai đều dừng được phiên chia sẻ mà không cần mở app. |
| Gói tin dị dạng | Mọi trường đều được kiểm tra biên trước khi đọc. Các bộ phân tích gói được phủ bởi unit test, chạy dưới AddressSanitizer, UndefinedBehaviorSanitizer và ThreadSanitizer trong CI, và được fuzz mỗi đêm bằng libFuzzer — sáu target bao phủ định dạng gói tin, phân tích H.264, ráp gói, máy trạng thái phiên và văn bản UI. Crash do fuzz tìm ra được giữ lại trong repo làm regression test, và độ phủ mới được gộp ngược vào bộ seed corpus. |

### Deskhub **không** bảo vệ được những gì

Đây là danh sách thành thật. Chỉ passcode không giải quyết được những điều này; mã hoá
phiên tuỳ chọn thu hẹp một phần nhưng không phải tất cả:

- **Không có danh tính máy mạnh.** Passcode là 4 chữ số so sánh bằng phép bằng. Khi tắt
  mã hoá nó đi dạng thô trong `Hello`. Khi bật mã hoá và tắt escrow, khoá phiên mới là bí
  mật thật — nhưng vẫn không có bước ghép đôi, không hộp thoại phê duyệt trên host, không
  danh sách địa chỉ được phép.
- **Mã hoá là tuỳ chọn.** Lưu lượng mặc định là UDP plaintext. Ai bắt được phiên chưa mã
  hoá đều xem được màn hình và đọc được phím. Bật mã hoá chỉ phủ payload phiên; gói dò tìm
  vẫn plaintext.
- **Escrow làm yếu việc trao khoá.** Khi escrow bật, ai đưa đúng passcode đều nhận khoá
  phiên từ host. Khi LAN thù địch, hãy ưu tiên trao khoá ngoài băng.
- **Tên thiết bị vẫn có thể đi plaintext** trên các đường dò tìm/cho vào chưa được mã hoá
  phiên phủ. Tên trong ô *Your name* hiện trên host và ghi vào nhật ký host.
- **Không chống DoS.** Làm ngập cổng sẽ phá phiên; giới hạn tốc độ chỉ làm chậm đoán, không
  chặn lũ.
- **Beacon dò tìm vẫn trả lời.** `LIST_SOURCES` không cần phiên và nhận phản hồi từ mọi
  địa chỉ nguồn; `PING` cũng vậy. Passcode chỉ làm rỗng phần trả lời. Giới hạn tốc độ giảm
  lạm dụng oracle; nó không giấu việc có host Deskhub.
- **Một chỗ người xem tự giải phóng sau 5 giây im lặng.** Nếu người xem rớt, chỗ đó mở lại
  và `Hello` được chấp nhận tiếp theo chiếm chỗ.
- **Chia sẻ là phơi nguyên màn hình.** Không phải một cửa sổ. Xem [`PRIVACY.vi.md` §3.4](PRIVACY.vi.md).
- **Điện thoại hay máy tính bảng làm host là phơi nguyên cái máy.** Host di động luôn chỉ
  xem, loại rủi ro điều khiển từ xa nhưng không loại rủi ro lộ nội dung.

## Chạy ở đâu thì an toàn

✅ **An toàn**

- Mạng LAN ở nhà hoặc mạng cá nhân mà bạn kiểm soát mọi thiết bị trong đó.
- Một tailnet Tailscale (hoặc đường hầm WireGuard/VPN khác) mà chỉ thiết bị của bạn tham
  gia. VPN cung cấp phần mã hoá và phần kiểm tra danh tính mà Deskhub không có.
- Một máy chỉ đóng vai *client* (điện thoại, máy tính bảng, laptop không bao giờ chia sẻ
  màn hình). Client không nhận phiên vào.

❌ **Không an toàn — đừng làm**

- Port-forward UDP 47777 qua router, hoặc đặt máy đang chia sẻ vào DMZ.
- Chia sẻ màn hình trên Wi-Fi quán cà phê, khách sạn, sân bay, trường học, coworking hay
  hội nghị.
- Chia sẻ trên mạng LAN của văn phòng hay nhà trọ chung mà bạn không tin tưởng mọi thiết
  bị khác.
- Bất kỳ mạng nào có thiết bị khách, thiết bị IoT bạn không tự cấu hình, hay máy của bạn
  cùng phòng mà bạn không quản trị.
- Phơi cổng qua giao diện công cộng của một máy ảo cloud hay một dịch vụ tunnel công khai.

Mặc định socket lắng nghe trên mọi giao diện mạng (`INADDR_ANY`), nên nó tiếp cận được
từ mọi mạng mà máy đang nối vào — kể cả mạng bạn quên là mình đang nối. Cài đặt **Share
on network** thu hẹp điều này: chọn một địa chỉ của máy thì host chỉ bind đúng giao diện
đó, nên máy ở các mạng còn lại thậm chí không chạm được tới cổng. Hai lưu ý: nếu địa chỉ
đã chọn không còn tồn tại lúc bạn bắt đầu chia sẻ (rút cáp, DHCP cấp địa chỉ mới),
Deskhub quay về lắng nghe trên mọi giao diện và nói rõ điều đó trong dòng trạng thái
chia sẻ — hãy kiểm tra banner nếu bạn dựa vào tính năng này; và bind một giao diện cũng
chặn luôn viewer qua loopback (`127.0.0.1`) trên cùng máy. Trên Windows, app chạy với
quyền cao ngay từ lúc khởi động (nó xin một lần, để gõ được vào các cửa sổ quyền cao) và
tự mở luật tường lửa giúp bạn khi bạn chia sẻ — luật đó mở cho cả app trên mọi profile,
nên bind hẹp lại không làm tường lửa hẹp theo; chính sự tiện lợi đó làm cho quy tắc phía
trên trở nên quan trọng.

## Kẻ tấn công cùng mạng làm được gì

Nếu ai đó ở cùng mạng LAN với một máy đang chia sẻ màn hình, và Deskhub đang chạy, họ có
thể:

1. Phát hiện ra máy đó bằng cách quét cổng UDP 47777. Passcode làm rỗng danh sách màn
   hình và độ phân giải mà họ nhận về, nhưng máy vẫn trả lời gói dò, nên nó vẫn tự để lộ
   mình.
2. Đọc passcode của bạn ngay trên đường truyền rồi kết nối, và lập tức có chuột và bàn
   phím. Từ đó: mở trình duyệt, đọc email của bạn, cài phần mềm, rút dữ liệu ra ngoài.
   Passcode chỉ chặn được chừng nào họ chưa thấy gói tin nào của bạn — mã nằm phơi trong
   mọi gói `Hello`, nên ai nghe lén được mạng chỉ việc đọc nó rồi kết nối.
3. Dù kết nối được hay không, họ vẫn ghi lại phiên một cách thụ động và dựng lại màn hình
   cùng các phím bạn gõ ở chế độ ngoại tuyến.
4. Nếu đồng bộ clipboard đang bật, đọc mọi đoạn văn bản bạn copy trong lúc chia sẻ —
   văn bản clipboard đi trên cùng kênh UDP không mã hóa như mọi thứ khác. Mật khẩu copy
   từ trình quản lý mật khẩu là nạn nhân kinh điển; hãy tắt công tắc này trên mạng bạn
   không hoàn toàn tin tưởng.

Hành vi "host thắng" chỉ hạn chế được phá phách khi bạn *đang ngồi tại* máy. Nó không có
tác dụng gì khi bạn rời khỏi máy, mà đó mới là lúc quan trọng.

## Danh sách việc nên làm để giảm rủi ro

Nếu bạn muốn tiếp tục dùng Deskhub như hiện tại, những việc sau đáng làm:

- [ ] Chạy Tailscale trên cả hai máy và chỉ kết nối qua địa chỉ `100.x.y.z`.
- [ ] Kiểm tra chắc chắn router của bạn **không** có port-forward hay ánh xạ UPnP nào cho
      UDP 47777.
- [ ] Đổi passcode được sinh sẵn trong Settings thành mã mà người xem không đoán ra từ
      thói quen của bạn, và bỏ tích *Viewers can control this machine* mỗi khi bạn chỉ cần
      cho ai đó xem. Cả hai đều không thay thế được VPN — chúng chỉ nâng mức sàn với người
      lạ không đọc được lưu lượng của bạn.
- [ ] Thoát Deskhub khi bạn không dùng. Nó không chạy như dịch vụ nền — đóng app là đóng
      lỗ hổng.
- [ ] Trên Linux, nếu bạn dùng `ufw`, hãy giới hạn phạm vi luật thay vì mở toang:
      `sudo ufw allow from 100.64.0.0/10 to any port 47777 proto udp` thay cho
      `sudo ufw allow 47777/udp`.
- [ ] Đừng để một phiên chia sẻ chạy trên chiếc laptop mà bạn mang sang mạng khác.
- [ ] Khoá máy khi bạn rời đi, để một phiên không người trông không bị chiếm quyền âm
      thầm.

## Dấu vết để lại trên máy

Nhật ký chẩn đoán được ghi ở dạng văn bản thuần dưới `~/.deskhub/`
(`%USERPROFILE%\.deskhub` trên Windows) trên Windows, macOS và Linux. Chúng chứa thống kê
kết nối và địa chỉ của máy đối diện, không chứa nội dung màn hình hay phím gõ.

Các app desktop giữ thêm hai tệp trong thư mục đó: `ui-settings.txt` (fps, bitrate, giới
hạn độ phân giải, cổng, công tắc chỉ xem, passcode host của bạn, và tên thiết bị tuỳ chọn
hiển thị cho host — lưu đúng như văn bản bạn đã gõ) và
`recent-devices.txt` (10 địa chỉ gần nhất bạn đã kết nối, thời điểm, và passcode bạn dùng
cho từng địa chỉ). Các app di động giữ đúng hai tệp đó bên trong vùng sandbox của chúng —
trên iOS là trong app group container, để broadcast extension đọc đúng passcode host mà
app đang hiển thị cho bạn.
Passcode được lưu bằng cách che đi với một khoá XOR cố định, đủ để nó không hiện lên màn
hình và không lộ ra khi mở tệp xem qua — **đó không phải mã hoá**, và ai có mã nguồn cùng
tệp đó khôi phục lại chúng trong vài giây. Hãy coi thư mục đó là đọc được bởi mọi thứ
chạy dưới danh nghĩa tài khoản của bạn.

Không thứ gì trong số này được tải lên đâu cả; bạn xoá thư mục đó lúc nào cũng được.

## Các biện pháp giảm thiểu đã lên kế hoạch

Đang theo dõi, theo thứ tự dự định làm:

1. **Hộp thoại xác nhận kết nối trên host** — máy đang chia sẻ hỏi trước khi chấp nhận
   phiên đầu tiên, thay vì chấp nhận âm thầm.
2. **Lưu passcode và khoá phiên trong keychain của hệ điều hành** thay vì tệp văn bản bị
   che đi.
3. **Làm im beacon dò tìm** để nó không trả lời gì cả trước một gói dò không mời, thay vì
   trả lời bằng danh sách rỗng.

Đã giao / đã ghi trong đặc tả kể từ các lần sửa sớm hơn: passcode bắt buộc và công tắc
chỉ xem trên mọi host; mã hoá phiên tuỳ chọn với khoá phiên do host sinh, sao chép/làm mới,
vòng đời theo lần chia sẻ hoặc bền vững, và escrow khoá tuỳ chọn; từ chối hạ cấp host đang
mã hoá xuống plaintext; giới hạn tốc độ kết nối và dò tìm như [`docs/SPECIFICATION.md`](docs/SPECIFICATION.md)
§9. Phần triển khai đang được căn theo đặc tả đó nếu bản dựng còn lệch.

Danh sách này là tuyên bố về ý định, không phải lịch trình. Deskhub do một người duy trì
trong thời gian rảnh. Hãy coi hiện trạng là hiện trạng, không phải kế hoạch.

## Báo cáo lỗ hổng

Vui lòng báo cáo vấn đề bảo mật **một cách riêng tư** — đừng mở issue công khai trên
GitHub.

- **Email:** manhpv151090@gmail.com — ghi `[Deskhub security]` ở tiêu đề.
- **Hoặc:** mở một [security advisory riêng tư](https://github.com/manhpham90vn/Deskhub/security/advisories/new)
  trên GitHub.

Vui lòng nêu rõ bạn đang chạy gì (hệ điều hành, phiên bản Deskhub trên thanh tiêu đề hoặc
trong [`VERSION`](VERSION)), bạn đã làm gì, và chuyện gì đã xảy ra. Một proof of concept
giúp ích rất nhiều.

**Điều bạn có thể mong đợi:** một xác nhận đã nhận trong vòng 7 ngày và một đánh giá
trong vòng 30 ngày. Đây là dự án làm lúc rảnh của một người, nên mong bạn kiên nhẫn với
mốc thời gian — nhưng dù thế nào bạn cũng sẽ nhận được câu trả lời thẳng thắn. Nếu có bản
vá, bạn sẽ được ghi công trong release notes trừ khi bạn không muốn.

Không có chương trình thưởng lỗi; không có khoản chi trả nào.

**Những gì đã được ghi ở trên không phải là lỗ hổng.** Việc thiếu xác thực và mã hoá là
đã biết, đã liệt kê, và đang được xử lý — một báo cáo rằng Deskhub kết nối được mà không
cần mật khẩu không cho chúng tôi biết điều gì mới. Những gì *đáng* báo cáo: lỗi hỏng bộ
nhớ hoặc crash có thể kích hoạt từ một gói tin dị dạng, một cách thoát ra khỏi mô hình
mối đe doạ đã ghi nhận, bất cứ thứ gì làm rò dữ liệu ra khỏi máy, hoặc một lỗ hổng trong
một biện pháp giảm thiểu sau khi nó được triển khai.

## Các phiên bản được hỗ trợ

Chỉ bản phát hành mới nhất trên [trang Releases](https://github.com/manhpham90vn/Deskhub/releases)
được hỗ trợ. Bản vá đi kèm bản phát hành mới; không có backport về các phiên bản cũ.

## Phạm vi

Chính sách này áp dụng cho mã nguồn Deskhub trong kho này và các bản dựng nhị phân được
công bố trên trang Releases, TestFlight và Google Play. Nó không áp dụng cho Tailscale, hệ
điều hành của bạn, router của bạn, hay bất kỳ phần mềm nào khác bạn chạy song song.
