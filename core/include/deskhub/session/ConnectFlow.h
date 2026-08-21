#pragma once
#include "deskhub/protocol/Wire.h"
#include "deskhub/ui/Strings.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace deskhub {

struct ConnectDecision {
    bool showPicker = false;
    uint8_t sourceId = 0;
};

inline ConnectDecision DecideAfterSourceQuery(std::span<const SourceInfo> sources) {
    ConnectDecision d;
    if (sources.size() > 1) {
        d.showPicker = true;
        return d;
    }
    if (!sources.empty()) d.sourceId = sources.front().sourceId;
    return d;
}

struct OpenChoice {
    bool desktop = false;
    bool shell = false;
    bool files = false;

    bool operator==(const OpenChoice&) const = default;
};

enum class ConnectProblem : uint8_t {
    None,
    NothingTicked,
    HostHasNoTerminal,
    HostNotTakingFiles,
    NoSourcesShared,
};

struct ConnectPlan {
    bool openShell = false;
    bool openFiles = false;
    bool openDesktop = false;
    bool showPicker = false;
    uint8_t sourceId = 0;
    ConnectProblem problem = ConnectProblem::None;
};

inline ConnectPlan PlanAfterConnect(const HostCaps& caps, std::span<const SourceInfo> sources,
    const OpenChoice& choice) {
    ConnectPlan plan;
    if (!choice.desktop && !choice.shell && !choice.files) {
        plan.problem = ConnectProblem::NothingTicked;
        return plan;
    }

    if (choice.shell) {
        plan.openShell = caps.terminal;
        if (!caps.terminal) plan.problem = ConnectProblem::HostHasNoTerminal;
    }
    if (choice.files) {
        plan.openFiles = caps.files;
        if (!caps.files) plan.problem = ConnectProblem::HostNotTakingFiles;
    }
    if (!choice.desktop) return plan;

    if (sources.empty()) {
        plan.problem = ConnectProblem::NoSourcesShared;
        return plan;
    }

    const ConnectDecision decision = DecideAfterSourceQuery(sources);
    plan.openDesktop = true;
    plan.showPicker = decision.showPicker;
    plan.sourceId = decision.sourceId;
    return plan;
}

inline std::string ConnectProblemText(ConnectProblem problem, std::string_view address) {
    switch (problem) {
        case ConnectProblem::None: return {};
        case ConnectProblem::NothingTicked: return ui::kOpenNothingTicked;
        case ConnectProblem::HostHasNoTerminal: return ui::kHostHasNoTerminal;
        case ConnectProblem::HostNotTakingFiles: return ui::kTransferHostNotTaking;
        case ConnectProblem::NoSourcesShared: return ui::SourceQueryEmpty(address);
    }
    return {};
}

}
