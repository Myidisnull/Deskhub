[English](PRIVACY.md) · **Tiếng Việt**

# Chính sách quyền riêng tư của Deskhub

_Ngày hiệu lực: 7 tháng 8, 2026 — Phiên bản 1.2_

> Đây là bản dịch của [`PRIVACY.md`](PRIVACY.md). Nếu hai bản có khác biệt, **bản tiếng
> Anh là bản có giá trị pháp lý**.

## 1. Giới thiệu

Chính sách quyền riêng tư này mô tả cách **Deskhub** ("ứng dụng", "chúng tôi") xử lý
thông tin khi bạn sử dụng các ứng dụng di động Deskhub (iOS, Android) và các ứng dụng
desktop Deskhub cho Windows, macOS và Linux (gọi chung là "Phần mềm").

Deskhub là một ứng dụng remote desktop: nó truyền màn hình của một trong các máy tính của
bạn tới một thiết bị khác của bạn và cho phép bạn điều khiển máy tính đó bằng chuột, bàn
phím và thao tác chạm.

Phần mềm được phát triển và phát hành bởi một nhà phát triển cá nhân:

- **Nhà phát triển:** Manh Pham
- **Liên hệ:** manhpv151090@gmail.com
- **Trang dự án:** https://github.com/manhpham90vn/Deskhub

## 2. Bản tóm tắt

**Deskhub không thu thập, lưu trữ, bán hay chia sẻ bất kỳ dữ liệu cá nhân nào. Chúng tôi
không vận hành máy chủ nào, và không dữ liệu nào về bạn hay về cách bạn sử dụng từng đi
tới chúng tôi hay bất kỳ bên thứ ba nào thông qua Phần mềm.** Không có tài khoản người
dùng, không analytics, không báo cáo sự cố, không quảng cáo, và không có SDK bên thứ ba
nào được nhúng trong Phần mềm.

## 3. Thông tin mà Phần mềm xử lý

Để hoạt động, Phần mềm phải xử lý một số dữ liệu **hoàn toàn trên và giữa các thiết bị
của chính bạn**. Không dữ liệu nào trong số đó được truyền tới nhà phát triển hay bên thứ
ba.

| Dữ liệu | Mục đích | Nó đi đâu | Thời gian lưu |
|---|---|---|---|
| Nội dung màn hình của máy đang chia sẻ (các khung hình) | Hiển thị màn hình đó trên thiết bị còn lại của bạn | Gửi trực tiếp giữa hai thiết bị của bạn, chỉ được mã hoá trên đường truyền bởi chính lớp mạng/VPN của bạn | Không bao giờ lưu; chỉ tồn tại trong bộ nhớ trong lúc phiên đang chạy |
| Thao tác chuột, bàn phím và chạm | Điều khiển máy đang chia sẻ từ thiết bị còn lại | Gửi trực tiếp từ thiết bị đang xem tới máy đang chia sẻ | Không bao giờ lưu; bỏ đi ngay sau khi được đưa vào máy |
| Địa chỉ (IP/hostname) bạn nhập | Kết nối tới máy kia | Nằm lại trên chính thiết bị bạn đã nhập | Giữ cục bộ cho tới khi bạn thay đổi |
| 10 địa chỉ gần nhất bạn đã kết nối, thời điểm của từng lần, và mã passcode bạn dùng cho từng địa chỉ | Điền vào danh sách *Recent devices* để bạn kết nối lại bằng một cú bấm | Ghi vào `recent-devices.txt` trong thư mục riêng của app trên thiết bị bạn — `%USERPROFILE%\.deskhub` trên Windows, `~/.deskhub` trên macOS và Linux, vùng sandbox của app trên iOS và Android | Giữ cho tới khi bạn kết nối tới 10 địa chỉ mới hơn, hoặc bạn xoá tệp đó |
| Trên Windows, macOS và Linux: tuỳ chọn chia sẻ của bạn (tốc độ khung hình, bitrate, giới hạn độ phân giải, cổng, có cho người xem điều khiển máy hay không, và mã passcode bạn yêu cầu người xem nhập) | Khôi phục cài đặt cho lần mở app kế tiếp | Ghi vào `ui-settings.txt` trong cùng thư mục | Giữ cho tới khi bạn thay đổi hoặc xoá tệp |
| Thống kê kết nối (bitrate, tỉ lệ mất gói, độ trễ) | Điều chỉnh chất lượng luồng; hiển thị trên thanh trạng thái | Chỉ trao đổi giữa hai thiết bị của bạn | Không bao giờ lưu; bỏ đi khi phiên kết thúc |

