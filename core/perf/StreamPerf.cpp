#include "Perf.h"
#include "PerfHarness.h"

#include "deskhub/protocol/RecordStream.h"
#include "deskhub/protocol/Wire.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using namespace deskhub;
using namespace deskhub::perf;

namespace {

constexpr size_t kTerminalRecordBytes = 512;
constexpr size_t kBulkRecordBytes = kMaxRecordSize;
constexpr size_t kNetworkReadBytes = 1200;
constexpr size_t kBurstBytes = 64 * 1024;
constexpr size_t kScalingWireBytes = 256 * 1024;
constexpr size_t kBytesPerKilobyte = 1024;

std::vector<uint8_t> BuildWire(size_t recordBytes, size_t totalBytes, size_t& records) {
    std::vector<uint8_t> message(recordBytes);
    FillRandom(message);
    std::vector<uint8_t> record(kRecordPrefixSize + recordBytes);
    record.resize(BuildRecord(record, message));
    std::vector<uint8_t> wire;
    records = 0;
    while (wire.size() < totalBytes) {
        wire.insert(wire.end(), record.begin(), record.end());
        ++records;
    }
    return wire;
}

size_t DrainInReads(RecordStream& stream, std::span<const uint8_t> wire,
    std::vector<uint8_t>& record) {
    size_t drained = 0;
    for (size_t offset = 0; offset < wire.size(); offset += kNetworkReadBytes) {
        const size_t take = std::min(kNetworkReadBytes, wire.size() - offset);
        stream.Append(wire.subspan(offset, take));
        while (stream.Next(record)) ++drained;
    }
    return drained;
}

}

void RunStreamPerf() {
    BeginGroup("record stream (terminal + transfer framing)");

    size_t terminalRecords = 0;
    const std::vector<uint8_t> terminalWire =
        BuildWire(kTerminalRecordBytes, kBurstBytes, terminalRecords);
    RecordStream terminalStream;
    std::vector<uint8_t> terminalRecord;
    Measure(Workload{"stream/terminal-records-64k", "record", terminalRecords,
        terminalWire.size(), 0.1,
        [&] { Consume(DrainInReads(terminalStream, terminalWire, terminalRecord)); }});

    size_t bulkRecords = 0;
    const std::vector<uint8_t> bulkWire = BuildWire(kBulkRecordBytes, kBurstBytes, bulkRecords);
    RecordStream bulkStream;
    std::vector<uint8_t> bulkRecord;
    Measure(Workload{"stream/bulk-records-64k", "record", bulkRecords, bulkWire.size(), 0.1,
        [&] { Consume(DrainInReads(bulkStream, bulkWire, bulkRecord)); }});

    size_t scalingRecords = 0;
    const std::vector<uint8_t> scalingWire =
        BuildWire(kTerminalRecordBytes, kScalingWireBytes, scalingRecords);
    RecordStream scalingStream;
    std::vector<uint8_t> scalingRecord;
    MeasureScaling(ScalingWorkload{"stream/record-drain-scaling", "KB", 64, 256, 7.0,
        [&](uint64_t units) {
            const size_t bytes = std::min(size_t(units) * kBytesPerKilobyte, scalingWire.size());
            Consume(DrainInReads(scalingStream,
                std::span<const uint8_t>(scalingWire.data(), bytes), scalingRecord));
            scalingStream.Reset();
        }});
}
