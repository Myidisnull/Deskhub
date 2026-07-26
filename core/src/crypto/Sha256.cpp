// =============================================================================
// Sha256.cpp — cài đặt SHA-256 / HMAC / PBKDF2 khai báo ở Sha256.h.
//
// Đây là mã cài đặt một CHUẨN, không phải một thiết kế: mọi hằng số và mọi bước
// đều lấy nguyên từ FIPS 180-4 (SHA-256), RFC 2104 (HMAC) và RFC 8018 §5.2
// (PBKDF2). Không có chỗ nào để sáng tạo, và cũng không nên có — sai một bit ở đây
// thì mọi thứ vẫn "chạy", chỉ là không còn an toàn nữa. Vì thế
// core/tests/crypto/CryptoTests.cpp đối chiếu với vector chuẩn công bố kèm các tài
// liệu trên; đừng sửa file này mà không chạy chúng.
//
// BỐ CỤC: hằng số → Sha256Ctx (Compress/Update/Final) → Sha256 → HmacSha256 →
//         Pbkdf2Sha256 → ConstantTimeEqual.
//
// LIÊN QUAN: deskhub/crypto/Sha256.h (vì sao tự cài + phạm vi bảo vệ)
// =============================================================================
#include "deskhub/crypto/Sha256.h"

#include <cstring>

namespace deskhub {

namespace {

// 64 hằng số vòng của SHA-256: phần thập phân của căn bậc ba 64 số nguyên tố đầu.
constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

// Giá trị băm ban đầu: phần thập phân của căn bậc hai 8 số nguyên tố đầu.
constexpr uint32_t kH0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

inline uint32_t Ror(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}

// Đọc/ghi big-endian tại chỗ. Không dùng deskhub/wire/ByteOrder.h vì tầng crypto
// phải đứng độc lập với tầng wire — nó không biết gì về giao thức, và ngược lại.
inline uint32_t Be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

inline void PutBe32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}

} // namespace

void Sha256Ctx::Reset() {
    for (size_t i = 0; i < 8; ++i) h_[i] = kH0[i];
    bufLen_ = 0;
    totalBits_ = 0;
    std::memset(buf_, 0, sizeof(buf_));
}