### 3.1 Ngang hàng theo thiết kế

Toàn bộ liên lạc diễn ra **trực tiếp giữa hai thiết bị của chính bạn**, qua:

- mạng nội bộ của bạn (Wi-Fi/LAN), hoặc
- một VPN do **bạn** vận hành hoặc thuê bao (ví dụ Tailscale), nếu bạn chọn dùng để truy
  cập qua Internet.

Chúng tôi không vận hành máy chủ trung chuyển, máy chủ báo hiệu, hay bất kỳ backend nào
khác. Phần mềm không có phương tiện kỹ thuật nào để gửi dữ liệu về nhà phát triển.

### 3.2 Dữ liệu chúng tôi KHÔNG xử lý

Phần mềm không truy cập hay xử lý: tên, địa chỉ email, số điện thoại, danh bạ, vị trí,
ảnh, tệp (ngoài những gì hiển thị trên màn hình PC mà chính bạn chọn để truyền),
micro, camera, mã định danh quảng cáo, hay bất kỳ mã định danh thiết bị nào vượt quá mức
hệ điều hành cần để chạy ứng dụng.

### 3.3 Phạm vi của việc chia sẻ màn hình và điều khiển từ xa

Chia sẻ sẽ truyền **toàn bộ màn hình được chọn**: mọi thứ xuất hiện trên màn hình đó đều
hiện ra với người xem đang kết nối, bao gồm thông báo, cửa sổ bật lên, và bất kỳ cửa sổ
nào bạn mở trong lúc đang chia sẻ. (Tính năng chia sẻ riêng một cửa sổ ứng dụng đã bị gỡ
ngày 2026-07-27; Phần mềm hiện chỉ chia sẻ nguyên màn hình.) Khi bạn cho phép điều khiển
từ xa, thao tác của người xem được đưa vào máy như thể họ đang ngồi tại PC và có thể chạm
tới **mọi ứng dụng hiển thị trên màn hình đang chia sẻ** — không còn giới hạn trong một
cửa sổ nữa. Trên mọi host bạn đều có thể tắt hẳn điều khiển từ xa trong mục Settings,
khiến phiên chia sẻ thành chỉ xem: thao tác gửi tới sẽ bị bỏ đi thay vì được đưa vào máy. Hai
cơ chế an toàn luôn hoạt động khi điều khiển được bật: nếu người ngồi tại PC chạm vào
chuột hoặc bàn phím thật, thao tác từ xa tạm dừng ("host thắng"), và mọi phím mà phía từ
xa đang giữ sẽ được tự động nhả khi kết nối kết thúc hoặc người xem chuyển đi chỗ khác.
Tối đa năm người xem có thể cùng xem một PC, nhưng tại mỗi thời điểm chỉ một trong số họ
điều khiển chuột và bàn phím.

## 4. Các quyền mà ứng dụng yêu cầu

| Nền tảng | Quyền | Lý do |
|---|---|---|
| iOS | Local Network | iOS bắt buộc phải có để gửi/nhận dữ liệu tới PC của bạn trong cùng mạng. Chỉ dùng cho phiên truyền hình ảnh. |
| Android | `INTERNET`, trạng thái mạng | Cần để mở kết nối UDP tới PC của bạn. Chỉ dùng cho phiên truyền hình ảnh. |

