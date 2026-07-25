// =============================================================================
// HostRegistry.cpp — cài đặt phép gộp, sắp xếp và hết hạn của danh sách máy.
//
// Danh sách được giữ ĐÃ SẮP XẾP mọi lúc, không sắp lại khi đọc: chèn tìm vị trí bằng
// so sánh tuyến tính rồi insert. Với trần 32 máy thì đây là cách rẻ nhất và, quan
// trọng hơn, nó khiến hosts() trả về thứ tự đúng mà không phải cấp phát gì — giao
// diện đọc danh sách này mỗi khung hình.
//
// LIÊN QUAN: deskhub/discovery/HostRegistry.h (ba việc nó làm và vì sao)
// =============================================================================
#include "deskhub/discovery/HostRegistry.h"

namespace deskhub {

namespace {

// Thứ tự hiển thị: theo tên, rồi hostId để phá hoà (hai máy trùng tên là chuyện
// thường — "MacBook Pro" mặc định). So sánh byte thô, không theo locale: core không
// có bảng đối chiếu ngôn ngữ, và thứ tự chỉ cần ỔN ĐỊNH chứ không cần đúng từ điển
// của mọi thứ tiếng.
bool Before(const DiscoveredHost& a, const DiscoveredHost& b) {
    if (a.name != b.name) return a.name < b.name;
    return a.hostId < b.hostId;
}

using Diff = std::vector<DiscoveredHost>::difference_type;

// Chèn vào đúng chỗ để danh sách luôn ở trạng thái đã sắp xếp.
void InsertSorted(std::vector<DiscoveredHost>& v, DiscoveredHost h) {
    auto at = v.begin();
    while (at != v.end() && Before(*at, h)) ++at;
    v.insert(at, std::move(h));
}

} // namespace

uint32_t HostRegistry::BeginScan(uint64_t nowUs) {
    // probeId lấy từ đồng hồ, gấp hai nửa vào nhau để không mất bit cao. Khác 0 để
    // phân biệt với gói của bản cũ không có trường này.
    uint32_t id = uint32_t(nowUs ^ (nowUs >> 32));
    if (!id) id = 1;
    if (id == probeId_) ++id; // hai lượt quét trong cùng một micro-giây
    probeId_ = id;
    return id;
}

bool HostRegistry::OnAnnounce(const HostAnnounce& m, std::string address, uint32_t rttUs,
    uint64_t nowUs) {
    // Câu trả lời của lượt quét trước: bỏ. RTT của nó đo từ mốc DISCOVER đã cũ nên
    // sẽ ra một con số vô nghĩa, mà đó lại chính là con số hiện trên thẻ máy.
    if (m.probeId != probeId_) return false;

    const bool acceptsInput = (m.flags & kAnnounceFlagAcceptsInput) != 0;
    const bool busy = (m.flags & kAnnounceFlagBusy) != 0;

    for (size_t i = 0; i < hosts_.size(); ++i) {
        DiscoveredHost h = hosts_[i]; // chép ra: đổi tên sẽ phải dời chỗ trong vector
        if (h.hostId != m.hostId) continue;

        // Đã biết máy này. Luôn làm mới mốc thời gian, nhưng chỉ ĐỔI ĐỊA CHỈ khi
        // đường mới nhanh hơn: một máy cắm cả Wi-Fi lẫn Ethernet trả lời hai lần,
        // và ta muốn người dùng bấm vào là đi đường tốt hơn.
        const bool renamed = h.name != m.name;
        bool changed = renamed || h.sourceCount != m.sourceCount ||
                       h.acceptsInput != acceptsInput || h.busy != busy;

        h.lastSeenUs = nowUs;
        h.name = m.name;
        h.sourceCount = m.sourceCount;
        h.acceptsInput = acceptsInput;
        h.busy = busy;
        if (h.address.empty() || rttUs < h.rttUs) {
            changed = changed || h.address != address;
            h.address = std::move(address);
            h.rttUs = rttUs;
        }

        if (renamed) {
            // Tên là khoá sắp xếp → bỏ ra rồi chèn lại đúng chỗ. Hiếm (chỉ khi người
            // dùng đổi tên máy giữa hai lượt quét) nên không cần đường đi nhanh hơn.
            hosts_.erase(hosts_.begin() + Diff(i));
            InsertSorted(hosts_, std::move(h));
        } else {
            hosts_[i] = std::move(h);
        }
        return changed;
    }

    // Máy mới. Đầy thì bỏ qua — danh sách dài hơn màn hình không giúp được ai, và
    // đẩy một máy đang hiện ra để nhét máy mới vào sẽ làm danh sách nhấp nháy.
    if (hosts_.size() >= kMaxDiscoveredHosts) return false;

    DiscoveredHost h;
    h.hostId = m.hostId;
    h.name = m.name;
    h.address = std::move(address);
    h.sourceCount = m.sourceCount;
    h.acceptsInput = acceptsInput;
    h.busy = busy;
    h.rttUs = rttUs;
    h.lastSeenUs = nowUs;
    InsertSorted(hosts_, std::move(h));
    return true;
}

size_t HostRegistry::Expire(uint64_t nowUs) {
    size_t removed = 0;
    for (size_t i = hosts_.size(); i-- > 0;) {
        // Trừ theo hướng này chứ không phải `nowUs - lastSeenUs > staleUs`: nowUs
        // của caller có thể lùi một nhịp giữa hai vòng lặp (đồng hồ đơn điệu vẫn có
        // thể đọc lệch giữa các lõi), và trừ số không dấu khi đó cho ra một con số
        // khổng lồ — cả danh sách biến mất trong một khung hình.
        if (nowUs > hosts_[i].lastSeenUs && nowUs - hosts_[i].lastSeenUs > staleUs_) {
            hosts_.erase(hosts_.begin() + Diff(i));
            ++removed;
        }
    }
    return removed;
}

size_t HostRegistry::ReachableCount(uint64_t nowUs) const {
    size_t n = 0;
    for (const auto& h : hosts_) {
        if (!(nowUs > h.lastSeenUs && nowUs - h.lastSeenUs > staleUs_)) ++n;
    }
    return n;
}

} // namespace deskhub
