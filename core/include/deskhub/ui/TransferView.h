#pragma once
#include "deskhub/session/FileSender.h"
#include "deskhub/session/FileTransfer.h"

#include <cstdint>
#include <string>

namespace deskhub::ui {

struct TransferView {
    bool active = false;
    bool done = false;
    bool failed = false;
    uint16_t fileIndex = 0;
    uint16_t fileCount = 0;
    uint64_t bytes = 0;
    uint64_t total = 0;
    std::string name{};
    std::string message{};

    bool Idle() const;
    double Fraction() const;
    int Percent() const;
    std::string Step() const;
    std::string StatusLine() const;

    bool operator==(const TransferView&) const = default;
};

TransferView TransferViewOf(FileSenderState state, TransferReason reason,
    const TransferProgress& progress);

std::string FileSizeText(uint64_t bytes);

}
