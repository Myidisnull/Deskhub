[English](SPECIFICATION.md) · **Tiếng Việt**

# Deskhub — Đặc tả chức năng

Tài liệu này mô tả Deskhub **làm được gì**, dưới góc nhìn của người dùng. Đây là đặc tả
sản phẩm, không phải tài liệu thiết kế: không có chi tiết cài đặt, không mô tả giao thức,
không hướng dẫn build. Những nội dung đó nằm ở [`README.vi.md`](../README.vi.md),
[`SECURITY.vi.md`](../SECURITY.vi.md) và trong mã nguồn.

Đây là bản dịch của [`SPECIFICATION.md`](SPECIFICATION.md); khi hai bản khác nhau, bản
tiếng Anh là bản chuẩn.

- **Trạng thái:** mô tả hành vi của mã nguồn hiện tại.
- **Đối tượng đọc:** người cần biết sản phẩm phải làm gì — người kiểm thử, người review,
  người đóng góp, nội dung mô tả trên store.

---

## 1. Tóm tắt sản phẩm

Deskhub cho phép một máy hiển thị màn hình của nó cho các máy khác trong cùng mạng, và
cho phép các máy đó điều khiển chuột và bàn phím của nó. Chỉ có một ứng dụng duy nhất:
cùng một app vừa chia sẻ màn hình, vừa xem màn hình của máy khác.

Không bắt buộc cài đặt, không có tài khoản, không đăng nhập, không chạy nền, không có
thành phần đám mây. Hai máy tìm thấy nhau bằng địa chỉ IP trong một mạng mà cả hai đều
truy cập được.

## 2. Thuật ngữ

| Thuật ngữ | Ý nghĩa |
| --- | --- |
| **Host** | Máy đang được chia sẻ màn hình. |
| **Client** / **Viewer** | Máy đang xem host, và có thể điều khiển host. |
| **Source** (nguồn) | Một màn hình có thể chia sẻ trên host. Một host có thể chia sẻ nhiều nguồn cùng lúc. |
| **Session** (phiên) | Một viewer đang xem một nguồn. Mỗi nguồn mở trong một cửa sổ riêng. |
| **Passcode** (mã) | Mã 4 chữ số mà host yêu cầu trước khi cho viewer vào. |

Một máy có thể vừa là host vừa là client cùng lúc.

## 3. Vai trò theo nền tảng

| Nền tảng | Chia sẻ được | Xem được |
| --- | :--: | :--: |
| Windows | ✅ | ✅ |
| macOS | ✅ | ✅ |
| Linux | ✅ | ✅ |
| Android | ✅ chỉ xem | ✅ |
| iOS | ✅ chỉ xem | ✅ |

Mọi nền tảng đều có cùng bộ tính năng phía client, trừ những khác biệt nêu ở mục 12.
Điện thoại và máy tính bảng chia sẻ ở chế độ **chỉ xem**: chúng phát màn hình nhưng không
bao giờ nhận điều khiển từ xa, vì không hệ điều hành di động nào cho một ứng dụng thường
điều khiển máy.

Ứng dụng được chia thành các mục cùng tên trên mọi nền tảng: **Host**, **Client** và
**Settings**.

---

## 4. Host — chia sẻ màn hình máy này

