[English](SPECIFICATION.md) · **Tiếng Việt**

# Deskhub — Đặc tả chức năng

Dự án mã nguồn mở là **Deskhub**; tên sản phẩm hiển thị trong ứng dụng là **System Runtime**. Tài liệu này mô tả System Runtime **làm được gì**, dưới góc nhìn của người dùng. Đây là đặc tả
sản phẩm, không phải tài liệu thiết kế: không có chi tiết cài đặt, không mô tả giao thức,
không hướng dẫn build. Những nội dung đó nằm ở [`README.vi.md`](../README.vi.md),
[`SECURITY.vi.md`](../SECURITY.vi.md) và trong mã nguồn.

Đây là bản dịch của [`SPECIFICATION.md`](SPECIFICATION.md); khi hai bản khác nhau, bản
tiếng Anh là bản chuẩn.

- **Trạng thái:** mô tả hành vi sản phẩm dự kiến. Khi bản dựng đang chạy còn lệch,
  phần triển khai sẽ được căn theo tài liệu này — đặc biệt là mô hình mã hoá phiên
  tuỳ chọn ở mục 9.
- **Đối tượng đọc:** người cần biết sản phẩm phải làm gì — người kiểm thử, người review,
  người đóng góp, nội dung mô tả trên store.

---

## 1. Tóm tắt sản phẩm

System Runtime cho phép một máy hiển thị màn hình của nó cho các máy khác trong cùng mạng, và
cho phép các máy đó điều khiển chuột và bàn phím của nó. Chỉ có một ứng dụng duy nhất:
cùng một app vừa chia sẻ màn hình, vừa xem màn hình của máy khác.

Không bắt buộc cài đặt, không có tài khoản, không đăng nhập, không có dịch vụ nền của hệ
thống, không có thành phần đám mây. Hai máy tìm thấy nhau bằng địa chỉ IP trong một mạng
mà cả hai đều truy cập được. Trên Windows và macOS, người dùng có thể chọn giữ ứng dụng
chạy ở khu vực thông báo sau khi đóng cửa sổ; đó không phải dịch vụ hệ thống.

## 2. Thuật ngữ

| Thuật ngữ | Ý nghĩa |
| --- | --- |
| **Host** | Máy đang được chia sẻ màn hình. |
| **Client** / **Viewer** | Máy đang xem host, và có thể điều khiển host. |
| **Source** (nguồn) | Một màn hình có thể chia sẻ trên host. Một host có thể chia sẻ nhiều nguồn cùng lúc. |
| **Session** (phiên) | Một viewer đang xem một nguồn. Mỗi nguồn mở trong một cửa sổ riêng. |
| **Passcode** (mã) | Mã 4 chữ số host yêu cầu cho việc dò tìm và cho vào. Luôn bắt buộc; không phải khoá mã hoá. |
| **Session key** (khoá phiên) | Bí mật dùng khi bật mã hoá phiên. Host sinh ra; viewer sao chép khi tắt escrow. |
| **Escrow** (ký thác khoá) | Khi mã hoá bật và escrow bật, host tự chuyển khoá phiên cho viewer đang kết nối để họ không phải gõ. |

Một máy có thể vừa là host vừa là client cùng lúc.

## 3. Vai trò theo nền tảng

| Nền tảng | Chia sẻ được | Xem được | Âm thanh |
| --- | :--: | :--: | :--: |
| Windows | ✅ | ✅ | ✅ |
| macOS | ✅ | ✅ | ✅ |
| Linux | ✅ | ✅ | ✅ |
| Android | ✅ chỉ xem | ✅ | ⚠️ Android 10+ |
| iOS | ✅ chỉ xem | ✅ | ⚠️ chỉ âm thanh ứng dụng |

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
| H-6 | Thông tin kết nối | Khi đang chia sẻ, ứng dụng liệt kê các địa chỉ mạng của máy và cổng mà viewer cần dùng, để đọc hoặc sao chép cho người khác. Trên desktop, mục *Chia sẻ trên mạng* (T-9) nằm ngay trên màn host cạnh danh sách này, và danh sách chỉ hiện địa chỉ của mạng đang được chọn — chọn *Mọi mạng* thì hiện tất cả. Khi đang chia sẻ (hoặc đang khởi động chia sẻ), mục chọn này bị khoá; dừng chia sẻ để đổi. |
| H-7 | Bảng phiên trực tiếp | Với mỗi màn hình đang chia sẻ, host thấy: tên màn hình, độ phân giải, số viewer, tốc độ thu hình, tốc độ gửi, băng thông đang dùng và độ trễ khứ hồi. Mỗi viewer đang kết nối hiện thành một dòng riêng dưới màn hình tương ứng, nhận diện bằng tên hiển thị kèm địa chỉ — dạng "Tên (ip:port)" — nếu viewer đã đặt tên (C-7), hoặc chỉ bằng địa chỉ nếu chưa. |
| H-8 | Ngắt một viewer | Host có thể ngắt bất kỳ viewer nào từ bảng phiên. |
| H-9 | Giới hạn viewer | Tối đa **5** viewer xem một host cùng lúc. Các kết nối thêm bị từ chối với lý do máy đang bận. |
| H-10 | Báo lỗi | Nếu không bắt đầu chia sẻ được, lý do được hiển thị cho người dùng thay vì thất bại âm thầm. |

