[English](PRIVACY.md) · [中文](PRIVACY.zh.md) · **Tiếng Việt**

# Chính sách quyền riêng tư của System Runtime

_Ngày hiệu lực: 14 tháng 8, 2026 — Phiên bản 1.10_

> Đây là bản dịch của [`PRIVACY.md`](PRIVACY.md). Nếu hai bản có khác biệt, **bản tiếng
> Anh là bản có giá trị pháp lý**.

## 1. Giới thiệu

Chính sách quyền riêng tư này mô tả cách **System Runtime** (dự án mã nguồn mở **Deskhub**; "ứng dụng", "chúng tôi") xử lý
thông tin khi bạn sử dụng các ứng dụng di động System Runtime / Deskhub (iOS, Android) và các ứng dụng
desktop cho Windows, macOS và Linux (gọi chung là "Phần mềm").

System Runtime là một ứng dụng remote desktop: nó truyền màn hình của một trong các máy tính của
bạn tới một thiết bị khác của bạn và cho phép bạn điều khiển máy tính đó bằng chuột, bàn
phím và thao tác chạm.

Phần mềm được phát triển và phát hành bởi một nhà phát triển cá nhân:

- **Nhà phát triển:** Manh Pham
- **Liên hệ:** manhpv151090@gmail.com
- **Trang dự án:** https://github.com/manhpham90vn/Deskhub

## 2. Bản tóm tắt

