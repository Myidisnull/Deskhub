// =============================================================================
// StreamSizeTests.cpp — chọn độ phân giải phát (FitStreamSize): trần người dùng,
// trần màn hình client, giữ tỉ lệ, và các đường lùi an toàn.
//
// Các cỡ dùng ở đây là cỡ THẬT của thiết bị, không phải số tròn bịa ra: đây là chỗ
// duy nhất kiểm chứng được rằng một màn Retina lẻ (3024×1964) hay một điện thoại
// dọc thật sự ra khung hợp lệ, chẵn, và không lệch tỉ lệ đủ để sinh viền đen.
// =============================================================================
#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/control/StreamSize.h"

#include <cmath>
#include <cstdio>

using namespace deskhub;

namespace {

// Tỉ lệ lệch bao nhiêu phần trăm so với nguồn. Lệch tạo viền đen, nên đây là con số
// phải giữ nhỏ — làm tròn chẵn một lần thì không bao giờ quá ~0.1%.
double AspectDriftPct(uint32_t srcW, uint32_t srcH, StreamSize s) {
    const double a0 = double(srcW) / double(srcH), a1 = double(s.width) / double(s.height);
    return std::fabs(a1 - a0) / a0 * 100.0;
}

void CheckEvenAndAligned(uint32_t srcW, uint32_t srcH, StreamSize s, const char* what) {
    Check((s.width & 1u) == 0 && (s.height & 1u) == 0, what);
    Check(AspectDriftPct(srcW, srcH, s) < 0.2, "aspect ratio preserved (<0.2% drift)");
}

void TestUserCap() {
    std::printf("[size] user cap shrinks the long edge, aspect preserved...\n");
    // MacBook Pro 14" native.
    const StreamSize s = FitStreamSize(3024, 1964, 1920, 0, 0);
    Check(s.width == 1920, "long edge lands exactly on the cap");
    Check(s == StreamSize{1920, 1246}, "3024x1964 cap 1920 -> 1920x1246");
    CheckEvenAndAligned(3024, 1964, s, "capped size is even on both axes");

    // 6K XDR — cùng luật, hệ số co lớn hơn nhiều.
    const StreamSize x = FitStreamSize(6016, 3384, 1920, 0, 0);
    Check(x == StreamSize{1920, 1080}, "6016x3384 cap 1920 -> exactly 1920x1080");
}

void TestNoUpscale() {
    std::printf("[size] never upscales: source under every cap is passed through...\n");
    Check(FitStreamSize(1280, 720, 1920, 0, 0) == StreamSize{1280, 720},
        "720p source, 1920 cap -> untouched");
    Check(FitStreamSize(1280, 720, 1920, 3840, 2160) == StreamSize{1280, 720},
        "a big client does not make the host invent pixels");
    Check(FitStreamSize(1920, 1080, 0, 0, 0) == StreamSize{1920, 1080},
        "no caps at all -> native");
}

void TestPortraitSource() {
    std::printf("[size] portrait source caps its own long edge (height)...\n");
    const StreamSize s = FitStreamSize(2160, 3840, 1920, 0, 0);
    Check(s == StreamSize{1080, 1920}, "2160x3840 cap 1920 -> 1080x1920, height is the long edge");
    CheckEvenAndAligned(2160, 3840, s, "portrait result is even");
}

void TestClientCapPhone() {
    std::printf("[size] a phone's screen caps the stream below the user cap...\n");
    // iPhone 15 Pro, 1179x2556 pixel. Báo ở hướng DỌC — đúng như thiết bị gửi đi.
    // Phải tính theo hướng NGANG (2556 rộng, 1179 cao) vì người dùng xoay được.
    const StreamSize s = FitStreamSize(3024, 1964, 1920, 1179, 2556);
    Check(s.width < 1920, "client screen is the binding limit, not the 1920 user cap");
    Check(s == StreamSize{1814, 1178}, "3024x1964 into a 2556x1179 landscape rect");
    CheckEvenAndAligned(3024, 1964, s, "client-capped size is even");

    // Cùng máy báo ở hướng NGANG phải ra ĐÚNG cùng kết quả — nếu không thì xoay máy
    // giữa phiên sẽ đổi chất lượng, mà ta không có đường báo lại để sửa.
    Check(FitStreamSize(3024, 1964, 1920, 2556, 1179) == s,
        "orientation the client reports does not change the answer");

    // Máy nhỏ hơn nhiều: đây là chỗ tiết kiệm thật sự.
    const StreamSize small = FitStreamSize(3024, 1964, 1920, 750, 1334);
    Check(small == StreamSize{1154, 750}, "750x1334 phone -> 1154x750");
    CheckEvenAndAligned(3024, 1964, small, "small-phone result is even");
    // 0.87 Mpixel so với 2.39 của trần 1920 — đây mới là chỗ thương lượng trả tiền.
    Check(small.width * small.height < 1'000'000, "under 1 Mpixel");
}

void TestClientCapTabletLosesToUserCap() {
    std::printf("[size] a big tablet loses to the user cap (tighter of the two wins)...\n");
    // iPad Pro 12.9", 2048x2732. Rộng rãi hơn trần 1920 → trần người dùng thắng.
    Check(FitStreamSize(3024, 1964, 1920, 2048, 2732) == StreamSize{1920, 1246},
        "tablet bigger than the cap -> cap decides");
    // Bỏ trần người dùng thì màn hình client mới lộ ra là ràng buộc.
    const StreamSize s = FitStreamSize(3024, 1964, 0, 2048, 2732);
    Check(s.width > 1920 && s.width <= 2732, "no user cap -> the tablet's own screen bounds it");
}

void TestLegacyClientAndGarbage() {
    std::printf("[size] legacy 3840x2160 clients and nonsense sizes fall back safely...\n");
    // Client đời cũ hardcode 3840x2160 → rộng hơn trần, nên hành vi y hệt như trước
    // khi có thương lượng. Đây là điều kiện để đổi ngữ nghĩa trường mà không vỡ.
    Check(FitStreamSize(3024, 1964, 1920, 3840, 2160) == FitStreamSize(3024, 1964, 1920, 0, 0),
        "a legacy client changes nothing");
    // Nguồn rỗng.
    Check(FitStreamSize(0, 0, 1920, 0, 0) == StreamSize{}, "empty source -> empty result");
    // Trần vô lý nhỏ: thà phát native còn hơn trả cỡ không nén được.
    Check(FitStreamSize(3024, 1964, 1, 0, 0) == StreamSize{3024, 1964},
        "absurd cap falls back to native instead of returning an unencodable size");
    Check(FitStreamSize(3024, 1964, 0, 1, 1) == StreamSize{3024, 1964},
        "absurd client size falls back to native");
}

} // namespace

void RunStreamSizeTests() {
    TestUserCap();
    TestNoUpscale();
    TestPortraitSource();
    TestClientCapPhone();
    TestClientCapTabletLosesToUserCap();
    TestLegacyClientAndGarbage();
}