## 5. Client — kết nối và xem máy khác

| ID | Tính năng | Mô tả |
| --- | --- | --- |
| C-1 | Kết nối theo địa chỉ | Người dùng nhập địa chỉ IP của host vào một ô và cổng UDP vào ô riêng, được điền sẵn giá trị mặc định `47777`. Dán `192.168.1.10:47777` vào ô địa chỉ vẫn hoạt động — cổng ghi rõ trong địa chỉ được ưu tiên hơn ô cổng. Nhập sai định dạng sẽ hiện gợi ý giải thích chứ không báo lỗi cụt. |
| C-2 | Nhập passcode | Người dùng nhập 4 chữ số hiển thị trên host. Mã không đúng 4 chữ số bị chặn ngay trước khi kết nối; nếu địa chỉ là máy đã từng kết nối, mã đã nhớ sẽ được dùng khi ô nhập để trống. Hộp thoại mở ra từ danh sách thiết bị cũng hiển thị cổng UDP của thiết bị, được điền sẵn và sửa được. |
| C-2a | Nhập khoá phiên | Khi host bật mã hoá và tắt escrow, viewer phải cung cấp khoá phiên của host. Giao diện kết nối coi khoá phiên là bí mật chính trong chế độ đó; passcode vẫn được kiểm nhưng không nhấn mạnh khi trao tay. Khi escrow bật, hoặc mã hoá tắt, không cần ô khoá phiên. |
| C-3 | Tuỳ chọn chỉ xem | Trước khi kết nối, viewer có thể bỏ tích *điều khiển máy từ xa* để chỉ xem mà không gửi bất kỳ thao tác nào. |
| C-4 | Chọn nguồn | Nếu host chia sẻ nhiều hơn một màn hình, viewer được hỏi muốn xem màn hình nào. Chọn nhiều thì mở nhiều cửa sổ. Nếu host chỉ chia sẻ một màn hình, cửa sổ mở ngay. |
| C-5 | Lỗi rõ ràng | Nếu không tới được host, host không chia sẻ, từ chối passcode, từ chối khoá phiên, hoặc yêu cầu mã hoá mà viewer không đáp ứng được, viewer được cho biết chính xác là trường hợp nào — kèm địa chỉ trong thông báo. Nếu máy này không mở được phiên xem (ví dụ Windows không có GPU D3D11 dùng được), đó cũng là một lỗi riêng và rõ ràng. |
| C-6 | Thông báo kết thúc | Khi một phiên kết thúc, từ phía nào cũng vậy, viewer được cho biết lý do. |
| C-7 | Tên người xem | Ô *Your name* trên trang kết nối dùng để đặt tên cho thiết bị này. Khi người dùng chưa từng đặt tên, ô được điền sẵn giá trị mặc định theo nền tảng: hostname của máy trên Windows và Linux (tên đăng nhập nếu không lấy được hostname), tên máy tính trên macOS, tên thiết bị trên iOS, và model thiết bị trên Android. Ô có thể sửa, và nội dung của ô lúc kết nối chính là thứ được lưu và gửi đi. Ô không bao giờ rơi vào trạng thái không có tên: kết nối khi ô bị xoá trắng sẽ quay về giá trị mặc định theo nền tảng ở trên — giá trị đó được điền lại vào ô, được lưu và gửi đi — nên mỗi kết nối luôn kèm theo một cái tên. Host hiển thị tên đó bên cạnh địa chỉ của máy để phân biệt các viewer với nhau. Tên được nhớ trên thiết bị, dài tối đa **64** byte, và các ký tự điều khiển bị loại bỏ. Host chạy phiên bản cũ đơn giản là không hiển thị tên. |

## 6. Tìm máy để kết nối