Ứng dụng không yêu cầu quyền nào khác. Nếu một phiên bản sau này cần thêm quyền mới,
quyền đó sẽ được xin đúng ngữ cảnh và chính sách này sẽ được cập nhật.

## 5. Analytics, quảng cáo và bên thứ ba

- **Analytics / telemetry:** không có.
- **Báo cáo sự cố:** không có. Nhật ký chẩn đoán (`[DIAG]`) chỉ tồn tại trên chính máy
  bạn — trong cửa sổ console của app và, trên Windows, macOS và Linux, trong các tệp văn
  bản thuần dưới `~/.deskhub/` (`%USERPROFILE%\.deskhub` trên Windows). Chúng không bao
  giờ được tải lên đâu cả; chúng chỉ rời khỏi thiết bị của bạn nếu chính bạn sao chép và
  gửi đi, và bạn có thể xoá thư mục đó bất cứ lúc nào.
- **Quảng cáo:** không có.
- **SDK bên thứ ba:** không có. Phần mềm chỉ được xây từ mã nguồn của chính nó (công khai
  tại trang dự án) và các framework của hệ điều hành.
- **Chợ ứng dụng:** ứng dụng được phân phối qua Apple App Store và Google Play. Apple và
  Google có thể thu thập thống kê cài đặt/sử dụng theo chính sách quyền riêng tư của
  riêng họ; việc thu thập đó nằm ngoài tầm kiểm soát của chúng tôi và chúng tôi chỉ nhận
  được những thống kê tổng hợp, ẩn danh mà các nền tảng đó hiển thị cho mọi nhà phát
  triển.
- **Tailscale hoặc các VPN khác:** nếu bạn chọn kết nối qua VPN, lưu lượng của bạn được
  xử lý theo chính sách quyền riêng tư của nhà cung cấp đó. Deskhub không yêu cầu cũng
  không đóng gói kèm VPN nào.

## 6. Bảo mật

- Lưu lượng truyền hình ảnh nằm trong chính mạng của bạn hoặc trong đường hầm VPN của
  bạn. Khi bạn dùng VPN như Tailscale, lưu lượng giữa các thiết bị được VPN đó mã hoá
  đầu-cuối (WireGuard).
