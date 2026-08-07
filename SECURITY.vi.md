[English](SECURITY.md) · **Tiếng Việt**

# Chính sách bảo mật của Deskhub

_Cập nhật lần cuối: 7 tháng 8, 2026_

> Đây là bản dịch của [`SECURITY.md`](SECURITY.md). Nếu hai bản có khác biệt, **bản tiếng
> Anh là bản chuẩn**.

## ⚠️ Đọc phần này trước

**Deskhub không có lớp mã hoá nào của riêng nó. Mọi host đều yêu cầu mã 4 chữ số, nhưng
mã đó đi ở dạng thô, nên nó chỉ chặn được người lạ không nhìn thấy lưu lượng của bạn, chỉ
vậy thôi. Bất kỳ ai bắt được một gói tin đều đọc ra mã, và từ đó có toàn quyền chuột và
bàn phím trên máy đang chia sẻ.**

Mọi host cũng có thể chia sẻ ở chế độ **chỉ xem** (thao tác gửi tới bị bỏ đi thay vì được
đưa vào máy). Cả hai tuỳ chọn nằm trong mục Settings của app Windows, macOS và Linux, và
mọi client — Windows, macOS, Linux, Android, iOS — đều nhập được passcode.

Đây là lựa chọn thiết kế có chủ đích, không phải sơ suất — Deskhub được xây để chạy bên
trong một mạng mà bạn vốn đã tin tưởng, và để mượn phần mã hoá cùng phần xác thực danh
tính từ tầng bên dưới nó (mạng LAN của bạn, hoặc một đường hầm WireGuard như Tailscale).
Nó **không** được xây để sống sót khi phơi ra Internet công cộng.

Nên chỉ có đúng một quy tắc:

> **Đừng bao giờ port-forward UDP 47777. Đừng bao giờ phơi máy đang chia sẻ trực tiếp ra
> Internet. Muốn truy cập từ xa, hãy dùng VPN — [Tailscale](https://tailscale.com) là thứ
> dự án này được kiểm thử cùng — và kết nối tới địa chỉ `100.x.y.z`.**

Nếu bạn theo đúng quy tắc đó, Deskhub an toàn để dùng. Nếu bạn phá vỡ nó, bạn đang trao
máy của mình cho Internet.

## Mô hình mối đe doạ

### Deskhub bảo vệ được những gì

| | |
|---|---|
| Dữ liệu tới tay nhà phát triển | Không có gì cả. Không máy chủ, không tài khoản, không telemetry, không SDK bên thứ ba. Xem [`PRIVACY.vi.md`](PRIVACY.vi.md). |
| Người xem từ xa tranh máy với bạn | "Host thắng": ngay khi bạn chạm vào chuột hoặc bàn phím thật, thao tác từ xa tạm dừng (đúng như vậy trên cả host Windows, macOS và Linux). |
| Phím bị kẹt ở trạng thái nhấn | Mọi phím mà phía từ xa đang giữ đều được nhả tự động khi phiên kết thúc hoặc người xem chuyển đi chỗ khác. |
| Người lạ không nghe lén được lưu lượng của bạn | Mọi host đều yêu cầu passcode: mã sai bị từ chối, và cứ mỗi ba lần sai thì host khoá 30 giây. Điều đó giới hạn việc dò ở mức 3 lần thử mỗi nửa phút, nên đi hết 10 000 tổ hợp sẽ mất khoảng một ngày thử liên tục không nghỉ. Gói dò tìm không có mã đúng chỉ nhận về danh sách rỗng thay vì tên các màn hình của bạn. |
| Những người xem tranh chuột với nhau | Tối đa 5 người xem một host, nhưng chỉ một người điều khiển: ai vào trước thì thắng, và thao tác của người vào sau bị bỏ qua cho tới khi người vào trước ngừng thao tác một giây. Người thứ 6 bị từ chối với lý do `Busy`. |
| Người xem mà bạn chỉ muốn cho xem màn hình | Chế độ chia sẻ chỉ xem, có trên mọi host, bỏ các gói điều khiển ngay tại host trước khi bất cứ thứ gì được đưa vào máy — nó không dựa vào việc yêu cầu client tự giác. |
| Gói tin dị dạng | Mọi trường đều được kiểm tra biên trước khi đọc. Các bộ phân tích gói được phủ bởi unit test và chạy dưới AddressSanitizer, UndefinedBehaviorSanitizer và ThreadSanitizer trong CI. |

### Deskhub **không** bảo vệ được những gì

Đây là danh sách thành thật. Không điều nào dưới đây được giải quyết ở thời điểm hiện
tại — và passcode không giải quyết được điều nào cả:

- **Không có xác thực thật sự.** Passcode là 4 chữ số gửi ở dạng thô bên trong gói
  `Hello` và được so sánh bằng phép bằng — nó là cái khoá cửa, không phải phép kiểm tra
  danh tính bằng mật mã. Ai bắt được một gói tin là có nó mãi mãi và phát lại được. Việc
  bắt buộc phải có mã đã loại bỏ trường hợp "không có mã nào cả", nhưng vẫn không có bước
  ghép đôi, không có hộp thoại phê duyệt, và không có danh sách địa chỉ được phép: ai gửi
  đúng bốn chữ số trước thì người đó vào.
- **Không có mã hoá.** Không TLS, không DTLS, không Noise, không có lớp mật mã nào ở tầng
  ứng dụng trong mã nguồn này. Khung hình, phím gõ và chuyển động chuột đều đi dưới dạng
  UDP thô. Ai bắt được lưu lượng của bạn đều xem được màn hình của bạn và đọc được mọi
  thứ bạn gõ.
- **Không bảo vệ toàn vẹn.** Gói tin không được ký hay xác thực, nên kẻ tấn công chèn
  được lưu lượng thì giả mạo được sự kiện nhập liệu.
- **Không chống được việc chiếm phiên trên mạng dùng chung.** Một phiên đang chạy chỉ
  được nhận diện bằng một session id 32 bit. Nó được sinh từ CSPRNG của hệ điều hành
  (`BCryptGenRandom` / `arc4random_buf` / `getrandom`), nên đoán mò là không khả thi —
  nhưng trên mạng mà kẻ tấn công *nghe lén* được gói tin của bạn, id đó nằm phơi trong
  từng gói một. Có nó, kẻ tấn công chèn được thao tác và chuyển hướng được luồng video về
  địa chỉ của chính họ.
- **Không giới hạn tần suất, không chống DoS.** Làm ngập cổng sẽ phá hỏng phiên đang chạy.
- **Beacon dò tìm trả lời bất kỳ ai.** Gói `LIST_SOURCES` không cần phiên và nhận được
  phản hồi từ bất kỳ địa chỉ nguồn nào; `PING` cũng vậy. Passcode chỉ làm rỗng phần trả
  lời — gói dò không có mã đúng được báo "không chia sẻ gì" thay vì nhận tên và độ phân
  giải các màn hình của bạn. Dù thế nào gói tin vẫn quay về, nên máy vẫn bị phát hiện
  được bằng cách quét, và cổng vẫn dùng được như một bộ phản xạ UDP nhỏ.
- **Một chỗ người xem tự giải phóng sau 5 giây im lặng.** Nếu người xem của bạn rớt mạng,
  chỗ đó mở lại và gói `Hello` tới tiếp theo sẽ chiếm — bất kể ai gửi, chỉ phải qua cửa
  passcode.
- **Chia sẻ là phơi nguyên màn hình.** Không phải một cửa sổ: mọi thông báo, cửa sổ bật
  lên và cửa sổ trên màn hình đó. Xem [`PRIVACY.vi.md` §3.3](PRIVACY.vi.md).

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

Socket lắng nghe trên mọi giao diện mạng (`INADDR_ANY`), nên nó tiếp cận được từ mọi mạng
mà máy đang nối vào — kể cả mạng bạn quên là mình đang nối. Trên Windows, app chạy với
quyền cao ngay từ lúc khởi động (nó xin một lần, để gõ được vào các cửa sổ quyền cao) và
tự mở luật tường lửa giúp bạn khi bạn chia sẻ; chính sự tiện lợi đó làm cho quy tắc phía
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
hạn độ phân giải, cổng, công tắc chỉ xem, và passcode host của bạn) và
`recent-devices.txt` (10 địa chỉ gần nhất bạn đã kết nối, thời điểm, và passcode bạn dùng
cho từng địa chỉ). Các app di động giữ đúng hai tệp đó bên trong vùng sandbox của chúng.
Passcode được lưu bằng cách che đi với một khoá XOR cố định, đủ để nó không hiện lên màn
hình và không lộ ra khi mở tệp xem qua — **đó không phải mã hoá**, và ai có mã nguồn cùng
tệp đó khôi phục lại chúng trong vài giây. Hãy coi thư mục đó là đọc được bởi mọi thứ
chạy dưới danh nghĩa tài khoản của bạn.

Không thứ gì trong số này được tải lên đâu cả; bạn xoá thư mục đó lúc nào cũng được.

## Các biện pháp giảm thiểu đã lên kế hoạch

Đang theo dõi, theo thứ tự dự định làm:

1. **Hộp thoại xác nhận kết nối trên host** — máy đang chia sẻ hỏi trước khi chấp nhận
   phiên đầu tiên, thay vì chấp nhận âm thầm.
2. **Mã hoá và xác thực thật sự** — một phép trao đổi khoá có xác thực (Noise IK hoặc
   DTLS) để Deskhub không còn phụ thuộc vào việc mạng có đáng tin hay không, và để
   passcode thôi bị đọc được trên đường truyền. Cho tới khi việc này hoàn thành, quy tắc ở
   đầu tài liệu này chính là toàn bộ mô hình bảo mật.
3. **Lưu passcode trong keychain của hệ điều hành** thay vì một tệp văn bản bị che đi.
4. **Làm im beacon dò tìm** để nó không trả lời gì cả trước một gói dò không mời, thay vì
   trả lời bằng danh sách rỗng.

Đã hoàn thành kể từ lần cập nhật trước của danh sách này: passcode và công tắc chỉ xem
trên mọi host, ô nhập passcode trong mọi client, passcode host được sinh ra ở lần chạy
đầu thay vì để trống, và beacon dò tìm giấu danh sách màn hình khỏi các gói dò không có
mã đúng. Không cái nào trong số đó thay thế được mục 2.

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