| ID | Tính năng | Mô tả |
| --- | --- | --- |
| D-1 | Quét mạng | Client quét mạng nội bộ để tìm các máy đang chia sẻ và liệt kê chúng, có hiển thị tiến độ trong lúc quét ("đã kiểm tra *n* trên *m* địa chỉ"). Khi quét xong mà không tìm thấy gì, người dùng được giải thích vì sao một máy có thể vắng mặt: máy chỉ xuất hiện khi đang chia sẻ. |
| D-2 | Phạm vi quét | Mỗi lần quét kiểm tra tối đa **512** địa chỉ trong mạng nội bộ. Nếu máy không có địa chỉ mạng nội bộ, người dùng được báo là không quét được. |
| D-3 | Tự quét lại | Việc quét lặp lại định kỳ, và có thể chạy lại ngay bằng *Refresh now*. |
| D-4 | Bấm để kết nối | Bấm vào một thiết bị tìm được sẽ bắt đầu kết nối tới thiết bị đó. |
| D-5 | Thiết bị gần đây | Các máy đã từng kết nối được lưu trong danh sách *Recent devices* — tối đa **10** — kèm địa chỉ, trạng thái, ping và thời điểm kết nối gần nhất. |
| D-6 | Trạng thái trực tiếp | Mỗi thiết bị gần đây hiển thị **Online**, **Offline** hoặc **Checking…** kèm độ trễ khứ hồi, tự làm mới mỗi **30 giây** và làm mới được theo yêu cầu. |
| D-7 | Nhớ bí mật | Passcode đã dùng cho một thiết bị được lưu cùng thiết bị đó. Khi lần kết nối dùng mã hoá, sự kiện đó và khoá phiên đã dùng cũng được nhớ, để lần sau có thể điền sẵn. Tất cả được lưu ở dạng che đi — đây là tiện lợi, không phải bảo vệ (xem mục 9). |
| D-8 | Xoá thiết bị | Có thể xoá một thiết bị khỏi danh sách gần đây. |
| D-9 | Kết nối lại khi có mã hoá | Mở một thiết bị gần đây lần trước kết nối có mã hoá sẽ thử lại trước với passcode và khoá phiên đã nhớ. Nếu thất bại vì khoá hoặc mã hoá, ứng dụng hiện hộp nhập khoá phiên, giải thích khoá sai hoặc host đã đổi khoá, và cho phép dán khoá mới. |

## 7. Xem một phiên

| ID | Tính năng | Mô tả |
| --- | --- | --- |
| V-1 | Vừa khung | Màn hình từ xa được co giãn vừa cửa sổ, giữ nguyên tỉ lệ; cửa sổ mở ra với kích thước theo nguồn. Trên desktop, khi hình dạng luồng thực sự thay đổi giữa phiên — host điện thoại/máy tính bảng xoay màn hình, hoặc chuyển sang màn hình có tỉ lệ khác — cửa sổ tự chỉnh lại theo hình dạng mới; thay đổi chất lượng cùng tỉ lệ thì không đụng tới cửa sổ. |
| V-2 | Phóng to và kéo | Có thể phóng to tới **5×** và kéo để di chuyển vùng nhìn. Mức phóng được hiển thị và đặt lại được bằng một thao tác. |
| V-3 | Trạng thái phiên | Cửa sổ hiển thị dòng trạng thái trực tiếp: tốc độ khung hình, băng thông, độ trễ khứ hồi và độ trễ đầu-cuối. |
| V-4 | Cửa sổ có tiêu đề | Mỗi cửa sổ xem có tiêu đề gồm tên nguồn đang xem và trạng thái hiện tại, để phân biệt được khi mở nhiều phiên. |
| V-5 | Ngắt kết nối | Viewer có thể kết thúc phiên bất cứ lúc nào. |
| V-6 | Âm thanh | Khi cả hai máy đều hỗ trợ (mục 3), viewer nghe được những gì máy đang chia sẻ đang phát, đồng bộ với hình trong khoảng một khung hình. Âm thanh đi trên kênh riêng: mất một gói chỉ mất một phần giây âm thanh và không làm hỏng hình; máy không phát gì thì hầu như không tốn băng thông. Tắt nếu viewer tắt (T-35), và host tắt thì không bao giờ gửi (T-34). |

## 8. Điều khiển máy từ xa

