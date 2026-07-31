#pragma once
#include <cstdint>
#include <vector>

#include "AgentLoop.h"
#include "SessionRow.h"

struct AgentControl {
    virtual ~AgentControl() = default;

    virtual bool active() const = 0;

    virtual bool stopRequested() const = 0;

    virtual void SetRows(std::vector<SessionSourceRow> rows) = 0;

    virtual void OnBound() {}

    virtual void OnFailed(const char*) {}
};