**System Runtime không thu thập, lưu trữ, bán hay chia sẻ bất kỳ dữ liệu cá nhân nào. Chúng tôi
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
| Nội dung màn hình của máy đang chia sẻ (các khung hình) | Hiển thị màn hình đó trên thiết bị còn lại của bạn | Gửi trực tiếp giữa hai thiết bị của bạn; khi bật *Encrypt session traffic*, payload phiên được System Runtime mã hoá, nếu không thì chỉ bởi lớp mạng/VPN của bạn (nếu có) | Không bao giờ lưu; chỉ tồn tại trong bộ nhớ trong lúc phiên đang chạy |
| Thao tác chuột, bàn phím và chạm | Điều khiển máy đang chia sẻ từ thiết bị còn lại | Gửi trực tiếp từ thiết bị đang xem tới máy đang chia sẻ (mã hoá theo phiên khi bật mã hoá) | Không bao giờ lưu; bỏ đi ngay sau khi được đưa vào máy |
| Địa chỉ (IP/hostname) bạn nhập | Kết nối tới máy kia | Nằm lại trên chính thiết bị bạn đã nhập | Giữ cục bộ cho tới khi bạn thay đổi |
| 10 địa chỉ gần nhất bạn đã kết nối, thời điểm của từng lần, mã passcode bạn dùng cho từng địa chỉ, và khi áp dụng liệu kết nối có mã hoá cùng khoá phiên đã dùng | Điền vào danh sách *Recent devices* để bạn kết nối lại bằng một cú bấm | Ghi vào `recent-devices.txt` trong thư mục riêng của app trên thiết bị bạn — `%USERPROFILE%\.system-runtime` trên Windows, `~/.system-runtime` trên macOS và Linux, vùng sandbox của app trên iOS và Android | Giữ cho tới khi bạn kết nối tới 10 địa chỉ mới hơn, hoặc bạn xoá tệp đó |
| Tuỳ chọn chia sẻ của bạn (tốc độ khung hình, bitrate, giới hạn độ phân giải, cổng, địa chỉ mạng để chia sẻ, có cho người xem điều khiển máy hay không, các công tắc đồng bộ clipboard, khởi động cùng hệ điều hành, tự chia sẻ và chế độ chạy nền, giới hạn lưu giữ log, thư mục log tuỳ chọn, mã passcode bạn yêu cầu người xem nhập, và khi dùng các công tắc mã hoá phiên, vòng đời khoá, escrow và khoá phiên) | Khôi phục cài đặt cho lần mở app kế tiếp | Ghi vào `ui-settings.txt` trong cùng thư mục. Trên iOS tệp này nằm trong app group container mà app và broadcast extension dùng chung, để cả hai phần thống nhất passcode, cổng và cài đặt mã hoá | Giữ cho tới khi bạn thay đổi hoặc xoá tệp |
| Văn bản clipboard (chỉ khi công tắc đồng bộ clipboard đang bật và có phiên đang chạy) | Để văn bản copy trên một thiết bị dán được trên các thiết bị kia | Gửi trực tiếp giữa các thiết bị của bạn — mã hoá theo phiên khi bật mã hoá, nếu không thì plaintext như lưu lượng chưa mã hoá khác — giới hạn 32 KiB mỗi lần copy; chỉ văn bản thuần, không bao giờ là ảnh hay tệp | System Runtime không bao giờ lưu; chỉ nằm trong clipboard hệ thống bình thường của mỗi thiết bị |
| Các tệp nhật ký chẩn đoán ghi trong lúc app desktop chạy (`system-runtime-*.log`, và các tệp cũ hơn được nén thành `system-runtime-*.log.gz`) | Khắc phục sự cố cục bộ và đính kèm vào báo lỗi nếu bạn chọn gửi | Ghi vào thư mục log bạn chọn trong Cài đặt, hoặc vào `~/.system-runtime/` trên macOS và Linux / `%USERPROFILE%\.system-runtime` trên Windows khi để trống. Hiện trong Cài đặt trên các nền tảng đó. Không bao giờ tự tải lên | Mỗi ngày lịch một tệp, ghi nối tiếp qua các lần khởi động. Tách khi một tệp vượt quá kích thước bạn đặt (mặc định 10 MB). Log thường cũ hơn tuổi nén (mặc định 7 ngày) thành `.log.gz`. Tệp cũ hơn tuổi xoá (mặc định 30 ngày) bị gỡ. Bạn có thể đổi các tuổi đó và thư mục trong Cài đặt, hoặc tự xoá các tệp |
| Việc có đang phát hay không, số người xem đang kết nối, mức bộ nhớ tính bằng megabyte mà chính broadcast extension đang dùng, và nội dung lỗi khởi động gần nhất (chỉ trên iOS) | Để màn hình chia sẻ của app báo được trạng thái của broadcast extension, thứ mà iOS chạy như một tiến trình riêng và sẽ chấm dứt nếu nó dùng quá nhiều bộ nhớ | Ghi vào `broadcast-status.txt` trong cùng app group container đó | Xoá đi khi buổi phát kết thúc |
| Tên thiết bị trong ô *Your name* — được điền sẵn tên của chính máy hoặc thiết bị này (hostname trên Windows và Linux, tên máy tính trên macOS, tên thiết bị trên iOS, model trên Android) cho tới khi bạn sửa nó | Hiển thị trên host mà bạn kết nối tới, bên cạnh địa chỉ của thiết bị này, để người đang chia sẻ phân biệt được các người xem | Lưu vào `ui-settings.txt` trong cùng thư mục, và gửi tới host khi bạn kết nối — ở dạng thô, như phần còn lại của lưu lượng — nên tên mặc định này được gửi đi trừ khi bạn thay nó bằng một tên do bạn tự chọn; xoá trắng ô chỉ khôi phục lại tên mặc định, và tên đó vẫn được lưu và gửi đi. Host chỉ giữ nó trong bộ nhớ, chỉ trong thời gian kết nối | Mặc định là tên của máy hoặc thiết bị; giữ cho tới khi bạn đổi nó hoặc xoá tệp. Xoá trắng ô chỉ khôi phục tên mặc định chứ không bỏ được tên |
| Thống kê kết nối (bitrate, tỉ lệ mất gói, độ trễ) | Điều chỉnh chất lượng luồng; hiển thị trên thanh trạng thái | Chỉ trao đổi giữa hai thiết bị của bạn | Không bao giờ lưu; bỏ đi khi phiên kết thúc |

### 3.1 Ngang hàng theo thiết kế

Toàn bộ liên lạc diễn ra **trực tiếp giữa hai thiết bị của chính bạn**, qua:

- mạng nội bộ của bạn (Wi-Fi/LAN), hoặc
- một VPN do **bạn** vận hành hoặc thuê bao (ví dụ Tailscale), nếu bạn chọn dùng để truy
  cập qua Internet.

Chúng tôi không vận hành máy chủ trung chuyển, máy chủ báo hiệu, hay bất kỳ backend nào
khác. Phần mềm không có phương tiện kỹ thuật nào để gửi dữ liệu về nhà phát triển.

### 3.2 Dữ liệu chúng tôi KHÔNG xử lý

Phần mềm không truy cập hay xử lý: tên của bạn (ngoài tên thiết bị đã mô tả ở trên,
mặc định là tên của chính máy hoặc thiết bị bạn), địa chỉ email, số điện thoại, danh bạ, vị trí,
ảnh, tệp (ngoài những gì hiển thị trên màn hình PC mà chính bạn chọn để truyền),
micro, camera, mã định danh quảng cáo, hay bất kỳ mã định danh thiết bị nào vượt quá mức
hệ điều hành cần để chạy ứng dụng.

### 3.3 Chia sẻ màn hình điện thoại hoặc máy tính bảng

Thiết bị Android và iOS vừa xem được máy khác, vừa chia sẻ được màn hình của chính nó.
Luồng này là **chỉ xem**: các gói chuột và bàn phím gửi tới đều bị bỏ đi, vì không hệ điều
hành nào trong hai hệ này cho một ứng dụng thường điều khiển máy. Thứ được thu là **toàn
bộ màn hình**, gồm cả những gì hiện lên trong lúc chia sẻ — thông báo, ứng dụng khác, ứng
dụng ngân hàng, mật khẩu bạn gõ. Trên Android, hệ thống hiện hộp thoại xin quyền quay màn
hình cho từng lần chia sẻ và một thông báo thường trực trong suốt thời gian chạy; trên
iOS, chỉ báo broadcast của hệ thống luôn hiển thị. Cả hai đều là tín hiệu của chính hệ điều
hành, và đều dùng để dừng chia sẻ bất cứ lúc nào. Giống như trên máy tính, hình ảnh đi
thẳng tới thiết bị kia của bạn, không bao giờ được lưu lại hay gửi cho chúng tôi.

### 3.4 Phạm vi của việc chia sẻ màn hình và điều khiển từ xa

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
| iOS | Quay màn hình (broadcast) | Chỉ khi bạn bắt đầu chia sẻ màn hình của thiết bị này, từ bộ chọn broadcast của hệ thống. iOS hỏi mỗi lần và hiện chỉ báo đang quay suốt thời gian đó. |
| Android | `INTERNET`, trạng thái mạng | Cần để mở kết nối UDP tới PC của bạn. Chỉ dùng cho phiên truyền hình ảnh. |
| Android | Đồng ý thu màn hình (`MediaProjection`) | Chỉ khi bạn bắt đầu chia sẻ màn hình của thiết bị này. Android hỏi mỗi lần; câu trả lời không thể ghi nhớ. |
| Android | `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_MEDIA_PROJECTION` | Giữ cho phiên chia sẻ tiếp tục chạy khi app xuống nền hoặc màn hình tắt. Android bắt buộc phải có để thu màn hình. |
| Android | `POST_NOTIFICATIONS` | Hiện thông báo thường trực mà Android bắt buộc phải có khi đang chia sẻ màn hình. Không gửi thông báo nào khác. |

Ứng dụng không yêu cầu quyền nào khác. Nếu một phiên bản sau này cần thêm quyền mới,
quyền đó sẽ được xin đúng ngữ cảnh và chính sách này sẽ được cập nhật.

## 5. Analytics, quảng cáo và bên thứ ba