| ID | Tính năng | Mô tả |
| --- | --- | --- |
| I-1 | Chuột | Di chuyển, các nút trái / phải / giữa / lùi / tiến, và con lăn đều được gửi tới host. |
| I-2 | Bàn phím | Nhấn và nhả phím được gửi đi, bao gồm cả tổ hợp phím bổ trợ. |
| I-3 | Khoá con trỏ (desktop) | `F9` khoá chuột vào màn hình từ xa, phục vụ game và các phần mềm cần chuyển động chuột thô; `F9` hoặc `Esc` để nhả. Trạng thái hiện tại hiển thị trên tiêu đề cửa sổ. |
| I-4 | An toàn khi mất focus | Khi cửa sổ mất focus, con trỏ được nhả khoá và mọi phím đang giữ được thả ra, nên không có phím nào bị kẹt trên host. |
| I-5 | Trackpad cảm ứng (di động) | Trên điện thoại và máy tính bảng, khung hình hoạt động như trackpad: kéo để di chuyển con trỏ, chạm để bấm, chạm hai lần để bấm chuột phải, giữ rồi kéo để rê, kéo hai ngón theo chiều dọc để cuộn. |
| I-6 | Chế độ con trỏ / kéo (di động) | Một nút chuyển giữa điều khiển con trỏ từ xa và kéo vùng nhìn khi đang phóng to. |
| I-7 | Bàn phím ảo (di động) | Bàn phím của thiết bị có thể hiện/ẩn theo yêu cầu và gõ thẳng vào máy từ xa. |
| I-8 | Thanh phím tắt (di động) | Nút tắt cho những phím khó gõ trên bàn phím cảm ứng: `Esc`, `Tab`, `Enter`, bốn phím mũi tên, `Del`, `Ctrl+C`, `Ctrl+V`. |
| I-9 | Host luôn thắng | Thao tác của người đang ngồi trực tiếp tại máy host được ưu tiên hơn mọi viewer từ xa. |
| I-10 | Mỗi lúc một người điều khiển | Chỉ một viewer điều khiển chuột và bàn phím tại một thời điểm. Người vào sớm nhất thắng khi tranh chấp; thao tác của các viewer còn lại bị bỏ qua cho tới khi người đang điều khiển ngừng thao tác **1 giây**. |
| I-11 | Bắt buộc chỉ xem | Khi host tắt quyền điều khiển, hoặc viewer chọn chỉ xem, không thao tác nào tới được host và cửa sổ viewer ghi rõ đang ở chế độ chỉ xem. |

## 9. Kiểm soát truy cập và an toàn

| ID | Tính năng | Mô tả |
| --- | --- | --- |
| S-1 | Mã hoá phiên tuỳ chọn | Host có thể bật *Encrypt session traffic* (mặc định tắt). Khi bật, video, âm thanh, thao tác và clipboard của phiên được mã hoá đầu-cuối giữa host và viewer. Gói dò tìm mạng vẫn không mã hoá. Dành cho mạng chưa hoàn toàn tin cậy; LAN hoặc VPN đáng tin có thể để tắt. Chi tiết và rủi ro còn lại nằm ở [`SECURITY.vi.md`](../SECURITY.vi.md). |
| S-2 | Passcode bắt buộc | Mọi host đều yêu cầu passcode 4 chữ số. Mã được sinh ngẫu nhiên ở lần chạy đầu tiên; người dùng đổi được nhưng không thể để trống hay tắt đi. Passcode cho viewer vào và chặn việc dò; nó không phải khoá mã hoá phiên. |
| S-3 | Passcode chặn cả việc dò | Host sẽ không tiết lộ đang chia sẻ những gì nếu chưa có mã đúng. |
| S-4 | Khoá khi sai nhiều lần | Các lần sai passcode hoặc khoá phiên từ cùng một nguồn được đếm. Sau **5** lần thất bại trong cửa sổ **60 giây**, host từ chối thêm lần thử từ nguồn đó trong **30 giây**. |
| S-4a | Giới hạn tốc độ dò tìm | Các gói dò tìm từ cùng một nguồn bị giới hạn tốc độ để phản hồi danh sách màn hình không thể dùng làm oracle đoán passcode không giới hạn. |
| S-5 | Công tắc điều khiển | Host có thể chia sẻ với tuỳ chọn *viewer được điều khiển máy này* tắt đi, khiến mọi phiên đều là chỉ xem bất kể viewer yêu cầu gì. |
| S-6 | Đồng ý thu hình | Trên các nền tảng yêu cầu, hệ điều hành tự hiện hộp thoại xin quyền và hộp thoại chọn màn hình; System Runtime không thu hình được nếu người dùng chưa cấp quyền. |
| S-7 | Chỉ chia sẻ khi được yêu cầu | Không có gì được chia sẻ cho tới khi người dùng bấm bắt đầu, hoặc tới khi khởi động nếu **Start sharing automatically when the app launches** được bật trong Cài đặt. Dừng chia sẻ, hoặc thoát hẳn ứng dụng, sẽ kết thúc mọi phiên. Đóng cửa sổ xuống khu vực thông báo trên Windows, macOS hoặc Linux (khi tuỳ chọn đó bật) thì phiên chia sẻ đang chạy vẫn tiếp tục. |
| S-8 | Khoá phiên | Khi mã hoá bật, host hiện khoá phiên đã sinh kèm **Copy** và **Refresh**. Khoá dành để sao chép, không do người dùng tự nghĩ ra. Refresh làm mất hiệu lực khoá cũ và ngắt các viewer phụ thuộc khoá đó. |
| S-9 | Vòng đời khoá | Khi mã hoá bật, host chọn *Per share* (mặc định) hoặc *Persistent*. *Per share* sinh khoá mới mỗi lần bắt đầu chia sẻ và bỏ khi dừng. *Persistent* giữ cùng khoá qua các lần chia sẻ và khởi động cho tới khi người dùng refresh. |
| S-10 | Ký thác khoá cho viewer | Khi mã hoá bật, host có thể bật *Escrow key to viewers* (mặc định tắt; không dùng được khi mã hoá tắt). Escrow bật thì viewer đưa đúng passcode sẽ nhận khoá phiên từ host và không cần gõ. Escrow tắt thì viewer phải tự cung cấp khoá phiên (C-2a). Escrow là tiện lợi trên mạng nội bộ, không thay thế đường trao tay tin cậy khi mang khoá. |
| S-11 | Không hạ cấp plaintext | Host bật mã hoá sẽ từ chối cho vào không mã hoá. Viewer không đáp ứng được mã hoá bị từ chối với lỗi rõ ràng (C-5), không bao giờ được chấp nhận thầm bằng plaintext. |