| ID | Tính năng | Mô tả |
| --- | --- | --- |
| H-1 | Chọn màn hình | Trước khi chia sẻ, người dùng tích chọn những màn hình của máy sẽ được chia sẻ. Phải chọn ít nhất một. |
| H-2 | Chia sẻ nhiều màn hình | Có thể chia sẻ nhiều màn hình cùng lúc; mỗi màn hình thành một nguồn riêng để viewer chọn. |
| H-3 | Giới hạn nguồn | Tối đa **8** màn hình được chia sẻ cùng lúc. Nếu máy có nhiều hơn, người dùng được cảnh báo rằng chỉ 8 màn hình đầu được chia sẻ. |
| H-4 | Bật / tắt chia sẻ | Một thao tác để bắt đầu, một thao tác để dừng. Trạng thái hiện tại luôn được hiển thị (*Không chia sẻ* / *Đang bắt đầu…* / *Đang chia sẻ*). |
| H-5 | Dừng một màn hình | Có thể dừng riêng một màn hình đang chia sẻ mà không kết thúc toàn bộ phiên chia sẻ. |
| H-6 | Thông tin kết nối | Khi đang chia sẻ, ứng dụng liệt kê các địa chỉ mạng của máy và cổng mà viewer cần dùng, để đọc hoặc sao chép cho người khác. |
| H-7 | Bảng phiên trực tiếp | Với mỗi màn hình đang chia sẻ, host thấy: tên màn hình, độ phân giải, số viewer, tốc độ thu hình, tốc độ gửi, băng thông đang dùng và độ trễ khứ hồi. Mỗi viewer đang kết nối hiện thành một dòng riêng dưới màn hình tương ứng, kèm địa chỉ. |
| H-8 | Ngắt một viewer | Host có thể ngắt bất kỳ viewer nào từ bảng phiên. |
| H-9 | Giới hạn viewer | Tối đa **5** viewer xem một host cùng lúc. Các kết nối thêm bị từ chối với lý do máy đang bận. |
| H-10 | Báo lỗi | Nếu không bắt đầu chia sẻ được, lý do được hiển thị cho người dùng thay vì thất bại âm thầm. |

## 5. Client — kết nối và xem máy khác

| ID | Tính năng | Mô tả |
| --- | --- | --- |
| C-1 | Kết nối theo địa chỉ | Người dùng nhập địa chỉ IP của host vào một ô và cổng UDP vào ô riêng, được điền sẵn giá trị mặc định `47777`. Dán `192.168.1.10:47777` vào ô địa chỉ vẫn hoạt động — cổng ghi rõ trong địa chỉ được ưu tiên hơn ô cổng. Nhập sai định dạng sẽ hiện gợi ý giải thích chứ không báo lỗi cụt. |
| C-2 | Nhập passcode | Người dùng nhập 4 chữ số hiển thị trên host. Mã không đúng 4 chữ số bị chặn ngay trước khi kết nối; nếu địa chỉ là máy đã từng kết nối, mã đã nhớ sẽ được dùng khi ô nhập để trống. Hộp thoại mở ra từ danh sách thiết bị cũng hiển thị cổng UDP của thiết bị, được điền sẵn và sửa được. |
| C-3 | Tuỳ chọn chỉ xem | Trước khi kết nối, viewer có thể bỏ tích *điều khiển máy từ xa* để chỉ xem mà không gửi bất kỳ thao tác nào. |
| C-4 | Chọn nguồn | Nếu host chia sẻ nhiều hơn một màn hình, viewer được hỏi muốn xem màn hình nào. Chọn nhiều thì mở nhiều cửa sổ. Nếu host chỉ chia sẻ một màn hình, cửa sổ mở ngay. |
| C-5 | Lỗi rõ ràng | Nếu không tới được host, host không chia sẻ, hoặc passcode sai, viewer được cho biết chính xác là trường hợp nào — kèm địa chỉ trong thông báo. |
| C-6 | Thông báo kết thúc | Khi một phiên kết thúc, từ phía nào cũng vậy, viewer được cho biết lý do. |

## 6. Tìm máy để kết nối

| ID | Tính năng | Mô tả |
| --- | --- | --- |
| D-1 | Quét mạng | Client quét mạng nội bộ để tìm các máy đang chia sẻ và liệt kê chúng, có hiển thị tiến độ trong lúc quét ("đã kiểm tra *n* trên *m* địa chỉ"). Khi quét xong mà không tìm thấy gì, người dùng được giải thích vì sao một máy có thể vắng mặt: máy chỉ xuất hiện khi đang chia sẻ. |
| D-2 | Phạm vi quét | Mỗi lần quét kiểm tra tối đa **512** địa chỉ trong mạng nội bộ. Nếu máy không có địa chỉ mạng nội bộ, người dùng được báo là không quét được. |
| D-3 | Tự quét lại | Việc quét lặp lại định kỳ, và có thể chạy lại ngay bằng *Refresh now*. |
| D-4 | Bấm để kết nối | Bấm vào một thiết bị tìm được sẽ bắt đầu kết nối tới thiết bị đó. |
| D-5 | Thiết bị gần đây | Các máy đã từng kết nối được lưu trong danh sách *Recent devices* — tối đa **10** — kèm địa chỉ, trạng thái, ping và thời điểm kết nối gần nhất. |
| D-6 | Trạng thái trực tiếp | Mỗi thiết bị gần đây hiển thị **Online**, **Offline** hoặc **Checking…** kèm độ trễ khứ hồi, tự làm mới mỗi **30 giây** và làm mới được theo yêu cầu. |
| D-7 | Nhớ passcode | Passcode đã dùng cho một thiết bị được lưu cùng thiết bị đó, nên lần sau kết nối không cần gõ lại. Mã được lưu ở dạng che đi — đây là tiện lợi, không phải bảo vệ (xem mục 9). |
| D-8 | Xoá thiết bị | Có thể xoá một thiết bị khỏi danh sách gần đây. |

