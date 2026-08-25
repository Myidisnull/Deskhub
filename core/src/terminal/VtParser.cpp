#include "deskhub/terminal/VtParser.h"

namespace deskhub::term {

namespace {

constexpr char32_t kReplacement = 0xFFFD;
constexpr char32_t kMaxCodepoint = 0x10FFFF;
constexpr uint8_t kEsc = 0x1B;
constexpr uint8_t kBel = 0x07;
constexpr uint8_t kCan = 0x18;
constexpr uint8_t kSub = 0x1A;
constexpr uint8_t kDel = 0x7F;

bool IsSurrogate(char32_t cp) {
    return cp >= 0xD800 && cp <= 0xDFFF;
}

bool IsParamByte(uint8_t b) {
    return b >= 0x30 && b <= 0x39;
}

bool IsSeparator(uint8_t b) {
    return b == ';' || b == ':';
}

bool IsPrivateMarker(uint8_t b) {
    return b >= 0x3C && b <= 0x3F;
}

bool IsIntermediate(uint8_t b) {
    return b >= 0x20 && b <= 0x2F;
}

bool IsCsiFinal(uint8_t b) {
    return b >= 0x40 && b <= 0x7E;
}

bool IsEscFinal(uint8_t b) {
    return b >= 0x30 && b <= 0x7E;
}

}

std::string EncodeUtf8(char32_t cp) {
    std::string out;
    if (cp > kMaxCodepoint || IsSurrogate(cp)) cp = kReplacement;
    if (cp < 0x80) {
        out.push_back(char(cp));
    } else if (cp < 0x800) {
        out.push_back(char(0xC0 | (cp >> 6)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(char(0xE0 | (cp >> 12)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(char(0xF0 | (cp >> 18)));
        out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    }
    return out;
}

void VtParser::Reset() {
    state_ = State::Ground;
    prefix_ = 0;
    intermediate_ = 0;
    paramCount_ = 0;
    params_.fill(0);
    osc_.clear();
    utf8Acc_ = 0;
    utf8Left_ = 0;
    utf8Min_ = 0;
    escInString_ = false;
}

void VtParser::Parse(std::span<const uint8_t> bytes, std::vector<VtEvent>& out) {
    for (uint8_t b : bytes) Consume(b, out);
}

void VtParser::StartSequence() {
    prefix_ = 0;
    intermediate_ = 0;
    paramCount_ = 0;
    osc_.clear();
    escInString_ = false;
}

void VtParser::PushParamDigit(uint8_t byte) {
    if (paramCount_ == 0) {
        params_[0] = 0;
        paramCount_ = 1;
    }
    int32_t& slot = params_[paramCount_ - 1];
    if (slot == kVtParamOmitted) slot = 0;
    slot = slot * 10 + (byte - '0');
    if (slot > kMaxVtParamValue) slot = kMaxVtParamValue;
}

void VtParser::PushParamSeparator() {
    if (paramCount_ == 0) {
        params_[0] = kVtParamOmitted;
        paramCount_ = 1;
    }
    if (paramCount_ < kMaxVtParams) {
        params_[paramCount_] = kVtParamOmitted;
        ++paramCount_;
    }
}

void VtParser::EmitPrint(char32_t cp, std::vector<VtEvent>& out) {
    VtEvent ev;
    ev.action = VtAction::Print;
    ev.codepoint = cp;
    out.push_back(std::move(ev));
}

void VtParser::EmitExecute(uint8_t byte, std::vector<VtEvent>& out) {
    VtEvent ev;
    ev.action = VtAction::Execute;
    ev.final = byte;
    out.push_back(std::move(ev));
}

void VtParser::EmitCsi(uint8_t final, std::vector<VtEvent>& out) {
    VtEvent ev;
    ev.action = VtAction::Csi;
    ev.final = final;
    ev.prefix = prefix_;
    ev.intermediate = intermediate_;
    ev.paramCount = paramCount_;
    ev.params = params_;
    out.push_back(std::move(ev));
}

void VtParser::EmitEsc(uint8_t final, std::vector<VtEvent>& out) {
    VtEvent ev;
    ev.action = VtAction::Esc;
    ev.final = final;
    ev.intermediate = intermediate_;
    out.push_back(std::move(ev));
}

void VtParser::EmitOsc(std::vector<VtEvent>& out) {
    VtEvent ev;
    ev.action = VtAction::Osc;
    ev.paramCount = 1;
    ev.params[0] = kVtParamOmitted;

    size_t at = 0;
    int32_t command = 0;
    while (at < osc_.size() && osc_[at] >= '0' && osc_[at] <= '9') {
        command = command * 10 + (osc_[at] - '0');
        if (command > kMaxVtParamValue) command = kMaxVtParamValue;
        ++at;
    }
    if (at > 0) ev.params[0] = command;
    if (at < osc_.size() && osc_[at] == ';') ++at;
    ev.text = osc_.substr(at);
    out.push_back(std::move(ev));
    osc_.clear();
}

void VtParser::ConsumeGround(uint8_t byte, std::vector<VtEvent>& out) {
    if (utf8Left_ > 0) {
        if ((byte & 0xC0) == 0x80) {
            utf8Acc_ = (utf8Acc_ << 6) | uint32_t(byte & 0x3F);
            if (--utf8Left_ == 0) {
                const char32_t cp = char32_t(utf8Acc_);
                const bool ok = cp >= utf8Min_ && cp <= kMaxCodepoint && !IsSurrogate(cp);
                EmitPrint(ok ? cp : kReplacement, out);
            }
            return;
        }
        utf8Left_ = 0;
        EmitPrint(kReplacement, out);
        Consume(byte, out);
        return;
    }

    if (byte < 0x80) {
        EmitPrint(char32_t(byte), out);
        return;
    }
    if (byte >= 0xC2 && byte <= 0xDF) {
        utf8Acc_ = byte & 0x1Fu;
        utf8Left_ = 1;
        utf8Min_ = 0x80;
        return;
    }
    if (byte >= 0xE0 && byte <= 0xEF) {
        utf8Acc_ = byte & 0x0Fu;
        utf8Left_ = 2;
        utf8Min_ = 0x800;
        return;
    }
    if (byte >= 0xF0 && byte <= 0xF4) {
        utf8Acc_ = byte & 0x07u;
        utf8Left_ = 3;
        utf8Min_ = 0x10000;
        return;
    }
    EmitPrint(kReplacement, out);
}

void VtParser::Consume(uint8_t byte, std::vector<VtEvent>& out) {
    switch (state_) {
        case State::Ground:
            if (byte == kEsc) {
                if (utf8Left_ > 0) {
                    utf8Left_ = 0;
                    EmitPrint(kReplacement, out);
                }
                StartSequence();
                state_ = State::Escape;
                return;
            }
            if (byte < 0x20 || byte == kDel) {
                if (utf8Left_ > 0) {
                    utf8Left_ = 0;
                    EmitPrint(kReplacement, out);
                }
                if (byte != kDel) EmitExecute(byte, out);
                return;
            }
            ConsumeGround(byte, out);
            return;

        case State::Escape:
            if (byte == kEsc) {
                StartSequence();
                return;
            }
            if (byte == '[') {
                state_ = State::CsiEntry;
                return;
            }
            if (byte == ']') {
                state_ = State::OscString;
                return;
            }
            if (byte == 'P' || byte == 'X' || byte == '^' || byte == '_') {
                state_ = State::StringIgnore;
                return;
            }
            if (IsIntermediate(byte)) {
                intermediate_ = byte;
                state_ = State::EscapeIntermediate;
                return;
            }
            if (IsEscFinal(byte)) {
                EmitEsc(byte, out);
                state_ = State::Ground;
                return;
            }
            if (byte < 0x20) EmitExecute(byte, out);
            return;

        case State::EscapeIntermediate:
            if (byte == kEsc) {
                StartSequence();
                state_ = State::Escape;
                return;
            }
            if (IsIntermediate(byte)) {
                intermediate_ = byte;
                return;
            }
            if (IsEscFinal(byte)) {
                EmitEsc(byte, out);
                state_ = State::Ground;
                return;
            }
            if (byte < 0x20) EmitExecute(byte, out);
            return;

        case State::CsiEntry:
        case State::CsiParam:
            if (IsParamByte(byte)) {
                PushParamDigit(byte);
                state_ = State::CsiParam;
                return;
            }
            if (IsSeparator(byte)) {
                PushParamSeparator();
                state_ = State::CsiParam;
                return;
            }
            if (IsPrivateMarker(byte)) {
                if (state_ == State::CsiEntry) {
                    prefix_ = byte;
                    state_ = State::CsiParam;
                } else {
                    state_ = State::CsiIgnore;
                }
                return;
            }
            if (IsIntermediate(byte)) {
                intermediate_ = byte;
                state_ = State::CsiIntermediate;
                return;
            }
            if (IsCsiFinal(byte)) {
                EmitCsi(byte, out);
                state_ = State::Ground;
                return;
            }
            if (byte == kEsc) {
                StartSequence();
                state_ = State::Escape;
                return;
            }
            if (byte < 0x20) EmitExecute(byte, out);
            return;

        case State::CsiIntermediate:
            if (IsIntermediate(byte)) {
                intermediate_ = byte;
                return;
            }
            if (IsCsiFinal(byte)) {
                EmitCsi(byte, out);
                state_ = State::Ground;
                return;
            }
            if (byte == kEsc) {
                StartSequence();
                state_ = State::Escape;
                return;
            }
            if (byte < 0x20)
                EmitExecute(byte, out);
            else
                state_ = State::CsiIgnore;
            return;

        case State::CsiIgnore:
            if (IsCsiFinal(byte)) {
                state_ = State::Ground;
                return;
            }
            if (byte == kEsc) {
                StartSequence();
                state_ = State::Escape;
                return;
            }
            if (byte < 0x20) EmitExecute(byte, out);
            return;

        case State::OscString:
        case State::StringIgnore: {
            const bool collecting = state_ == State::OscString;
            if (escInString_) {
                escInString_ = false;
                if (byte == '\\') {
                    if (collecting) EmitOsc(out);
                    state_ = State::Ground;
                    return;
                }
                if (collecting) EmitOsc(out);
                osc_.clear();
                StartSequence();
                state_ = State::Escape;
                Consume(byte, out);
                return;
            }
            if (byte == kEsc) {
                escInString_ = true;
                return;
            }
            if (byte == kBel) {
                if (collecting) EmitOsc(out);
                osc_.clear();
                state_ = State::Ground;
                return;
            }
            if (byte == kCan || byte == kSub) {
                osc_.clear();
                state_ = State::Ground;
                return;
            }
            if (collecting && osc_.size() < kMaxOscBytes) osc_.push_back(char(byte));
            return;
        }
    }
}

}
