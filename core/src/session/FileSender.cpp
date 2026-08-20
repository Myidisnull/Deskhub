#include "deskhub/session/FileSender.h"

#include "deskhub/transfer/SafeName.h"

#include <utility>

namespace deskhub {

bool FileSender::Offer(uint32_t batchId, std::vector<TransferFile> files) {
    if (Busy()) return false;
    if (files.empty() || files.size() > kMaxTransferFiles) return false;

    uint64_t total = 0;
    for (TransferFile& f : files) {
        f.name = SafeFileName(f.name);
        if (f.name.empty() || f.size > kMaxTransferFileBytes) return false;
        total += f.size;
        if (total > kMaxTransferBatchBytes) return false;
    }

    batchId_ = batchId;
    files_ = std::move(files);
    batchSize_ = total;
    batchSent_ = 0;
    index_ = 0;
    sent_ = 0;
    crc_.Reset();
    acked_ = 0;
    reason_ = TransferReason::Accepted;

    FileOffer offer;
    offer.batchId = batchId_;
    offer.files = files_;
    const size_t written = BuildFileOffer(buf_, offer);
    if (!written) {
        state_ = FileSenderState::Idle;
        return false;
    }
    state_ = FileSenderState::Offering;
    if (!Emit(written)) {
        state_ = FileSenderState::Idle;
        return false;
    }
    return true;
}

void FileSender::HandleMessage(std::span<const uint8_t> message) {
    const auto header = ParseCommonHeader(message);
    if (!header || header->chan != Chan::File) return;
    const std::span<const uint8_t> payload = PayloadOf(message);

    switch (header->type) {
        case MsgType::FileAccept: {
            if (state_ != FileSenderState::Offering) return;
            const auto accept = ParseFileAccept(payload);
            if (!accept || accept->batchId != batchId_) return;
            if (accept->reason != TransferReason::Accepted) {
                Finish(FileSenderState::Refused, accept->reason);
                return;
            }
            state_ = FileSenderState::Sending;
            Report();
            return;
        }
        case MsgType::FileAck: {
            if (state_ != FileSenderState::Sending) return;
            const auto ack = ParseFileAck(payload);
            if (!ack || ack->batchId != batchId_) return;
            if (ack->reason != TransferReason::Accepted) {
                Finish(FileSenderState::Failed, ack->reason);
                return;
            }
            if (size_t(ack->fileIndex) != acked_) {
                Abort(TransferReason::Corrupt);
                return;
            }
            ++acked_;
            if (acked_ >= files_.size()) Finish(FileSenderState::Done, TransferReason::Accepted);
            return;
        }
        case MsgType::FileCancel: {
            const auto cancel = ParseFileCancel(payload);
            if (!cancel || cancel->batchId != batchId_) return;
            if (!Busy()) return;
            Finish(state_ == FileSenderState::Offering ? FileSenderState::Refused
                                                       : FileSenderState::Failed,
                cancel->reason);
            return;
        }
        default: return;
    }
}

size_t FileSender::Pump(size_t maxChunks) {
    size_t sent = 0;
    while (sent < maxChunks && state_ == FileSenderState::Sending) {
        if (!SendOneChunk()) break;
        ++sent;
    }
    return sent;
}

bool FileSender::SendOneChunk() {
    if (index_ >= files_.size()) return false;
    const TransferFile& file = files_[index_];

    if (sent_ >= file.size) return FinishFile();

    const uint64_t left = file.size - sent_;
    const size_t want = size_t(left < kMaxFileChunkBytes ? left : kMaxFileChunkBytes);
    const size_t got = cb_.read ? cb_.read(index_, sent_, std::span<uint8_t>(chunk_).first(want))
                                : 0;
    if (got == 0 || got > want) {
        Abort(TransferReason::ReadFailed);
        return false;
    }

    const std::span<const uint8_t> data(chunk_.data(), got);
    const size_t written = BuildFileChunk(buf_, batchId_, index_, sent_, data);
    if (!written) {
        Abort(TransferReason::ReadFailed);
        return false;
    }
    if (!Emit(written)) return false;

    crc_.Update(data);
    sent_ += got;
    batchSent_ += got;
    Report();

    if (sent_ >= file.size) return FinishFile();
    return true;
}

bool FileSender::FinishFile() {
    FileDone done;
    done.batchId = batchId_;
    done.fileIndex = index_;
    done.crc32 = crc_.Value();
    const size_t written = BuildFileDone(buf_, done);
    if (!written) {
        Abort(TransferReason::ReadFailed);
        return false;
    }
    if (!Emit(written)) return false;

    crc_.Reset();
    sent_ = 0;
    ++index_;
    return true;
}

void FileSender::Cancel() {
    if (!Busy()) return;
    Abort(TransferReason::Cancelled);
}

void FileSender::LinkLost() {
    if (!Busy()) return;
    Finish(FileSenderState::Failed, TransferReason::LinkLost);
}

void FileSender::Abort(TransferReason reason) {
    FileCancel cancel;
    cancel.batchId = batchId_;
    cancel.reason = reason;
    const size_t written = BuildFileCancel(buf_, cancel);
    if (written) Emit(written);
    Finish(FileSenderState::Failed, reason);
}

void FileSender::Finish(FileSenderState state, TransferReason reason) {
    state_ = state;
    reason_ = reason;
    if (cb_.onFinished) cb_.onFinished(state_, reason_);
}

TransferProgress FileSender::Progress() const {
    TransferProgress p;
    p.batchId = batchId_;
    p.fileIndex = index_;
    p.fileCount = uint16_t(files_.size());
    p.fileBytes = sent_;
    p.batchBytes = batchSent_;
    p.batchSize = batchSize_;
    if (index_ < files_.size()) {
        p.fileSize = files_[index_].size;
        p.name = files_[index_].name;
    }
    return p;
}

void FileSender::Report() {
    if (cb_.onProgress) cb_.onProgress(Progress());
}

bool FileSender::Emit(size_t written) {
    return cb_.send && cb_.send(std::span<const uint8_t>(buf_.data(), written));
}

}