## 7. Xem một phiên

| ID | Tính năng | Mô tả |
| --- | --- | --- |
| V-1 | Vừa khung | Màn hình từ xa được co giãn vừa cửa sổ, giữ nguyên tỉ lệ; cửa sổ mở ra với kích thước theo nguồn. |
| V-2 | Phóng to và kéo | Có thể phóng to tới **5×** và kéo để di chuyển vùng nhìn. Mức phóng được hiển thị và đặt lại được bằng một thao tác. |
| V-3 | Trạng thái phiên | Cửa sổ hiển thị dòng trạng thái trực tiếp: tốc độ khung hình, băng thông, độ trễ khứ hồi và độ trễ đầu-cuối. |
| V-4 | Cửa sổ có tiêu đề | Mỗi cửa sổ xem có tiêu đề gồm tên nguồn đang xem và trạng thái hiện tại, để phân biệt được khi mở nhiều phiên. |
| V-5 | Ngắt kết nối | Viewer có thể kết thúc phiên bất cứ lúc nào. |

## 8. Điều khiển máy từ xa

| ID | Tính năng | Mô tả |
| --- | --- | --- |
| I-1 | Chuột | Di chuyển, các nút trái / phải / giữa / lùi / tiến, và con lăn đều được gửi tới host. |
| I-2 | Bàn phím | Nhấn và nhả phím được gửi đi, bao gồm cả tổ hợp phím bổ trợ. |
| I-3 | Khoá con trỏ (desktop) | `F9` khoá chuột vào màn hình từ xa, phục vụ game và các phần mềm cần chuyển động chuột thô; `F9` hoặc `Esc` để nhả. Trạng thái hiện tại hiển thị trên tiêu đề cửa sổ. |
| I-4 | An toàn khi mất focus | Khi cửa sổ mất focus, con trỏ được nhả khoá và mọi phím đang giữ được thả ra, nên không có phím nào bị kẹt trên host. |
| I-5 | Trackpad cảm ứng (di động) | Trên điện thoại và máy tính bảng, khung hình hoạt động như trackpad: kéo để di chuyển con trỏ, chạm để bấm, giữ rồi kéo để rê, kéo hai ngón theo chiều dọc để cuộn. |
| I-6 | Chế độ con trỏ / kéo (di động) | Một nút chuyển giữa điều khiển con trỏ từ xa và kéo vùng nhìn khi đang phóng to. |
| I-7 | Bàn phím ảo (di động) | Bàn phím của thiết bị có thể hiện/ẩn theo yêu cầu và gõ thẳng vào máy từ xa. |
| I-8 | Thanh phím tắt (di động) | Nút tắt cho những phím khó gõ trên bàn phím cảm ứng: `Esc`, `Tab`, `Enter`, bốn phím mũi tên, `Del`, `Ctrl+C`, `Ctrl+V`. |
| I-9 | Host luôn thắng | Thao tác của người đang ngồi trực tiếp tại máy host được ưu tiên hơn mọi viewer từ xa. |
| I-10 | Mỗi lúc một người điều khiển | Chỉ một viewer điều khiển chuột và bàn phím tại một thời điểm. Người vào sớm nhất thắng khi tranh chấp; thao tác của các viewer còn lại bị bỏ qua cho tới khi người đang điều khiển ngừng thao tác **1 giây**. |
| I-11 | Bắt buộc chỉ xem | Khi host tắt quyền điều khiển, hoặc viewer chọn chỉ xem, không thao tác nào tới được host và cửa sổ viewer ghi rõ đang ở chế độ chỉ xem. |