## 10. Cài đặt

Cài đặt thuộc về từng máy, được lưu lại qua các lần khởi động, và có hiệu lực từ lần bắt
đầu chia sẻ kế tiếp. Điện thoại và máy tính bảng hiện cổng mạng (T-4) — cũng chính là
cổng mà việc quét mạng gõ vào — đồng bộ clipboard (T-17), chia sẻ/phát âm thanh (T-34–T-35),
mã passcode (T-5), mã hoá phiên tuỳ chọn và các điều khiển liên quan (T-29–T-32), cùng mạng
để chia sẻ (T-14) trên màn hình chia sẻ; mọi thứ còn lại chúng dùng giá trị mặc định dựng sẵn.

| ID | Cài đặt | Khoảng giá trị | Mặc định |
| --- | --- | --- | --- |
| T-1 | Tốc độ khung hình | 1 – 240 fps | 60 |
| T-2 | Bitrate | 1 – 1000 Mbps | 20 |
| T-3 | Chất lượng | 720p · 1080p · 1440p · Native | 1080p |
| T-4 | Cổng mạng | 1 – 65535 | 47777 |
| T-5 | Passcode | đúng 4 chữ số | sinh ngẫu nhiên ở lần chạy đầu |
| T-6 | Viewer được điều khiển máy này | bật / tắt | bật |
| T-9 | Đóng cửa sổ vẫn chạy nền | bật / tắt (Windows, macOS và Linux) | tắt cho tới khi người dùng chọn |
| T-13 | Ẩn biểu tượng khay / menu bar | bật / tắt (Windows và macOS); chỉ hiện và thao tác được khi đang bật chạy nền; tắt chạy nền sẽ bỏ chọn mục này | tắt |
| T-14 | Chia sẻ trên mạng | Mọi mạng · một trong các địa chỉ của máy | Mọi mạng |
| T-15 | Bắt đầu chia sẻ khi mở app | bật / tắt | tắt |
| T-16 | Khởi động System Runtime khi đăng nhập | bật / tắt | tắt |
| T-17 | Đồng bộ văn bản clipboard | bật / tắt | tắt |
| T-34 | Chia sẻ âm thanh thiết bị này với viewer | bật / tắt | bật |
| T-35 | Phát âm thanh của thiết bị đang xem | bật / tắt | bật |
| T-22 | Tách tệp log khi lớn hơn | 1 – 1024 MB (Windows, macOS, Linux) | 10 |
| T-23 | Nén log cũ hơn | 0 – 3650 ngày; 0 nghĩa là không bao giờ (Windows, macOS, Linux) | 7 |
| T-24 | Xoá log cũ hơn | 0 – 3650 ngày; 0 nghĩa là không bao giờ; không được sớm hơn T-23 (Windows, macOS, Linux) | 30 |
| T-25 | Thư mục log | thư mục tuyệt đối và ghi được, hoặc để trống dùng thư mục System Runtime mặc định (Windows, macOS, Linux) | trống (thư mục mặc định) |
| T-27 | Ngôn ngữ | Mặc định hệ thống · English · 简体中文 · Français · Deutsch · Русский · 日本語 · 한국어 · العربية | Mặc định hệ thống (theo hệ điều hành) |
| T-29 | Mã hoá lưu lượng phiên | bật / tắt | tắt |
| T-30 | Vòng đời khoá phiên | Per share · Persistent; chỉ hiện khi T-29 bật | Per share |
| T-31 | Ký thác khoá cho viewer | bật / tắt; chỉ hiện và thao tác được khi T-29 bật; tắt T-29 sẽ đưa mục này về tắt | tắt |
| T-32 | Khoá phiên | giá trị đã sinh kèm Copy và Refresh; chỉ hiện khi T-29 bật; không do người dùng tự nghĩ ra | sinh khi bật mã hoá, và lại theo S-8 / S-9 |

