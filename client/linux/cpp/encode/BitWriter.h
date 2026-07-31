#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

class BitWriter {
public:
    void StartNal(uint8_t nalRefIdc, uint8_t nalType) {
        Flush();
        out_.push_back(0);
        out_.push_back(0);
        out_.push_back(0);
        out_.push_back(1);
        out_.push_back(uint8_t((nalRefIdc & 3) << 5 | (nalType & 0x1F)));
        zeroRun_ = 0;
    }

    void U(uint32_t bits, uint32_t value) {
        for (int i = int(bits) - 1; i >= 0; --i) Bit((value >> i) & 1);
    }

    void UE(uint32_t value) {
        const uint64_t v = uint64_t(value) + 1;
        uint32_t bits = 0;
        for (uint64_t t = v; t; t >>= 1) ++bits;
        U(bits - 1, 0);
        U(bits, uint32_t(v));
    }

    void SE(int32_t value) {
        UE(value <= 0 ? uint32_t(-2 * value) : uint32_t(2 * value - 1));
    }

    void Trailing() {
        Bit(1);
        while (bitCount_) Bit(0);
    }

    const std::vector<uint8_t>& bytes() const {
        return out_;
    }
    uint32_t bitLength() const {
        return uint32_t(out_.size()) * 8;
    }
    void Clear() {
        out_.clear();
        cur_ = 0;
        bitCount_ = 0;
        zeroRun_ = 0;
    }

private:
    void Bit(uint32_t b) {
        cur_ = uint8_t((cur_ << 1) | (b & 1));
        if (++bitCount_ == 8) {
            WriteByteEP(cur_);
            cur_ = 0;
            bitCount_ = 0;
        }
    }

    void Flush() {
        while (bitCount_) Bit(0);
    }

    void WriteByteEP(uint8_t b) {
        if (zeroRun_ >= 2 && b <= 3) {
            out_.push_back(0x03);
            zeroRun_ = 0;
        }
        out_.push_back(b);
        zeroRun_ = b == 0 ? zeroRun_ + 1 : 0;
    }

    std::vector<uint8_t> out_;
    uint8_t cur_ = 0;
    uint32_t bitCount_ = 0;
    uint32_t zeroRun_ = 0;
};