## 9. Kiểm soát truy cập và an toàn

| ID | Tính năng | Mô tả |
| --- | --- | --- |
| S-1 | Không mã hoá | Deskhub không mã hoá bất cứ thứ gì. Sản phẩm dành cho mạng tin cậy hoặc VPN. Điều này được nêu trong ứng dụng và ghi rõ trong [`SECURITY.vi.md`](../SECURITY.vi.md). |
| S-2 | Passcode bắt buộc | Mọi host đều yêu cầu passcode 4 chữ số. Mã được sinh ngẫu nhiên ở lần chạy đầu tiên; người dùng đổi được nhưng không thể để trống hay tắt đi. |
| S-3 | Passcode chặn cả việc dò | Host có yêu cầu passcode sẽ không tiết lộ đang chia sẻ những gì nếu chưa có mã đúng. |
| S-4 | Khoá khi sai nhiều lần | Sai passcode **3** lần sẽ khoá host, không nhận thêm lần thử nào trong **30 giây**. |
| S-5 | Công tắc điều khiển | Host có thể chia sẻ với tuỳ chọn *viewer được điều khiển máy này* tắt đi, khiến mọi phiên đều là chỉ xem bất kể viewer yêu cầu gì. |
| S-6 | Đồng ý thu hình | Trên các nền tảng yêu cầu, hệ điều hành tự hiện hộp thoại xin quyền và hộp thoại chọn màn hình; Deskhub không thu hình được nếu người dùng chưa cấp quyền. |
| S-7 | Chỉ chia sẻ khi được yêu cầu | Không có gì được chia sẻ cho tới khi người dùng bấm bắt đầu. Đóng ứng dụng hoặc dừng chia sẻ sẽ kết thúc mọi phiên. |

## 10. Cài đặt

Cài đặt thuộc về từng máy, được lưu lại qua các lần khởi động, và có hiệu lực từ lần bắt
đầu chia sẻ kế tiếp. Điện thoại và máy tính bảng chỉ hiện cổng mạng (T-4) — cũng chính là
cổng mà việc quét mạng gõ vào — cùng mã passcode (T-5) trên màn hình chia sẻ; mọi thứ còn
lại chúng dùng giá trị mặc định dựng sẵn.

| ID | Cài đặt | Khoảng giá trị | Mặc định |
| --- | --- | --- | --- |
| T-1 | Tốc độ khung hình | 1 – 240 fps | 60 |
| T-2 | Bitrate | 1 – 1000 Mbps | 20 |
| T-3 | Chất lượng | 720p · 1080p · 1440p · Native | 1080p |
| T-4 | Cổng mạng | 1 – 65535 | 47777 |
| T-5 | Passcode | đúng 4 chữ số | sinh ngẫu nhiên ở lần chạy đầu |
| T-6 | Viewer được điều khiển máy này | bật / tắt | bật |

| ID | Tính năng | Mô tả |
| --- | --- | --- |
| T-7 | Chất lượng tự động | Chất lượng luồng tự điều chỉnh theo băng thông khả dụng, trong giới hạn đã cấu hình; người dùng không phải làm gì khi điều kiện mạng thay đổi. |
| T-8 | Kiểm tra giá trị | Giá trị ngoài khoảng hoặc không phải số bị từ chối và giữ nguyên giá trị cũ, thay vì được áp dụng. |

## 11. Trạng thái và chẩn đoán

