#pragma once
// =============================================================================
// HostRegistry.h — gom ANNOUNCE thành một danh sách máy ỔN ĐỊNH, phía client.
//
// NHIỆM VỤ
//   Đầu vào là một dòng ANNOUNCE lộn xộn: mỗi lượt quét phát vài lần, mỗi host trả
//   lời một lần cho MỖI card mạng nó có, và câu trả lời của lượt quét trước còn lết
//   về sau khi người dùng đã bấm quét lại. Đầu ra phải là thứ vẽ thẳng lên màn hình
//   được: mỗi máy đúng MỘT dòng, thứ tự không nhảy, và biết máy nào vừa biến mất.
//
// BA VIỆC NÓ LÀM, THEO ĐÚNG THỨ TỰ QUAN TRỌNG
//   1. GỘP THEO MÁY, KHÔNG THEO ĐỊA CHỈ. Khoá là hostId. Một máy cắm cả Wi-Fi lẫn
//      Ethernet trả lời hai lần từ hai IP — hai dòng cho cùng một máy là sai, và bắt
//      người dùng đoán nên bấm dòng nào lại càng sai. Giữ địa chỉ có RTT thấp hơn.
//   2. THỨ TỰ CỐ ĐỊNH. Sắp theo tên rồi tới hostId, không theo thứ tự gói về. Danh
//      sách xếp theo thứ tự đến sẽ tự đảo chỗ giữa lúc người dùng đang đưa tay bấm.
//   3. HẾT HẠN. Máy im lặng quá `staleUs` bị bỏ. Không có nó thì danh sách chỉ dài
//      thêm và "2 machines reachable" biến thành lời nói dối ngay khi rút mạng.
//
// VÌ SAO Ở CORE
//   Sáu client đều cần đúng logic này, và nó thuần số học — thời gian bơm từ ngoài
//   (`nowUs`), địa chỉ do caller dịch từ sockaddr rồi truyền vào dạng chuỗi. Không
//   socket, không đồng hồ, test được offline.
//
// KHÔNG PHẢI VIỆC CỦA LỚP NÀY
//   Phát DISCOVER (cần socket broadcast) và dịch sockaddr → chuỗi. Caller lo, vì
//   mỗi nền tảng làm hai việc đó một kiểu.
//
// LIÊN QUAN: deskhub/discovery/Beacon.h (đầu kia — host trả ANNOUNCE),
//            deskhub/wire/Wire.h (BuildDiscover/ParseAnnounce), docs/04-protocol.md §3d
// =============================================================================
#include "deskhub/wire/Wire.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace deskhub {

// Trần danh sách. Mạng văn phòng có thể có rất nhiều máy chạy Deskhub; danh sách dài
// hơn màn hình là vô dụng, mà mỗi mục còn tốn một chuỗi tên.
inline constexpr size_t kMaxDiscoveredHosts = 32;

// Bao lâu không nghe thấy gì thì coi như máy đã đi. 6 giây = ba lượt quét 2 giây;
// một lượt mất gói không được phép làm máy nhấp nháy trong danh sách.
inline constexpr uint64_t kHostStaleUs = 6'000'000;

struct DiscoveredHost {
    uint32_t hostId = 0;
    std::string name;    // tên máy để hiển thị
    std::string address; // "ip:port" — thứ đưa thẳng vào ô địa chỉ khi người dùng bấm
    uint8_t sourceCount = 0;
    bool acceptsInput = true;
    bool busy = false;
    uint32_t rttUs = 0;      // đo từ lượt quét gần nhất
    uint64_t lastSeenUs = 0; // mốc hết hạn
};

class HostRegistry {
public:
    explicit HostRegistry(uint64_t staleUs = kHostStaleUs) : staleUs_(staleUs) {}

    // Bắt đầu một lượt quét: trả về probeId để đưa vào BuildDiscover. ANNOUNCE mang
    // probeId khác lượt hiện tại sẽ bị OnAnnounce bỏ — đó là câu trả lời đến muộn của
    // lượt trước, và RTT tính từ nó sẽ là một con số bịa.
    uint32_t BeginScan(uint64_t nowUs);

    // Một ANNOUNCE vừa về. `address` là "ip:port" caller dựng từ địa chỉ NGUỒN của
    // datagram và `m.port` (cổng nguồn của gói trả lời không nhất thiết là cổng host
    // đang nghe). `rttUs` = nowUs - thời điểm phát DISCOVER của lượt này.
    //
    // Trả true nếu danh sách ĐỔI (máy mới, hoặc thuộc tính hiển thị đổi) — caller chỉ
    // vẽ lại giao diện khi cần, thay vì mỗi gói một lần.
    bool OnAnnounce(const HostAnnounce& m, std::string address, uint32_t rttUs,
        uint64_t nowUs);

    // Bỏ những máy im lặng quá staleUs. Trả số máy đã bỏ. Caller gọi cùng nhịp với
    // vòng lặp giao diện của nó.
    size_t Expire(uint64_t nowUs);

    void Clear() {
        hosts_.clear();
    }

    std::span<const DiscoveredHost> hosts() const {
        return hosts_;
    }

    // Con số cho dòng "N machines reachable". Đếm máy còn hạn — bằng hosts().size()
    // ngay sau Expire(), nhưng gọi được mà không cần Expire() trước.
    size_t ReachableCount(uint64_t nowUs) const;

    uint32_t currentProbeId() const {
        return probeId_;
    }

private:
    std::vector<DiscoveredHost> hosts_; // luôn giữ đúng thứ tự sắp xếp
    uint64_t staleUs_;
    uint32_t probeId_ = 0;
};

} // namespace deskhub
