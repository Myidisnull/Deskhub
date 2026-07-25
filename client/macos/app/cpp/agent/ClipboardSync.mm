// =============================================================================
// ClipboardSync.mm — cài đặt bằng NSPasteboard + một thread hỏi vòng.
//
// VÌ SAO 300ms
//   Người dùng copy rồi chuyển sang máy kia dán — quãng đó luôn hơn một giây. 300ms
//   là không nhận ra được với người, mà vẫn rẻ (một lời gọi changeCount, không đọc
//   nội dung khi bộ đếm chưa đổi).
//
// LIÊN QUAN: agent/ClipboardSync.h (hai lớp khử vòng lặp + vì sao phải poll)
// =============================================================================
#import <AppKit/AppKit.h>

#include "agent/ClipboardSync.h"

#include <chrono>

#include "Log.h"
#include "deskhub/wire/Wire.h"

namespace {
constexpr int kPollMs = 300;
} // namespace

ClipboardSync::~ClipboardSync() {
    Stop();
}

void ClipboardSync::Start(LocalCopyHandler onLocalCopy) {
    Stop();
    onLocalCopy_ = std::move(onLocalCopy);
    quit_.store(false);
    {
        // Chụp changeCount hiện tại làm mốc: không thì lần hỏi đầu tiên coi thứ đang
        // nằm sẵn trong clipboard là "vừa copy" và bắn nó đi ngay khi vào phiên —
        // người dùng không hề copy gì cả.
        std::lock_guard<std::mutex> lk(mutex_);
        lastChangeCount_ = [[NSPasteboard generalPasteboard] changeCount];
    }
    thread_ = std::thread([this] { PollThread(); });
}

void ClipboardSync::Stop() {
    quit_.store(true);
    if (thread_.joinable()) thread_.join();
    onLocalCopy_ = nullptr;
}

void ClipboardSync::PollThread() {
    while (!quit_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        if (quit_.load()) break;

        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        const int64_t count = [pb changeCount];
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (count == lastChangeCount_) continue; // chưa ai copy gì
            lastChangeCount_ = count;
        }

        NSString* s = [pb stringForType:NSPasteboardTypeString];
        if (!s.length) continue; // ảnh/file — ngoài phạm vi (xem header)

        std::string utf8 = s.UTF8String ? s.UTF8String : "";
        if (utf8.empty() || utf8.size() > deskhub::kMaxClipboardBytes) continue;

        {
            // Lớp khử vòng lặp (b): đúng thứ ta vừa nhận từ đầu kia -> không gửi
            // ngược lại.
            std::lock_guard<std::mutex> lk(mutex_);
            if (utf8 == lastRemoteText_) continue;
        }

        if (onLocalCopy_) onLocalCopy_(utf8);
    }
}

void ClipboardSync::SetRemoteText(const std::string& utf8) {
    if (utf8.empty()) return;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (utf8 == lastRemoteText_) return; // đã đặt rồi, khỏi làm changeCount nhảy
        lastRemoteText_ = utf8;
    }

    NSString* s = [NSString stringWithUTF8String:utf8.c_str()];
    if (!s) return;

    // NSPasteboard phải chạm từ main thread. dispatch_sync để cập nhật
    // lastChangeCount_ NGAY sau khi ghi — lớp khử vòng lặp (a). Làm async thì thread
    // hỏi vòng có thể chen vào giữa lúc ghi và lúc ghi nhận bộ đếm, rồi bắn ngược
    // đúng văn bản vừa nhận.
    dispatch_block_t setIt = ^{
      NSPasteboard* pb = [NSPasteboard generalPasteboard];
      [pb clearContents];
      [pb setString:s forType:NSPasteboardTypeString];
      std::lock_guard<std::mutex> lk(mutex_);
      lastChangeCount_ = [pb changeCount];
    };
    if ([NSThread isMainThread])
        setIt();
    else
        dispatch_sync(dispatch_get_main_queue(), setIt);
}