| ID | Tính năng | Mô tả |
| --- | --- | --- |
| G-1 | Thống kê phía host | Số liệu theo từng màn hình và từng viewer: tốc độ thu hình, tốc độ gửi, băng thông và độ trễ khứ hồi. |
| G-2 | Thống kê phía client | Theo từng phiên: tốc độ khung hình, băng thông, độ trễ khứ hồi và độ trễ đầu-cuối. |
| G-3 | Nhật ký phiên | Trên Windows, macOS và Linux, mỗi lần chạy ghi một tệp log vào thư mục Deskhub của người dùng, để đính kèm khi báo lỗi. Android và iOS thay vào đó ghi chẩn đoán vào luồng log của chính hệ điều hành và không để lại tệp nào. |
| G-4 | Phiên bản và liên kết dự án | Ứng dụng hiển thị phiên bản của nó và liên kết tới trang dự án. |

## 12. Khác biệt theo nền tảng

| ID | Nền tảng | Hành vi |
| --- | --- | --- |
| P-1 | Windows | Ứng dụng xin quyền quản trị một lần lúc khởi động, đây là điều kiện để gõ được vào các cửa sổ chạy với quyền cao. Khi bắt đầu chia sẻ, ứng dụng tự thêm luật tường lửa của mình. |
| P-2 | macOS | Hiển thị mục **Permissions** với trạng thái cấp quyền theo thời gian thực của *Screen Recording* (cần để chia sẻ) và *Accessibility* (cần để nhận thao tác từ xa), nút xin từng quyền, và lối tắt mở System Settings. Một số phím bị macOS chặn âm thầm nếu chưa cấp Accessibility. |
| P-3 | Linux | Màn hình được chọn trong hộp thoại chia sẻ màn hình của chính môi trường desktop sau khi bấm Share, chứ không chọn trong ứng dụng. Việc chia sẻ còn cần hệ thống cho phép mô phỏng thao tác nhập liệu. |
| P-4 | Android / iOS | Chia sẻ ở chế độ **chỉ xem**: thiết bị phát màn hình và lặng lẽ bỏ qua mọi gói điều khiển, vì cả hai hệ điều hành đều không cho ứng dụng bơm thao tác vào toàn hệ thống. Toàn bộ màn hình được chia sẻ như một nguồn duy nhất, nên bộ chọn màn hình, chia sẻ nhiều màn hình và dừng từng màn hình (H-1, H-2, H-3, H-5) không áp dụng. Giao diện phiên ưu tiên cảm ứng: cử chỉ trackpad, nút phóng to, thanh phím tắt, bàn phím ảo, nút đổi màn hình và **End**. |
| P-5 | Android | Muốn chia sẻ phải qua hộp thoại xin quyền quay màn hình của hệ thống, cấp cho từng lần và không nhớ được. Trong lúc chia sẻ luôn có một thông báo thường trực, và luồng vẫn chạy khi ứng dụng xuống nền hoặc màn hình tắt. Tắt chia sẻ từ thông báo hệ thống sẽ kết thúc phiên. |
| P-6 | iOS | Chia sẻ được khởi động từ nút **Start sharing** trong ứng dụng, nút này mở bảng broadcast của hệ thống vì iOS bắt buộc phải qua bảng đó để xác nhận mọi lần phát, và chạy trong một tiến trình broadcast riêng nên vẫn tiếp tục sau khi đóng ứng dụng. Màn hình chia sẻ chỉ báo số người xem đang kết nối, không có bảng chi tiết từng người như H-7, và không thể ngắt riêng từng người xem (H-8). Một sự kiện hệ thống làm dừng broadcast — ví dụ cuộc gọi đến — sẽ kết thúc phiên. |

## 13. Nằm ngoài phạm vi

Deskhub **không** cung cấp, và đặc tả này không bao gồm:

- Truyền âm thanh.
- Truyền tệp, đồng bộ clipboard, hay in từ xa.
- Bất kỳ hệ thống tài khoản, danh bạ, hiện diện hay lời mời nào.
- Dịch vụ trung chuyển, điểm hẹn hay xuyên NAT — việc tiếp cận host qua internet là trách
  nhiệm của người dùng (ví dụ bằng VPN).
- Ghi lại phiên làm việc.
- Truy cập khi không có người tại máy, wake-on-LAN, hay điều khiển nguồn điện từ xa.
- Mã hoá, xác thực danh tính máy, hay đảm bảo toàn vẹn ở tầng truyền tải.
- Quản trị nhiều người dùng, phân quyền, hay nhật ký kiểm toán.
