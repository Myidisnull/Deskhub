#include "deskhub/cli/Json.h"

namespace deskhub::cli {

namespace {

constexpr char kHexDigits[] = "0123456789abcdef";
constexpr std::string_view kReplacement = "\xEF\xBF\xBD";

void AppendEscapedUnit(std::string& out, uint8_t byte) {
    out += "\\u00";
    out += kHexDigits[(byte >> 4) & 0x0F];
    out += kHexDigits[byte & 0x0F];
}

bool SecondByteFits(uint8_t lead, uint8_t second) {
    if (lead == 0xE0) return second >= 0xA0 && second <= 0xBF;
    if (lead == 0xED) return second >= 0x80 && second <= 0x9F;
    if (lead == 0xF0) return second >= 0x90 && second <= 0xBF;
    if (lead == 0xF4) return second >= 0x80 && second <= 0x8F;
    return second >= 0x80 && second <= 0xBF;
}

size_t SequenceLength(std::string_view text, size_t index) {
    const uint8_t lead = uint8_t(text[index]);
    if (lead < 0x80) return 1;

    size_t length = 0;
    if (lead >= 0xC2 && lead <= 0xDF) length = 2;
    if (lead >= 0xE0 && lead <= 0xEF) length = 3;
    if (lead >= 0xF0 && lead <= 0xF4) length = 4;
    if (length == 0 || index + length > text.size()) return 0;

    if (!SecondByteFits(lead, uint8_t(text[index + 1]))) return 0;
    for (size_t i = 2; i < length; ++i)
        if ((uint8_t(text[index + i]) & 0xC0) != 0x80) return 0;
    return length;
}

}

std::string JsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    size_t i = 0;
    while (i < text.size()) {
        const uint8_t byte = uint8_t(text[i]);
        if (byte == '"' || byte == '\\') {
            out += '\\';
            out += char(byte);
            ++i;
            continue;
        }
        if (byte == '\n') {
            out += "\\n";
            ++i;
            continue;
        }
        if (byte == '\r') {
            out += "\\r";
            ++i;
            continue;
        }
        if (byte == '\t') {
            out += "\\t";
            ++i;
            continue;
        }
        if (byte < 0x20 || byte == 0x7F) {
            AppendEscapedUnit(out, byte);
            ++i;
            continue;
        }
        const size_t length = SequenceLength(text, i);
        if (length == 0) {
            out += kReplacement;
            ++i;
            continue;
        }
        out.append(text, i, length);
        i += length;
    }
    return out;
}

void JsonWriter::Separator() {
    if (needComma_) out_ += ',';
    needComma_ = true;
}

void JsonWriter::Quoted(std::string_view text) {
    out_ += '"';
    out_ += JsonEscape(text);
    out_ += '"';
}

void JsonWriter::ObjectBegin() {
    Separator();
    out_ += '{';
    needComma_ = false;
}

void JsonWriter::ObjectEnd() {
    out_ += '}';
    needComma_ = true;
}

void JsonWriter::ArrayBegin() {
    Separator();
    out_ += '[';
    needComma_ = false;
}

void JsonWriter::ArrayEnd() {
    out_ += ']';
    needComma_ = true;
}

void JsonWriter::FieldBegin(std::string_view key) {
    Separator();
    Quoted(key);
    out_ += ':';
    needComma_ = false;
}

void JsonWriter::Field(std::string_view key, std::string_view value) {
    FieldBegin(key);
    Quoted(value);
    needComma_ = true;
}

void JsonWriter::Field(std::string_view key, const char* value) {
    Field(key, std::string_view(value ? value : ""));
}

void JsonWriter::FieldNumber(std::string_view key, int64_t value) {
    FieldBegin(key);
    out_ += std::to_string(value);
    needComma_ = true;
}

void JsonWriter::Field(std::string_view key, bool value) {
    FieldBegin(key);
    out_ += value ? "true" : "false";
    needComma_ = true;
}

void JsonWriter::Value(std::string_view value) {
    Separator();
    Quoted(value);
}

void JsonWriter::Value(const char* value) {
    Value(std::string_view(value ? value : ""));
}

void JsonWriter::ValueNumber(int64_t value) {
    Separator();
    out_ += std::to_string(value);
}

}
