#pragma once
#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>

namespace deskhub::cli {

class JsonWriter {
public:
    void ObjectBegin();
    void ObjectEnd();
    void ArrayBegin();
    void ArrayEnd();

    void FieldBegin(std::string_view key);
    void Field(std::string_view key, std::string_view value);
    void Field(std::string_view key, const char* value);
    void Field(std::string_view key, bool value);
    void FieldNumber(std::string_view key, int64_t value);

    template <class T>
        requires std::integral<T> && (!std::same_as<T, bool>)
    void Field(std::string_view key, T value) {
        FieldNumber(key, int64_t(value));
    }

    void Value(std::string_view value);
    void Value(const char* value);
    void ValueNumber(int64_t value);

    template <class T>
        requires std::integral<T> && (!std::same_as<T, bool>)
    void Value(T value) {
        ValueNumber(int64_t(value));
    }

    const std::string& Text() const {
        return out_;
    }

private:
    void Separator();
    void Quoted(std::string_view text);

    std::string out_{};
    bool needComma_ = false;
};

std::string JsonEscape(std::string_view text);

}
