# Độ trễ khi host là điện thoại — việc còn nợ

Tài liệu nội bộ, chỉ tiếng Việt, giống `docs/v5.1.0.md`. Xoá khi ba việc dưới đã xong.

- **Trạng thái:** đã đo trên máy thật ngày 19/08/2026, đã vá một phần ba.
- **Không liên quan tới audio.** Vấn đề này có sẵn trước bản 5.1.0; nó chỉ lộ ra khi
  người dùng mở YouTube trên điện thoại để thử tiếng, vì nội dung động mạnh mới đẩy
  đường ống tới giới hạn. Đã kiểm chứng bằng cách tắt hẳn audio: độ trễ vẫn leo y hệt.

---

## 1. Hiện tượng đo được

Pixel 4 chia sẻ màn hình cho Linux qua LAN, phát YouTube:

```
capture 38 fps | send 38 fps | 14409 kbps | RTT 9 ms | loss 0%
enc_lat_ms=3159 → 3229 → 3305 → 3471 → 3569     (+~75 ms mỗi giây, không bao giờ hồi)
burst_ms_max=41-65                               (gửi một khung mất 41-65 ms)
```

Người dùng mong muốn khoảng **30 ms**. Cùng đường ống đó khi nội dung tĩnh (3-5 Mbps)
thì `e2e` giữ 5-25 ms suốt nhiều phút, nên giới hạn không nằm ở mạng.

## 2. Cơ chế

`enc_lat` đo tuổi khung hình **lúc nó tới bộ gửi**, tức thời gian nó nằm chờ trong
MediaCodec. Ba giây rưỡi nghĩa là hàng đợi của encoder đang ôm ba giây rưỡi khung hình.

Mất cân bằng rất đơn giản:

- Màn hình ảo được tạo ở **60 fps** và đẩy đều đặn từng ấy khung vào Surface của encoder.
- Cả đường ống chỉ tiêu thụ được **38 fps**: mỗi khung mất 41-65 ms để gửi, trong khi
  ngân sách ở 38 fps là 26 ms.
- 22 khung dư mỗi giây xếp hàng trong MediaCodec và không bao giờ thoát ra.

Vì sao gửi chậm: nội dung động ở 672×1440 làm encoder phun ra 15 Mbps, và bộ điều khiển
bitrate **đẩy lên kịch trần 20 Mbps** vì nó chỉ nhìn thấy mất gói 0% và RTT 9 ms. Nó
không biết chính bộ gửi mới là chỗ nghẽn. Đây là bufferbloat nằm ngay trong máy gửi:
càng thấy mạng sạch, nó càng bơm, càng bơm càng dồn ứ.

## 3. Ba việc

**a) Chặn fps ở đầu vào encoder Android — ✅ đã làm.** MediaCodec nhận thêm
`max-fps-to-encoder`, nên nó bỏ khung ngay ở Surface thay vì mã hoá hết. Kéo `e2e` từ
4000 ms xuống 150-400 ms trên cùng nội dung. **Nhưng chưa đủ**, vì giá trị đang đặt bằng
đúng fps thương lượng (60) — cũng chính là tốc độ màn hình, nên trên thực tế nó không
chặn gì.

**b) Bộ điều khiển phải nghe thêm tuổi khung hình — chưa làm.** `BitrateController` hiện
chỉ nhận hai tín hiệu, **mất gói** và **RTT**, cả hai đều đến từ phía người xem. Không có
tín hiệu nào nói "chính tôi đang tụt lại". `enc_lat` là tín hiệu đó, và nó đã được đo sẵn
rồi — chỉ chưa ai đưa vào vòng điều khiển. Khung ra trễ quá ngưỡng thì hạ bitrate hoặc hạ
fps, bất kể mạng sạch đến đâu.

Việc này nằm trong `core/`, test được offline, và có lợi cho **cả năm nền tảng** chứ
không riêng Android — Windows và macOS cũng có thể rơi vào cùng cái bẫy khi GPU hoặc
đường truyền yếu hơn nội dung.

**c) Android hạ fps theo tốc độ gửi thật đo được — chưa làm.** Linux chặn khung *trước
khi* mã hoá bằng `FrameGate`; Android không có chỗ nào để chặn, vì khung đi thẳng từ
VirtualDisplay vào Surface của MediaCodec mà không qua tay C++. Cái duy nhất siết được là
`max-fps-to-encoder`, và nó là tham số lúc dựng encoder — muốn đổi thì phải dựng lại
encoder, việc mà đường quality ladder vốn đã làm khi đổi bậc chất lượng. Nối hai chuyện
đó lại là xong.

## 4. Cách kiểm chứng lại

Sửa `bitrate_mbps` trong `ui-settings.txt` của máy trên điện thoại rồi share lại cùng một
video là đủ để thấy quan hệ nhân quả — không cần build lại:

```sh
adb shell "run-as com.manhpham.deskhub sed -i s/bitrate_mbps=20/bitrate_mbps=6/ files/ui-settings.txt"
adb logcat -c
adb logcat -d -s Deskhub:V | grep -E "STREAMING|enc_lat"
```

Nếu `enc_lat` đứng yên ở vài chục ms thì khẳng định được: chỗ nghẽn là bộ gửi, không phải
mạng, và (b) là bản vá đúng.
