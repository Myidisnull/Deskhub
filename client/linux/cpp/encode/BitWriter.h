#pragma once
// =============================================================================
// BitWriter.h — ghi bit theo cú pháp RBSP của H.264. Chỉ dùng để dựng SPS/PPS.
//
// VÌ SAO PHẢI TỰ VIẾT SPS/PPS
//   VA-API mã hoá phần SLICE cho ta, nhưng bộ tham số (SPS/PPS) thì mỗi driver một
//   hành vi: có driver tự sinh, có driver không, và gần như không driver nào LẶP
//   LẠI chúng ở mỗi IDR. Giao thức Deskhub thì đòi SPS/PPS đi kèm MỖI IDR
//   (docs/08 §3, giống NVENC bật repeatSPSPPS) — vì client vào xem giữa chừng
//   không có cách nào khác để lấy tham số. Nên ta tự dựng byte của SPS/PPS rồi
//   đưa cho driver dưới dạng "packed header": nó ghi nguyên xi vào bitstream ở đầu
//   mỗi frame ta yêu cầu.
//
// ⚠ HAI THỨ PHẢI KHỚP TUYỆT ĐỐI
//   Byte SPS/PPS ta viết ở đây phải mô tả ĐÚNG những gì driver thật sự mã hoá.
//   VaEncoder điền VAEncSequenceParameterBufferH264 cho driver và dựng SPS ở đây
//   từ CÙNG một bộ hằng số — đó là lý do các hằng như kLog2MaxFrameNumMinus4 nằm ở
//   VaEncoder.h chứ không rải rác. Lệch một trường (số bit của frame_num chẳng
//   hạn) thì decoder phía client giải ra rác từ frame thứ hai trở đi, và triệu
//   chứng trông y hệt mất gói.
//
// EMULATION PREVENTION (0x03)
//   Chuỗi 00 00 00/01/02/03 trong RBSP phải được chèn một byte 0x03 vào giữa, nếu
//   không bộ giải mã sẽ tưởng đó là start code và cắt NAL sai chỗ. WriteByteEP()
//   làm việc đó; ta bật cờ has_emulation_bytes=1 khi đưa cho VA-API để driver
//   khỏi làm lại lần nữa (làm hai lần thì bitstream hỏng).
//
// LIÊN QUAN: encode/VaEncoder.cpp (nơi dựng SPS/PPS), docs/04-protocol.md §5
// =============================================================================
#include <cstddef>
#include <cstdint>
#include <vector>

class BitWriter {
public:
    // Ghi start code Annex-B + byte header NAL. `nalRefIdc` 0..3, `nalType` 7=SPS,
    // 8=PPS. Start code 4 byte (00 00 00 01) chứ không phải 3: một số bộ giải mã
    // đời cũ chỉ nhận dạng bộ tham số khi nó đứng sau start code dài.
    void StartNal(uint8_t nalRefIdc, uint8_t nalType) {
        Flush();
        out_.push_back(0);
        out_.push_back(0);
        out_.push_back(0);
        out_.push_back(1);
        // Byte header NAL nằm NGOÀI phạm vi emulation prevention (nó không thuộc
        // RBSP), nên đẩy thẳng.
        out_.push_back(uint8_t((nalRefIdc & 3) << 5 | (nalType & 0x1F)));
        zeroRun_ = 0;
    }

    // u(n): n bit không dấu, MSB trước. n ≤ 32.
    void U(uint32_t bits, uint32_t value) {
        for (int i = int(bits) - 1; i >= 0; --i) Bit((value >> i) & 1);
    }

    // ue(v): Exp-Golomb không dấu.
    void UE(uint32_t value) {
        // codeNum = 2^k - 1 + ... : viết k số 0, rồi bit 1, rồi k bit phần dư.
        const uint64_t v = uint64_t(value) + 1;
        uint32_t bits = 0;
        for (uint64_t t = v; t; t >>= 1) ++bits;
        U(bits - 1, 0);
        U(bits, uint32_t(v));
    }

    // se(v): Exp-Golomb có dấu. Ánh xạ 0,1,-1,2,-2... -> 0,1,2,3,4...
    void SE(int32_t value) {
        UE(value <= 0 ? uint32_t(-2 * value) : uint32_t(2 * value - 1));
    }

    // rbsp_trailing_bits(): bit 1 rồi độn 0 tới hết byte.
    void Trailing() {
        Bit(1);
        while (bitCount_) Bit(0);
    }

    const std::vector<uint8_t>& bytes() const {
        return out_;
    }
    // Độ dài tính bằng BIT — VAEncPackedHeaderParameterBuffer đòi con số này, và
    // nó phải tính cả start code lẫn byte header NAL.
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

    // Đẩy nốt các bit lẻ còn trong bộ đệm (chỉ dùng ở ranh giới NAL — trong RBSP
    // hợp lệ thì Trailing() đã căn byte rồi).
    void Flush() {
        while (bitCount_) Bit(0);
    }

    // Ghi một byte RBSP, chèn 0x03 khi cần — xem "emulation prevention" ở đầu file.
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