- **Analytics / telemetry:** không có.
- **Báo cáo sự cố:** không có. Nhật ký chẩn đoán (`[DIAG]`) chỉ tồn tại trên chính máy
  bạn — trong cửa sổ console của app và, trên Windows, macOS và Linux, trong các tệp văn
  bản thuần (và sau đó là gzip) dưới thư mục log bạn chọn trong Cài đặt, hoặc dưới
  `~/.system-runtime/` (`%USERPROFILE%\.system-runtime` trên Windows) khi để trống. Cài đặt cũng điều
  khiển mỗi tệp được lớn tới đâu và log cũ được giữ bao lâu. Chúng không bao giờ được tải
  lên đâu cả; chúng chỉ rời khỏi thiết bị của bạn nếu chính bạn sao chép và gửi đi, và bạn
  có thể xoá các tệp đó bất cứ lúc nào.
- **Quảng cáo:** không có.
- **SDK bên thứ ba:** không có. Phần mềm chỉ được xây từ mã nguồn của chính nó (công khai
  tại trang dự án) và các framework của hệ điều hành.
- **Chợ ứng dụng:** ứng dụng được phân phối qua Apple App Store và Google Play. Apple và
  Google có thể thu thập thống kê cài đặt/sử dụng theo chính sách quyền riêng tư của
  riêng họ; việc thu thập đó nằm ngoài tầm kiểm soát của chúng tôi và chúng tôi chỉ nhận
  được những thống kê tổng hợp, ẩn danh mà các nền tảng đó hiển thị cho mọi nhà phát
  triển.
- **Tailscale hoặc các VPN khác:** nếu bạn chọn kết nối qua VPN, lưu lượng của bạn được
  xử lý theo chính sách quyền riêng tư của nhà cung cấp đó. System Runtime không yêu cầu cũng
  không đóng gói kèm VPN nào.

## 6. Bảo mật

- Lưu lượng truyền hình ảnh nằm trong chính mạng của bạn hoặc trong đường hầm VPN của
  bạn. Khi bạn dùng VPN như Tailscale, lưu lượng giữa các thiết bị được VPN đó mã hoá
  đầu-cuối (WireGuard).