| ID | Tính năng | Mô tả |
| --- | --- | --- |
| T-7 | Chất lượng tự động | Chất lượng luồng tự điều chỉnh theo băng thông khả dụng, trong giới hạn đã cấu hình; người dùng không phải làm gì khi điều kiện mạng thay đổi. |
| T-8 | Kiểm tra giá trị | Giá trị ngoài khoảng hoặc không phải số bị từ chối và giữ nguyên giá trị cũ, thay vì được áp dụng. Thư mục log không dùng được cũng bị từ chối theo cách tương tự. |
| T-10 | Hỏi chạy nền lần đóng đầu | Trên Windows và macOS, lần đầu đóng cửa sổ chính trước khi tuỳ chọn chạy nền được ghi lại, ứng dụng hỏi có giữ chạy nền không. Mặc định chọn **Yes**. **Confirm** ghi nhận lựa chọn và áp dụng; **Close** không ghi nhận để lần sau vẫn hỏi, và lần đóng này thì thoát. |
| T-11 | Khay luôn hiện khi bật chạy nền | Khi tuỳ chọn chạy nền bật và không ẩn khay, biểu tượng khu vực thông báo / menu bar vẫn hiện kể cả lúc cửa sổ chính đang mở. Đóng cửa sổ xuống nền sẽ gỡ app khỏi taskbar / Dock; khôi phục bằng biểu tượng khay, hoặc mở lại System Runtime. Có thông báo ngắn khi biểu tượng khay đang hiện. |
| T-12 | Xác nhận thoát khi đang bận | Trên Windows và macOS, thoát hẳn trong lúc **Share** hoặc phiên **Connect** đang chạy sẽ hỏi xác nhận trước. Đóng cửa sổ xuống nền thì không hỏi. |
| T-18 | Quay về mọi mạng | Khi đã chọn một mạng cụ thể (T-14), host chỉ tiếp cận được qua địa chỉ đó. Nếu địa chỉ đó không còn tồn tại lúc bắt đầu chia sẻ, host chia sẻ trên mọi mạng và nói rõ điều đó trong dòng trạng thái chia sẻ. Địa chỉ đã lưu nhưng hiện không khả dụng vẫn được liệt kê, đánh dấu *not connected*. |
| T-19 | Tự chia sẻ khi mở app | Chỉ desktop. Khi bật T-15, mở app sẽ vào thẳng trang Host và bắt đầu chia sẻ với cài đặt đã lưu, đúng như khi người dùng bấm Share. Các quy tắc nền tảng vẫn áp dụng: Linux hiện hộp thoại chia sẻ màn hình của desktop trước (P-3), macOS vẫn yêu cầu các quyền của nó (P-2). |
| T-20 | Khởi động cùng hệ điều hành | Chỉ desktop. Khi bật T-16: Linux ghi một mục autostart vào `~/.config/autostart`; Windows đăng ký một scheduled task tên *System Runtime* khởi động app với quyền cao lúc đăng nhập, nên không hiện hộp thoại UAC; macOS đăng ký một Login Item mà người dùng cũng thấy được trong System Settings. Tắt đi sẽ gỡ bỏ đúng thứ đã tạo. Ô chọn luôn hiển thị trạng thái mà hệ điều hành báo, không chỉ là giá trị đã lưu lần cuối. |
| T-21 | Đồng bộ clipboard | Khi bật T-17, văn bản thuần copy trên một máy trong phiên sẽ xuất hiện trên các máy còn lại trong vòng vài giây, theo cả hai chiều; host chuyển tiếp bản copy của một viewer tới các viewer khác. Văn bản giới hạn 32 KiB (bản dài hơn bị cắt tại ranh giới ký tự); ảnh, file và định dạng không bao giờ được truyền. Công tắc của host quyết định cả phiên: tắt thì host bỏ qua và không bao giờ gửi dữ liệu clipboard. Mỗi máy cũng cần bật công tắc của chính nó để đọc/ghi clipboard cục bộ. Trên Android và iOS, hệ điều hành giới hạn việc này: thiết bị Android chỉ nhặt được bản copy của chính nó khi System Runtime là ứng dụng đang ở nền trước, còn văn bản gửi tới thì được áp dụng bất cứ lúc nào; viewer trên iOS có thể thấy hộp thoại dán của hệ thống khi System Runtime đọc một bản copy mới; và thiết bị iOS đang làm host hoàn toàn không tham gia, vì broadcast của nó chạy trong một process riêng không truy cập được clipboard. |
| T-36 | Âm thanh được chia sẻ là gì | Khi bật T-34, host chia sẻ những gì loa của chính nó đang phát — hỗn hợp mà mọi ứng dụng trên máy tạo ra. Không bao giờ thu microphone: System Runtime không có âm thanh hai chiều, và không xin quyền microphone trên bất kỳ nền tảng nào (quyền ghi trên Android chỉ để thu phát lại của ứng dụng khác). Viewer chỉ nhận âm thanh nếu đã xin (T-35), nên host bật T-34 cũng không gửi gì cho viewer không nghe, và cả hai công tắc có hiệu lực từ lần bắt đầu phiên kế tiếp. |
| T-26 | Chi tiết nhật ký | Trên Windows, macOS và Linux, trang Cài đặt liệt kê các tệp log cục bộ, hiện nội dung của chúng, và có thể mở thư mục log. Các tệp `.log.gz` đã nén xuất hiện trong danh sách nhưng được mở từ thư mục thay vì hiện nội tuyến. |
| T-28 | Tuỳ chọn ngôn ngữ | Trang Cài đặt có mục chọn ngôn ngữ (T-27). **Mặc định hệ thống** theo locale của hệ điều hành và ánh xạ các thẻ phổ biến như `zh-CN`, `fr-FR`, `ja` sang danh sách hỗ trợ, không nhận diện được thì dùng tiếng Anh. Lựa chọn tường minh được lưu và áp dụng khi khởi động lại; đổi khi app đang mở cập nhật chuỗi mới hiện ngay, còn nhãn đã vẽ trên cửa sổ chính có thể cần khởi động lại. |
| T-33 | Điều khiển mã hoá phiên | Khi T-29 bật, Cài đặt (và màn chia sẻ trên điện thoại/máy tính bảng) hiện khoá phiên hiện tại (T-32), vòng đời (T-30) và escrow (T-31). Tắt mã hoá sẽ ẩn các điều khiển đó và buộc escrow về tắt. Copy đưa khoá vào clipboard cục bộ; Refresh theo S-8. |

