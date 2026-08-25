#pragma once
#include "deskhub/protocol/Wire.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace deskhub {

struct TransferProgress {
    uint32_t batchId = 0;
    uint16_t fileIndex = 0;
    uint16_t fileCount = 0;
    uint64_t fileBytes = 0;
    uint64_t fileSize = 0;
    uint64_t batchBytes = 0;
    uint64_t batchSize = 0;
    std::string name{};
};

struct TransferRecord {
    uint64_t peerPacked = 0;
    std::string endpoint{};
    std::string peerName{};
    uint32_t batchId = 0;
    uint16_t fileIndex = 0;
    uint16_t fileCount = 0;
    uint64_t batchBytes = 0;
    uint64_t batchSize = 0;
    std::string name{};
    bool live = false;
    TransferReason reason = TransferReason::Accepted;
};

struct TransferPeer {
    std::string endpoint{};
    std::string name{};
};

std::string TransferAuditLine(const TransferPeer& peer, uint32_t batchId,
    std::string_view what, std::string_view detail = {});

std::string_view TransferReasonName(TransferReason reason);

}
