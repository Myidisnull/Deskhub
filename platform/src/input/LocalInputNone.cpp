#include "deskhubp/input/LocalInput.h"

struct LocalInputMonitor::Impl {};

LocalInputMonitor::LocalInputMonitor() : impl_(std::make_unique<Impl>()) {}

LocalInputMonitor::~LocalInputMonitor() = default;

void LocalInputMonitor::Start() {}

void LocalInputMonitor::Stop() {}

uint64_t LocalInputMonitor::lastLocalUs() const {
    return 0;
}