## 11. Trạng thái và chẩn đoán

| ID | Tính năng | Mô tả |
| --- | --- | --- |
| G-1 | Thống kê phía host | Số liệu theo từng màn hình và từng viewer: tốc độ thu hình, tốc độ gửi, băng thông và độ trễ khứ hồi. |
| G-2 | Thống kê phía client | Theo từng phiên: tốc độ khung hình, băng thông, độ trễ khứ hồi và độ trễ đầu-cuối. |
| G-3 | Nhật ký phiên | Trên Windows, macOS và Linux, app ghi nối tiếp vào một tệp log theo từng ngày trong thư mục log đã cấu hình (mặc định là thư mục System Runtime của người dùng), để đính kèm khi báo lỗi. Mỗi lần tiến trình khởi động ghi một banner ngắn gồm phiên bản, định danh máy, địa chỉ cục bộ và cài đặt hiện tại; Share và Connect cũng để lại một dòng. Chỉ khi tệp vượt quá kích thước đã cấu hình mới tách tệp mới (tệp đầy được lưu trữ kèm dấu thời gian). Các tệp cũ hơn được nén rồi xoá theo giá trị lưu giữ trong Cài đặt. Đổi thư mục log chỉ ảnh hưởng lần ghi mới; tệp cũ giữ nguyên vị trí. Android và iOS thay vào đó ghi chẩn đoán vào luồng log của chính hệ điều hành và không để lại tệp nào. |
| G-4 | Phiên bản và liên kết dự án | Ứng dụng hiển thị phiên bản của nó và liên kết tới trang dự án. |

## 12. Khác biệt theo nền tảng