- Trên mạng nội bộ thông thường, Deskhub không mã hoá thêm cho lưu lượng. Mọi host đều
  yêu cầu mã 4 chữ số trước khi chấp nhận kết nối — mã được sinh tự động cho bạn ở lần
  chạy đầu tiên và bạn đổi được — nhưng mã đó đi ở dạng thô như phần còn lại của lưu
  lượng, nên nó chỉ chặn được người không bắt được gói tin của bạn. Chỉ dùng Deskhub trên
  các mạng bạn tin tưởng, hoặc qua VPN, và đừng bao giờ phơi nó trực tiếp ra Internet. Mô hình mối
  đe doạ đầy đủ — cái gì được bảo vệ, cái gì không, và cách báo lỗ hổng — nằm trong
  [`SECURITY.vi.md`](https://github.com/manhpham90vn/Deskhub/blob/main/SECURITY.vi.md).
- Các mã passcode lưu trong `recent-devices.txt` và `ui-settings.txt` được che đi bằng
  một khoá cố định để không đọc được ngay khi nhìn vào. Đó không phải mã hoá và không
  nhằm chống lại người vốn đã truy cập được tài khoản người dùng của bạn.
- Vì chúng tôi không giữ dữ liệu nào về bạn, nên không có cơ sở dữ liệu nào phía nhà phát
  triển để mà bị rò rỉ.

## 7. Lưu trữ và xoá dữ liệu

Chúng tôi không giữ gì cả, nên chúng tôi không có gì để xoá. Toàn bộ dữ liệu phiên biến
mất khi phiên kết thúc. Địa chỉ lưu trong app được xoá bằng cách xoá trắng ô nhập hoặc gỡ
cài đặt app. Danh sách thiết bị gần đây và các cài đặt đã lưu — bao gồm mọi passcode —
được xoá bằng cách xoá thư mục riêng của app (`%USERPROFILE%\.deskhub` trên Windows,
`~/.deskhub` trên macOS và Linux), app sẽ tạo lại thư mục rỗng ở lần khởi động kế tiếp;
trên iOS và Android, gỡ cài đặt app là xoá hết.

## 8. Quyền của bạn (GDPR, CCPA và các luật tương tự)

Các đạo luật như Quy định bảo vệ dữ liệu chung của EU (GDPR) và Đạo luật quyền riêng tư
người tiêu dùng California (CCPA) trao cho bạn các quyền đối với dữ liệu cá nhân — truy
cập, chỉnh sửa, xoá, di chuyển, phản đối, và không bị phân biệt đối xử.

Vì Deskhub không thu thập hay nắm giữ bất kỳ dữ liệu cá nhân nào, nên không có dữ liệu
nào để thực thi các quyền đó. Nếu bạn cho rằng chúng tôi có giữ dữ liệu về bạn, hãy liên
hệ theo địa chỉ bên dưới và chúng tôi sẽ phản hồi trong vòng 30 ngày.

Chúng tôi không "bán" hay "chia sẻ" thông tin cá nhân theo định nghĩa của CCPA.

## 9. Quyền riêng tư của trẻ em

Phần mềm không hướng tới trẻ em và, như đã nêu ở trên, không thu thập dữ liệu từ bất kỳ
ai, kể cả trẻ em dưới 13 tuổi (COPPA) hay dưới 16 tuổi (GDPR).

## 10. Truyền dữ liệu xuyên biên giới

Không có. Dữ liệu của bạn không bao giờ rời khỏi các thiết bị và mạng của chính bạn thông
qua Phần mềm.

## 11. Thay đổi chính sách này

Nếu cách xử lý dữ liệu của Phần mềm có thay đổi (ví dụ một phiên bản sau này thêm tính
năng báo cáo sự cố tuỳ chọn), chính sách này sẽ được cập nhật **trước khi** thay đổi đó
được phát hành, với ngày hiệu lực mới và một dòng trong bảng lịch sử bên dưới. Phiên bản
hiện hành luôn được công bố tại:
https://github.com/manhpham90vn/Deskhub/blob/main/PRIVACY.md
(bản tiếng Việt: https://github.com/manhpham90vn/Deskhub/blob/main/PRIVACY.vi.md)

| Phiên bản | Ngày | Thay đổi |
|---|---|---|
| 1.2 | 2026-08-07 | Passcode nay là bắt buộc trên mọi host, được sinh ra ở lần chạy đầu thay vì để trống, và mọi client đều nhập được. Cài đặt chia sẻ nay được lưu trên macOS và Linux chứ không chỉ Windows, còn danh sách thiết bị gần đây được lưu trên mọi nền tảng, đều nằm trong thư mục cục bộ của chính app. Không có dữ liệu mới nào rời khỏi thiết bị của bạn. |
| 1.1 | 2026-08-05 | App Windows nay lưu dữ liệu giữa các lần chạy: danh sách 10 địa chỉ gần nhất bạn đã kết nối, cài đặt chia sẻ của bạn, và các passcode dùng với chúng. Tất cả nằm trong `%USERPROFILE%\.deskhub` trên chính máy bạn; không có gì được truyền đi đâu cả. Ghi nhận thêm chế độ chia sẻ chỉ xem và giới hạn 5 người xem. |
| 1.0 | 2026-07-24 | Công bố lần đầu. |

## 12. Liên hệ

Mọi thắc mắc về chính sách này hoặc về quyền riêng tư trong Deskhub:

- **Email:** manhpv151090@gmail.com
- **Issues:** https://github.com/manhpham90vn/Deskhub/issues
