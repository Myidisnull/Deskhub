#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace deskhub::term {

inline constexpr size_t kMaxVtParams = 32;
inline constexpr size_t kMaxOscBytes = 4096;
inline constexpr int32_t kVtParamOmitted = -1;
inline constexpr int32_t kMaxVtParamValue = 65535;

enum class VtAction : uint8_t {
    Print = 0,
    Execute = 1,
    Csi = 2,
    Esc = 3,
    Osc = 4,
};

struct VtEvent {
    VtAction action = VtAction::Print;
    char32_t codepoint = 0;
    uint8_t final = 0;
    uint8_t prefix = 0;
    uint8_t intermediate = 0;
    uint8_t paramCount = 0;
    std::array<int32_t, kMaxVtParams> params{};
    std::string text{};

    int32_t Param(size_t index, int32_t fallback) const {
        if (index >= paramCount) return fallback;
        return params[index] == kVtParamOmitted ? fallback : params[index];
    }
};

class VtParser {
public:
    void Parse(std::span<const uint8_t> bytes, std::vector<VtEvent>& out);
    void Reset();

private:
    enum class State : uint8_t {
        Ground = 0,
        Escape = 1,
        EscapeIntermediate = 2,
        CsiEntry = 3,
        CsiParam = 4,
        CsiIntermediate = 5,
        CsiIgnore = 6,
        OscString = 7,
        StringIgnore = 8,
    };

    void Consume(uint8_t byte, std::vector<VtEvent>& out);
    void ConsumeGround(uint8_t byte, std::vector<VtEvent>& out);
    void StartSequence();
    void PushParamDigit(uint8_t byte);
    void PushParamSeparator();
    void EmitPrint(char32_t cp, std::vector<VtEvent>& out);
    void EmitExecute(uint8_t byte, std::vector<VtEvent>& out);
    void EmitCsi(uint8_t final, std::vector<VtEvent>& out);
    void EmitEsc(uint8_t final, std::vector<VtEvent>& out);
    void EmitOsc(std::vector<VtEvent>& out);

    State state_ = State::Ground;
    uint8_t prefix_ = 0;
    uint8_t intermediate_ = 0;
    uint8_t paramCount_ = 0;
    std::array<int32_t, kMaxVtParams> params_{};
    std::string osc_{};
    uint32_t utf8Acc_ = 0;
    uint8_t utf8Left_ = 0;
    uint32_t utf8Min_ = 0;
    bool escInString_ = false;
};

std::string EncodeUtf8(char32_t cp);

}