- Trên mạng nội bộ thông thường, System Runtime không mã hoá thêm lưu lượng phiên trừ khi
  bạn bật *Encrypt session traffic*. Mọi host đều yêu cầu mã 4 chữ số trước khi chấp nhận
  kết nối — mã được sinh tự động cho bạn ở lần chạy đầu tiên và bạn đổi được — nhưng khi
  tắt mã hoá, mã đó đi ở dạng thô như phần còn lại của lưu lượng. Khi bật mã hoá, video,
  thao tác và clipboard của phiên được mã hoá; gói dò tìm vẫn plaintext; khoá phiên do host
  sinh được sao chép cho viewer trừ khi bật escrow. Tên thiết bị vẫn có thể đi trên đường
  plaintext và hiển thị trên host, nên đừng đặt thông tin nhạy cảm vào đó. Chỉ dùng System
  Runtime trên các mạng bạn tin tưởng, hoặc qua VPN, và đừng bao giờ phơi nó trực tiếp ra
  Internet. Mô hình mối đe doạ đầy đủ — cái gì được bảo vệ, cái gì không, và cách báo lỗ
  hổng — nằm trong
  [`SECURITY.vi.md`](https://github.com/manhpham90vn/Deskhub/blob/main/SECURITY.vi.md).
- Các mã passcode và khoá phiên lưu trong `recent-devices.txt` và `ui-settings.txt` được
  che đi bằng một khoá cố định để không đọc được ngay khi nhìn vào. Đó không phải mã hoá
  và không nhằm chống lại người vốn đã truy cập được tài khoản người dùng của bạn.
- Vì chúng tôi không giữ dữ liệu nào về bạn, nên không có cơ sở dữ liệu nào phía nhà phát
  triển để mà bị rò rỉ.

## 7. Lưu trữ và xoá dữ liệu

Chúng tôi không giữ gì cả, nên chúng tôi không có gì để xoá. Toàn bộ dữ liệu phiên biến
mất khi phiên kết thúc. Địa chỉ lưu trong app được xoá bằng cách xoá trắng ô nhập hoặc gỡ
cài đặt app. Danh sách thiết bị gần đây và các cài đặt đã lưu — bao gồm mọi passcode và
khoá phiên — được xoá bằng cách xoá thư mục riêng của app (`%USERPROFILE%\.system-runtime` trên Windows,
`~/.system-runtime` trên macOS và Linux), app sẽ tạo lại thư mục rỗng ở lần khởi động kế tiếp;
trên iOS và Android, gỡ cài đặt app là xoá hết.

## 8. Quyền của bạn (GDPR, CCPA và các luật tương tự)

Các đạo luật như Quy định bảo vệ dữ liệu chung của EU (GDPR) và Đạo luật quyền riêng tư
người tiêu dùng California (CCPA) trao cho bạn các quyền đối với dữ liệu cá nhân — truy
cập, chỉnh sửa, xoá, di chuyển, phản đối, và không bị phân biệt đối xử.

Vì System Runtime không thu thập hay nắm giữ bất kỳ dữ liệu cá nhân nào, nên không có dữ liệu
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
(bản tiếng Việt: https://github.com/manhpham90vn/Deskhub/blob/main/PRIVACY.vi.md;
bản tiếng Trung: https://github.com/manhpham90vn/Deskhub/blob/main/PRIVACY.zh.md)

| Phiên bản | Ngày | Thay đổi |
|---|---|---|
| 1.10 | 2026-08-14 | Mã hoá phiên tuỳ chọn: host có thể mã hoá video, thao tác và clipboard của phiên; khi dùng, khoá phiên do host sinh (sao chép/làm mới, vòng đời theo lần chia sẻ hoặc bền vững) và escrow khoá tuỳ chọn được lưu cùng tuỳ chọn chia sẻ cục bộ; thiết bị gần đây có thể nhớ kết nối có mã hoá và khoá phiên đã dùng. Dò tìm vẫn plaintext. Việc che trên đĩa nay cũng phủ khoá phiên đã lưu. |
| 1.9 | 2026-08-14 | Làm rõ tên sản phẩm hiển thị trong ứng dụng là **System Runtime**, còn dự án mã nguồn mở vẫn là **Deskhub**. Tên scheduled task khởi động cùng Windows, cách gọi thư mục log mặc định, và tên tệp log desktop (`system-runtime-*.log`) được căn theo System Runtime. Không đổi những gì được lưu hay truyền đi ngoài các tên đó. |
| 1.8 | 2026-08-14 | Cài đặt desktop có thể chọn tự bắt đầu chia sẻ khi mở app (mặc định tắt). Lựa chọn được lưu cùng các tuỳ chọn chia sẻ cục bộ khác. |
| 1.7 | 2026-08-13 | Đồng bộ clipboard nay hoạt động cả trên Android và iOS, với cùng công tắc, giới hạn 32 KiB và quy tắc chỉ-văn-bản-thuần như trên desktop. Hệ điều hành có giới hạn: thiết bị Android chỉ đọc được clipboard của chính nó khi Deskhub là ứng dụng đang ở nền trước (văn bản gửi tới thì được áp dụng bất cứ lúc nào); iOS có thể hiện hộp thoại dán của hệ thống khi Deskhub đọc một bản copy mới; thiết bị iOS đang làm host không bao giờ đồng bộ clipboard, vì broadcast của nó chạy trong một tiến trình riêng. Điện thoại và máy tính bảng cũng có thêm lựa chọn địa chỉ mạng để chia sẻ như trên desktop, lưu trong cùng tệp cài đặt cục bộ và không bao giờ được gửi đi đâu. Ngoài lựa chọn đó, không có gì mới được lưu trên bất kỳ thiết bị nào. Nhật ký phiên trên desktop ghi nối tiếp vào một tệp theo từng ngày lịch qua các lần khởi động, thay vì tạo tệp mới mỗi lần mở app. Lần khởi động vẫn ghi banner cục bộ; Share/Connect để lại dòng ghi. Tách theo kích thước, nén và xoá giữ nguyên. |
| 1.6 | 2026-08-13 | Các app desktop có thêm đồng bộ clipboard tuỳ chọn: khi công tắc bật, văn bản thuần bạn copy trong một phiên được gửi giữa các thiết bị của bạn (không mã hoá, như phần còn lại của lưu lượng, giới hạn 32 KiB) và đặt vào clipboard của máy kia; Deskhub không bao giờ lưu nó. Các cài đặt mới lưu cục bộ: địa chỉ mạng để chia sẻ, khởi động cùng hệ điều hành, tự chia sẻ khi mở app, chế độ chạy nền/khay, và chính công tắc clipboard. Bật khởi động cùng hệ điều hành sẽ tạo mục khởi động của chính nền tảng đó (tệp autostart trên Linux, scheduled task tên *System Runtime* trên Windows, Login Item trên macOS); tắt đi sẽ gỡ bỏ nó. Cài đặt desktop cũng có thể trỏ nhật ký phiên tới thư mục tuyệt đối, ghi được do người dùng chọn (để trống thì giữ thư mục System Runtime mặc định). |
| 1.5 | 2026-08-13 | Mỗi client nay có thể đặt một tên thiết bị (ô *Your name* trên trang kết nối). Tên được lưu trong `ui-settings.txt` trên chính thiết bị của bạn và được gửi tới host khi bạn kết nối — không mã hoá — để host gắn nhãn người xem này. Host chỉ giữ tên trong bộ nhớ khi còn kết nối. App desktop nay giữ nhật ký phiên cục bộ có thể cấu hình: tách theo kích thước, nén rồi xoá theo thời gian lưu giữ trong Cài đặt; trang Cài đặt có thể hiện nội dung log và mở thư mục log. Không tải lên; Android và iOS vẫn chỉ dùng luồng log của hệ điều hành. |
| 1.4 | 2026-08-12 | Tệp trạng thái trên iOS mà broadcast extension chia sẻ với app nay ghi thêm mức bộ nhớ tính bằng megabyte mà chính extension đang dùng, để màn hình chia sẻ hiển thị được con số đó. Giá trị này chỉ mô tả tiến trình broadcast của Deskhub, nằm nguyên trong app group container trên thiết bị của bạn, và bị xoá cùng phần còn lại của tệp trạng thái khi buổi phát kết thúc. |
| 1.3 | 2026-08-12 | Thiết bị Android và iOS nay chia sẻ được màn hình của chính nó ở chế độ chỉ xem, nên màn hình điện thoại hay máy tính bảng có thể phát sang một thiết bị khác của bạn. Thay đổi này thêm các quyền thu màn hình mà mỗi hệ điều hành yêu cầu (kèm foreground service và thông báo của nó trên Android) và, trên iOS, một app group container dùng chung giữa app và broadcast extension cho passcode và cổng, kèm một tệp trạng thái ngắn hạn mà extension ghi vào đó để app biết buổi phát có đang chạy hay không. Hình ảnh vẫn chỉ đi giữa các thiết bị của bạn và không bao giờ được lưu lại. |
| 1.2 | 2026-08-07 | Passcode nay là bắt buộc trên mọi host, được sinh ra ở lần chạy đầu thay vì để trống, và mọi client đều nhập được. Cài đặt chia sẻ nay được lưu trên macOS và Linux chứ không chỉ Windows, còn danh sách thiết bị gần đây được lưu trên mọi nền tảng, đều nằm trong thư mục cục bộ của chính app. Không có dữ liệu mới nào rời khỏi thiết bị của bạn. |
| 1.1 | 2026-08-05 | App Windows nay lưu dữ liệu giữa các lần chạy: danh sách 10 địa chỉ gần nhất bạn đã kết nối, cài đặt chia sẻ của bạn, và các passcode dùng với chúng. Tất cả nằm trong `%USERPROFILE%\.system-runtime` trên chính máy bạn; không có gì được truyền đi đâu cả. Ghi nhận thêm chế độ chia sẻ chỉ xem và giới hạn 5 người xem. |
| 1.0 | 2026-07-24 | Công bố lần đầu. |

## 12. Liên hệ

Mọi thắc mắc về chính sách này hoặc về quyền riêng tư trong System Runtime / Deskhub:

- **Email:** manhpv151090@gmail.com
- **Issues:** https://github.com/manhpham90vn/Deskhub/issues