// Hàm nén một block 64 byte — trái tim của SHA-256 (FIPS 180-4 §6.2.2).
void Sha256Ctx::Compress(const uint8_t* block) {
    uint32_t w[64];
    for (size_t i = 0; i < 16; ++i) w[i] = Be32(block + i * 4);
    for (size_t i = 16; i < 64; ++i) {
        const uint32_t s0 = Ror(w[i - 15], 7) ^ Ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = Ror(w[i - 2], 17) ^ Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    uint32_t e = h_[4], f = h_[5], g = h_[6], h = h_[7];

    for (size_t i = 0; i < 64; ++i) {
        const uint32_t S1 = Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + S1 + ch + kK[i] + w[i];
        const uint32_t S0 = Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += h;
}

void Sha256Ctx::Update(std::span<const uint8_t> data) {
    totalBits_ += uint64_t(data.size()) * 8;
    size_t off = 0;

    // Lấp nốt block dở dang từ lần Update trước.
    if (bufLen_) {
        const size_t need = kSha256BlockBytes - bufLen_;
        const size_t take = data.size() < need ? data.size() : need;
        std::memcpy(buf_ + bufLen_, data.data(), take);
        bufLen_ += take;
        off = take;
        if (bufLen_ < kSha256BlockBytes) return; // vẫn chưa đủ một block
        Compress(buf_);
        bufLen_ = 0;
    }

    // Nén thẳng từ đầu vào, không qua bộ đệm.
    for (; off + kSha256BlockBytes <= data.size(); off += kSha256BlockBytes)
        Compress(data.data() + off);

    // Phần đuôi lẻ chờ lần Update sau hoặc Final.
    if (off < data.size()) {
        bufLen_ = data.size() - off;
        std::memcpy(buf_, data.data() + off, bufLen_);
    }
}

// Đệm theo FIPS 180-4 §5.1.1: một bit 1, rồi các bit 0, rồi độ dài 64-bit big-endian
// — sao cho tổng độ dài chia hết cho 64 byte.
void Sha256Ctx::Final(std::span<uint8_t> out) {
    if (out.size() < kSha256Bytes) return;

    const uint64_t bits = totalBits_;
    uint8_t pad = 0x80;
    Update(std::span<const uint8_t>(&pad, 1));
    totalBits_ = bits; // Update vừa cộng thêm 8 bit của byte đệm — hoàn lại

    // Đệm 0 tới khi còn đúng 8 byte cuối block cho trường độ dài.
    const uint8_t zero = 0;
    while (bufLen_ != kSha256BlockBytes - 8) {
        Update(std::span<const uint8_t>(&zero, 1));
        totalBits_ = bits;
    }

    uint8_t lenBe[8];
    for (size_t i = 0; i < 8; ++i) lenBe[i] = uint8_t(bits >> (56 - i * 8));
    std::memcpy(buf_ + bufLen_, lenBe, 8);
    Compress(buf_);
    bufLen_ = 0;

    for (size_t i = 0; i < 8; ++i) PutBe32(out.data() + i * 4, h_[i]);
}

void Sha256(std::span<const uint8_t> data, std::span<uint8_t> out) {
    Sha256Ctx ctx;
    ctx.Update(data);
    ctx.Final(out);
}

namespace {

// HMAC (RFC 2104): H((K ⊕ opad) ‖ H((K ⊕ ipad) ‖ m)), với `m` cho phép truyền làm
// HAI mảnh rời.
//
// Vì sao hai mảnh: PBKDF2 cần HMAC(P, salt ‖ INT(1)). Nối hai thứ đó vào một bộ
// đệm tạm buộc phải chọn một kích thước trần cho salt, và cái trần đó sẽ cắt cụt
// salt trong im lặng nếu sau này ai đó tăng kAuthSaltBytes — một lỗi làm YẾU hệ
// thống mà không có triệu chứng nào. Hash vốn nạp dần được, nên cứ nạp làm hai
// lượt là bài toán biến mất, không cần trần nào cả.
void HmacSha256Parts(std::span<const uint8_t> key, std::span<const uint8_t> m1,
    std::span<const uint8_t> m2, std::span<uint8_t> out) {
    if (out.size() < kSha256Bytes) return;

    // Chuẩn hoá khoá về đúng một block: dài hơn thì băm, ngắn hơn thì đệm 0.
    uint8_t k[kSha256BlockBytes] = {};
    if (key.size() > kSha256BlockBytes) {
        Sha256(key, std::span<uint8_t>(k, kSha256Bytes));
    } else if (!key.empty()) {
        std::memcpy(k, key.data(), key.size());
    }

    uint8_t ipad[kSha256BlockBytes], opad[kSha256BlockBytes];
    for (size_t i = 0; i < kSha256BlockBytes; ++i) {
        ipad[i] = uint8_t(k[i] ^ 0x36);
        opad[i] = uint8_t(k[i] ^ 0x5c);
    }

    uint8_t inner[kSha256Bytes];
    Sha256Ctx ctx;
    ctx.Update(std::span<const uint8_t>(ipad, kSha256BlockBytes));
    if (!m1.empty()) ctx.Update(m1);
    if (!m2.empty()) ctx.Update(m2);
    ctx.Final(std::span<uint8_t>(inner, kSha256Bytes));

    ctx.Reset();
    ctx.Update(std::span<const uint8_t>(opad, kSha256BlockBytes));
    ctx.Update(std::span<const uint8_t>(inner, kSha256Bytes));
    ctx.Final(out);
}

} // namespace

void HmacSha256(std::span<const uint8_t> key, std::span<const uint8_t> data,
    std::span<uint8_t> out) {
    HmacSha256Parts(key, data, {}, out);
}

// PBKDF2 với dkLen = 32 (RFC 8018 §5.2). Vì đầu ra vừa đúng một block HMAC, chỉ có
// khối T_1 — bỏ được vòng lặp ghép khối của thuật toán tổng quát.
//   U_1 = HMAC(P, S ‖ INT(1));  U_i = HMAC(P, U_{i-1});  T_1 = U_1 ⊕ … ⊕ U_c
void Pbkdf2Sha256(std::span<const uint8_t> password, std::span<const uint8_t> salt,
    uint32_t iterations, std::span<uint8_t> out) {
    if (out.size() < kSha256Bytes) return;
    if (iterations == 0) iterations = 1;

    // U_1 = HMAC(P, S ‖ 00000001) — chỉ số khối 1, big-endian, theo đúng đặc tả.
    // Salt và chỉ số đi làm hai mảnh nên salt dài bao nhiêu cũng được.
    const uint8_t idx[4] = {0, 0, 0, 1};
    uint8_t u[kSha256Bytes];
    HmacSha256Parts(password, salt, std::span<const uint8_t>(idx, 4),
        std::span<uint8_t>(u, kSha256Bytes));

    uint8_t acc[kSha256Bytes];
    std::memcpy(acc, u, kSha256Bytes);

    // U_i = HMAC(P, U_{i-1}) — khoá LUÔN là mật khẩu, chỉ thông điệp mới đổi theo
    // vòng. Lấy U_{i-1} làm khoá là một lỗi kinh điển: nó vẫn cho ra một chuỗi byte
    // trông ngẫu nhiên và mọi test khứ hồi tự viết vẫn đạt, nhưng kết quả không còn
    // là PBKDF2 nữa (vector chuẩn RFC 6070 sẽ bắt được).
    uint8_t next[kSha256Bytes];
    for (uint32_t i = 1; i < iterations; ++i) {
        HmacSha256(password, std::span<const uint8_t>(u, kSha256Bytes),
            std::span<uint8_t>(next, kSha256Bytes));
        std::memcpy(u, next, kSha256Bytes);
        for (size_t b = 0; b < kSha256Bytes; ++b) acc[b] ^= u[b];
    }

    std::memcpy(out.data(), acc, kSha256Bytes);
}

// Gộp mọi khác biệt vào một biến rồi mới kiểm tra MỘT lần ở cuối: không có nhánh
// nào phụ thuộc nội dung, nên thời gian chạy chỉ phụ thuộc ĐỘ DÀI (vốn công khai).
bool ConstantTimeEqual(std::span<const uint8_t> a, std::span<const uint8_t> b) {
    if (a.size() != b.size()) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff = uint8_t(diff | (a[i] ^ b[i]));
    return diff == 0;
}

} // namespace deskhub
