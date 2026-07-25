#pragma once
// =============================================================================
// HostIdent.h — máy này tự giới thiệu là ai trên mạng.
//
// NHIỆM VỤ
//   Cấp hai thứ mà ANNOUNCE cần: một SỐ HIỆU ổn định theo máy và một CÁI TÊN người
//   đọc được. Cả hai được dùng ở hai đầu đối nghịch của cùng một tính năng — host
//   nhét vào ANNOUNCE (deskhub::Beacon), còn client dùng để LOẠI chính mình ra khỏi
//   kết quả quét (nếu không, máy nào cũng thấy chính nó trong danh sách "tìm thấy
//   trong mạng" và bấm vào là tự kết nối tới bản thân).
//
// VÌ SAO hostId PHẢI ỔN ĐỊNH QUA CÁC LẦN CHẠY
//   Client gộp các ANNOUNCE theo hostId (deskhub::HostRegistry) và giao diện lưu máy
//   đã dùng theo số này. Sinh ngẫu nhiên mỗi lần khởi động thì mỗi lần mở app lại
//   thành một máy mới, danh sách "đã dùng gần đây" chỉ dài thêm mà không bao giờ
//   khớp lại. Nên nó phải suy ra từ thứ gắn với MÁY, không phải với tiến trình.
//
//   Nguồn: MachineGuid trong registry (Windows sinh lúc cài, không đổi), băm lại còn
//   32 bit. Không đọc được (registry bị siết) thì lùi về băm tên máy — kém ổn định
//   hơn (đổi tên máy là đổi id) nhưng vẫn tốt hơn số ngẫu nhiên.
//
// KHÔNG PHẢI ĐỊNH DANH BẢO MẬT
//   hostId chỉ để GỘP dòng trong danh sách. Nó không xác thực gì cả — ai cũng gửi
//   ANNOUNCE với hostId bất kỳ được. Bảo mật phiên là chuyện của mã hoá (chưa có ở
//   v1), đừng bao giờ dùng số này làm cơ sở tin cậy.
//
// LIÊN QUAN: deskhub::Beacon (host trả lời), net/Discovery.h (client quét),
//            docs/04-protocol.md §3d
// =============================================================================
#include <cstdint>
#include <string>

// Số hiệu ổn định theo máy. Tính một lần rồi nhớ (thread-safe, gọi lúc nào cũng được).
uint32_t LocalHostId();

// Tên máy để hiển thị, UTF-8 (tên NetBIOS của Windows, ví dụ "GAMING-PC").
const std::string& LocalHostName();
