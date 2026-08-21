#include "deskhub/ui/TransferView.h"

#include "deskhub/ui/Strings.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace deskhub::ui {

bool TransferView::Idle() const {
    return !active && !done && !failed;
}

double TransferView::Fraction() const {
    if (total == 0) return active ? 0.0 : 1.0;
    return std::min(1.0, double(bytes) / double(total));
}

int TransferView::Percent() const {
    return int(std::lround(Fraction() * 100.0));
}

std::string TransferView::Step() const {
    if (fileCount == 0) return {};
    const uint16_t shown = std::min(uint16_t(fileIndex + 1), fileCount);
    return std::to_string(shown) + "/" + std::to_string(fileCount);
}

std::string TransferView::StatusLine() const {
    if (!active) return message;
    if (!name.empty()) return name;
    if (!message.empty()) return message;
    return kTransferSending;
}

TransferView TransferViewOf(FileSenderState state, TransferReason reason,
    const TransferProgress& progress) {
    TransferView view;
    view.active = state == FileSenderState::Offering || state == FileSenderState::Sending;
    view.done = state == FileSenderState::Done;
    view.failed = state == FileSenderState::Failed || state == FileSenderState::Refused;
    view.fileIndex = progress.fileIndex;
    view.fileCount = progress.fileCount;
    view.bytes = progress.batchBytes;
    view.total = progress.batchSize;
    view.name = progress.name;
    if (view.done || view.failed) view.message = TransferReasonText(reason);
    return view;
}

std::string FileSizeText(uint64_t bytes) {
    constexpr std::array<const char*, 5> kUnits{"B", "KB", "MB", "GB", "TB"};
    constexpr uint64_t kStep = 1000;

    size_t unit = 0;
    uint64_t whole = bytes;
    uint64_t remainder = 0;
    while (whole >= kStep && unit + 1 < kUnits.size()) {
        remainder = whole % kStep;
        whole /= kStep;
        ++unit;
    }
    std::string out = std::to_string(whole);
    if (unit > 0 && whole < 10) out += "." + std::to_string(remainder / 100);
    return out + " " + kUnits[unit];
}

}
