#pragma once
#include <cstdint>
#include <functional>
#include <span>

namespace deskhub::perf {

inline constexpr double kAllocationsUnchecked = -1.0;

struct Workload {
    const char* name = "";
    const char* unit = "op";
    uint64_t unitsPerRun = 1;
    uint64_t bytesPerRun = 0;
    double maxAllocationsPerUnit = kAllocationsUnchecked;
    std::function<void()> run{};
};

struct ScalingWorkload {
    const char* name = "";
    const char* unit = "op";
    uint64_t smallUnits = 0;
    uint64_t largeUnits = 0;
    double maxTimeRatio = 0.0;
    std::function<void(uint64_t units)> run{};
};

void Begin();
void BeginGroup(const char* title);
void Measure(const Workload& workload);
void MeasureScaling(const ScalingWorkload& workload);
int Summary();

uint32_t NextRandom();
void FillRandom(std::span<uint8_t> out);
void Consume(uint64_t value);

}
