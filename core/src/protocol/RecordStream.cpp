#include "deskhub/protocol/RecordStream.h"

namespace deskhub {

void RecordStream::Append(std::span<const uint8_t> bytes) {
    if (failed_ || bytes.empty()) return;
    Compact();
    if (buffer_.size() + bytes.size() > kMaxRecordBacklog) {
        failed_ = true;
        buffer_.clear();
        consumed_ = 0;
        return;
    }
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

bool RecordStream::Next(std::vector<uint8_t>& out) {
    if (failed_) return false;
    const std::span<const uint8_t> pending(buffer_.data() + consumed_, buffer_.size() - consumed_);
    const RecordView record = ReadRecord(pending);
    if (record.status == RecordStatus::Invalid) {
        failed_ = true;
        buffer_.clear();
        consumed_ = 0;
        return false;
    }
    if (record.status == RecordStatus::NeedMore) return false;
    out.assign(record.message.begin(), record.message.end());
    consumed_ += record.consumed;
    if (consumed_ == buffer_.size()) {
        buffer_.clear();
        consumed_ = 0;
    }
    return true;
}

void RecordStream::Reset() {
    buffer_.clear();
    consumed_ = 0;
    failed_ = false;
}

void RecordStream::Compact() {
    if (consumed_ == 0) return;
    buffer_.erase(buffer_.begin(), buffer_.begin() + std::ptrdiff_t(consumed_));
    consumed_ = 0;
}

}