| ID | Nền tảng | Hành vi |
| --- | --- | --- |
| P-1 | Windows | Ứng dụng xin quyền quản trị một lần lúc khởi động, đây là điều kiện để gõ được vào các cửa sổ chạy với quyền cao. Khi bắt đầu chia sẻ, ứng dụng tự thêm luật tường lửa của mình. Chỉ cho chạy một tiến trình; lần mở thứ hai hiện thông báo rồi thoát. Khi bật chạy nền, biểu tượng khu vực thông báo luôn hiện; click trái khôi phục cửa sổ, click phải có **Restore** / **Exit**. Đóng cửa sổ khi đang bật chạy nền sẽ hiện balloon ngắn rằng System Runtime vẫn chạy. |
| P-2 | macOS | Hiển thị mục **Permissions** với trạng thái cấp quyền theo thời gian thực của *Screen Recording* (cần để chia sẻ) và *Accessibility* (cần để nhận thao tác từ xa), nút xin từng quyền, và lối tắt mở System Settings. Một số phím bị macOS chặn âm thầm nếu chưa cấp Accessibility. Chỉ cho chạy một tiến trình; lần mở thứ hai hiện thông báo rồi thoát. Khi bật chạy nền, biểu tượng menu bar luôn hiện; click trái khôi phục cửa sổ, click phải có **Restore** / **Exit**. Đóng cửa sổ khi đang bật chạy nền sẽ hiện thông báo ngắn rằng System Runtime vẫn chạy. |
| P-3 | Linux | Màn hình được chọn trong hộp thoại chia sẻ màn hình của chính môi trường desktop sau khi bấm Share, chứ không chọn trong ứng dụng. Việc chia sẻ còn cần hệ thống cho phép mô phỏng thao tác nhập liệu. |
| P-4 | Android / iOS | Chia sẻ ở chế độ **chỉ xem**: thiết bị phát màn hình và lặng lẽ bỏ qua mọi gói điều khiển, vì cả hai hệ điều hành đều không cho ứng dụng bơm thao tác vào toàn hệ thống. Toàn bộ màn hình được chia sẻ như một nguồn duy nhất, nên bộ chọn màn hình, chia sẻ nhiều màn hình và dừng từng màn hình (H-1, H-2, H-3, H-5) không áp dụng. Xoay thiết bị thì luồng xoay theo: hình người xem thấy vẫn đúng chiều, và cửa sổ của họ tự chỉnh lại theo hình dạng mới (V-1). Giao diện phiên ưu tiên cảm ứng: cử chỉ trackpad, nút phóng to, thanh phím tắt, bàn phím ảo, nút đổi màn hình và **End**. |
| P-5 | Android | Muốn chia sẻ phải qua hộp thoại xin quyền quay màn hình của hệ thống, cấp cho từng lần và không nhớ được. Chia sẻ âm thanh (T-34) còn cần quyền ghi để thu phát lại của ứng dụng khác trên Android 10 trở lên; từ chối thì vẫn chia sẻ màn hình nhưng không có tiếng. Trong lúc chia sẻ luôn có một thông báo thường trực, và luồng vẫn chạy khi ứng dụng xuống nền hoặc màn hình tắt. Tắt chia sẻ từ thông báo hệ thống sẽ kết thúc phiên. |
| P-6 | iOS | Chia sẻ được khởi động từ nút **Start sharing** trong ứng dụng, nút này mở bảng broadcast của hệ thống vì iOS bắt buộc phải qua bảng đó để xác nhận mọi lần phát, và chạy trong một tiến trình broadcast riêng nên vẫn tiếp tục sau khi đóng ứng dụng. Màn hình chia sẻ báo số người xem đang kết nối — liệt kê tên của những người xem đã đặt tên (C-7) — và mức bộ nhớ hiện tại của tiến trình broadcast — iOS sẽ chấm dứt buổi phát nào dùng quá giới hạn bộ nhớ — không có bảng chi tiết từng người như H-7, và không thể ngắt riêng từng người xem (H-8). Một sự kiện hệ thống làm dừng broadcast — ví dụ cuộc gọi đến — sẽ kết thúc phiên. |

## 13. Nằm ngoài phạm vi

System Runtime **không** cung cấp, và đặc tả này không bao gồm:

- Thu microphone, âm thanh hai chiều, hay bất kỳ kênh thoại nào. Âm thanh chỉ đi một chiều,
  từ máy đang chia sẻ tới những người đang xem (V-6).
- Truyền tệp hay in từ xa.
- Đồng bộ clipboard ngoài văn bản thuần (ảnh, tệp, văn bản có định dạng).
- Bất kỳ hệ thống tài khoản, danh bạ, hiện diện hay lời mời nào.
- Dịch vụ trung chuyển, điểm hẹn hay xuyên NAT — việc tiếp cận host qua internet là trách
  nhiệm của người dùng (ví dụ bằng VPN).
- Ghi lại phiên làm việc.
- Truy cập khi không có người tại máy, wake-on-LAN, hay điều khiển nguồn điện từ xa.
- Xác thực danh tính máy ngoài passcode và khoá phiên tuỳ chọn, chứng thực thiết bị lẫn
  nhau, hay danh bạ khoá công khai.
- Quản trị nhiều người dùng, phân quyền, hay nhật ký kiểm toán.
