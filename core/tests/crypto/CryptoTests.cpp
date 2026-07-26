// =============================================================================
// CryptoTests.cpp — đối chiếu SHA-256 / HMAC / PBKDF2 với VECTOR CHUẨN.
//
// VÌ SAO PHẢI LÀ VECTOR CHUẨN, KHÔNG PHẢI TEST KHỨ HỒI
//   Một bản cài SHA-256 sai vẫn là một hàm xác định: băm rồi băm lại vẫn ra cùng
//   kết quả, HMAC vẫn khớp giữa hai đầu, và mọi test kiểu "tự tính rồi tự so" đều
//   ĐẠT. Cái nó không còn là SHA-256 — tức là mọi phân tích an toàn của thế giới về
//   thuật toán này không còn áp dụng cho ta nữa.
//
//   Cách duy nhất phát hiện ra điều đó là so với con số do NGƯỜI KHÁC công bố. Các
//   vector dưới đây lấy từ:
//     SHA-256 — FIPS 180-4 phụ lục B
//     HMAC    — RFC 4231 §4.2, §4.3
//     PBKDF2  — RFC 7914 §11
//
// LIÊN QUAN: deskhub/crypto/Sha256.h
// =============================================================================
#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/crypto/Sha256.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace deskhub;

namespace {

std::string Hex(std::span<const uint8_t> b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (uint8_t x : b) {
        s.push_back(d[x >> 4]);
        s.push_back(d[x & 0xF]);
    }
    return s;
}

std::span<const uint8_t> Bytes(const std::string& s) {
    return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

void CheckHash(const std::string& msg, const char* want, const char* what) {
    uint8_t out[kSha256Bytes];
    Sha256(Bytes(msg), out);
    Check(Hex(out) == want, what);
}

void TestSha256Vectors() {
    std::printf("[crypto] SHA-256 against the FIPS 180-4 vectors...\n");

    CheckHash("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256(\"abc\")");
    CheckHash("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "SHA-256(\"\")");
    CheckHash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
        "SHA-256(56-byte message)");

    // 1 000 000 chữ 'a' — ca kinh điển ép nhiều lần nén liên tiếp và ép trường độ
    // dài 64-bit vượt qua ngưỡng 32 bit của số bit (8 000 000 > 2^22, nhưng quan
    // trọng hơn là nó chạy qua 15625 block).
    CheckHash(std::string(1'000'000, 'a'),
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
        "SHA-256(1M x 'a')");

    // Ranh giới đệm: thông điệp 55 byte vừa đúng chỗ cho 1 byte 0x80 + 8 byte độ
    // dài trong MỘT block; 56 byte thì tràn sang block thứ hai. Đây là chỗ hay sai
    // nhất của mọi bản cài SHA, và hai ca này bắt được nó.
    CheckHash(std::string(55, 'a'),
        "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318",
        "SHA-256(55 bytes — padding fits one block)");
    CheckHash(std::string(56, 'a'),
        "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a",
        "SHA-256(56 bytes — padding spills to a second block)");

    // Nạp dần phải cho kết quả y hệt nạp một lần, kể cả khi cắt ở chỗ lệch block.
    Sha256Ctx ctx;
    const std::string a(100, 'x'), b(37, 'y');
    ctx.Update(Bytes(a));
    ctx.Update(Bytes(b));
    uint8_t inc[kSha256Bytes];
    ctx.Final(inc);
    uint8_t once[kSha256Bytes];
    Sha256(Bytes(a + b), once);
    Check(std::memcmp(inc, once, kSha256Bytes) == 0, "streaming Update matches one-shot");
}

void TestHmacVectors() {
    std::printf("[crypto] HMAC-SHA256 against the RFC 4231 vectors...\n");

    // RFC 4231 §4.2 — khoá 20 byte 0x0b, dữ liệu "Hi There".
    std::vector<uint8_t> k1(20, 0x0b);
    uint8_t out[kSha256Bytes];
    HmacSha256(k1, Bytes(std::string("Hi There")), out);
    Check(Hex(out) == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
        "HMAC RFC 4231 case 1");

    // §4.3 — khoá ngắn hơn một block, phải được đệm 0 chứ không băm.
    HmacSha256(Bytes(std::string("Jefe")),
        Bytes(std::string("what do ya want for nothing?")), out);
    Check(Hex(out) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
        "HMAC RFC 4231 case 2");

    // §4.6 — khoá 131 byte, DÀI HƠN một block: phải băm trước rồi mới đệm. Nhánh
    // này không bao giờ chạy trong Deskhub (khoá của ta luôn 32 byte) nên nếu sai
    // sẽ không có gì khác báo cho ta biết.
    std::vector<uint8_t> k3(131, 0xaa);
    HmacSha256(k3, Bytes(std::string("Test Using Larger Than Block-Size Key - Hash Key First")),
        out);
    Check(Hex(out) == "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
        "HMAC RFC 4231 case 6 (key longer than a block)");
}

void TestPbkdf2Vectors() {
    std::printf("[crypto] PBKDF2-HMAC-SHA256 against the RFC 7914 vectors...\n");

    uint8_t out[kSha256Bytes];
    // Giữ chuỗi trong biến có tên. `Bytes(std::string("password"))` trả về một span
    // trỏ vào một std::string TẠM đã chết ngay cuối biểu thức — span treo, và kết
    // quả sẽ sai một cách không tái lập được.
    const std::string pwStr = "password", saltStr = "salt";
    const auto pw = Bytes(pwStr);
    const auto salt = Bytes(saltStr);

    // c = 1: chỉ có U_1, không vào vòng lặp.
    Pbkdf2Sha256(pw, salt, 1, out);
    Check(Hex(out) == "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b",
        "PBKDF2 c=1");

    // c = 2 là ca bắt được lỗi kinh điển "lấy U_{i-1} làm khoá HMAC thay vì mật
    // khẩu": với c=1 hai cách cho kết quả giống nhau, từ c=2 thì khác.
    Pbkdf2Sha256(pw, salt, 2, out);
    Check(Hex(out) == "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43",
        "PBKDF2 c=2 (catches U_i keyed by the wrong thing)");

    Pbkdf2Sha256(pw, salt, 4096, out);
    Check(Hex(out) == "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a",
        "PBKDF2 c=4096");
}

void TestConstantTime() {
    std::printf("[crypto] constant-time compare: equal, differing, mismatched length...\n");

    const uint8_t a[4] = {1, 2, 3, 4};
    const uint8_t b[4] = {1, 2, 3, 4};
    const uint8_t c[4] = {1, 2, 3, 5}; // khác ở byte CUỐI
    const uint8_t d[4] = {9, 2, 3, 4}; // khác ở byte ĐẦU
    const uint8_t e[3] = {1, 2, 3};

    Check(ConstantTimeEqual(a, b), "equal buffers compare equal");
    Check(!ConstantTimeEqual(a, c), "difference in the last byte is caught");
    Check(!ConstantTimeEqual(a, d), "difference in the first byte is caught");
    Check(!ConstantTimeEqual(a, e), "different lengths never compare equal");
    Check(ConstantTimeEqual(std::span<const uint8_t>(), std::span<const uint8_t>()),
        "two empty buffers compare equal");
}

} // namespace

void RunCryptoTests() {
    TestSha256Vectors();
    TestHmacVectors();
    TestPbkdf2Vectors();
    TestConstantTime();
}
