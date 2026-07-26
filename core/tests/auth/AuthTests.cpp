// =============================================================================
// AuthTests.cpp — cổng gác mật khẩu: AuthGuard đứng riêng, rồi bắt tay đầu-cuối
// qua HostSession ↔ ClientSession.
//
// Trọng tâm của file này là các ca "KHÔNG được phép xảy ra". Một lỗi ở tầng xác
// thực thường không làm gì hỏng cả — nó chỉ lặng lẽ mở cửa. Nên phần lớn các Check
// dưới đây khẳng định một điều gì đó bị TỪ CHỐI, và ca dễ viết nhất (mật khẩu đúng
// thì vào được) là ca ít giá trị nhất.
//
// LIÊN QUAN: deskhub/auth/AuthGuard.h, deskhub/auth/PasswordAuth.h,
//            core/tests/session/SessionTests.cpp (bắt tay khi KHÔNG có mật khẩu)
// =============================================================================
#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/auth/AuthGuard.h"
#include "deskhub/session/ClientSession.h"
#include "deskhub/session/HostSession.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

using namespace deskhub;

namespace {

const char* kPassword = "correct horse battery";

std::vector<uint8_t> Salt(uint8_t seed) {
    std::vector<uint8_t> s(kAuthSaltBytes);
    for (size_t i = 0; i < s.size(); ++i) s[i] = uint8_t(seed + i);
    return s;
}

std::vector<uint8_t> Nonce(uint8_t seed) {
    std::vector<uint8_t> n(kAuthNonceBytes);
    for (size_t i = 0; i < n.size(); ++i) n[i] = uint8_t(seed * 7 + i * 3 + 1);
    return n;
}

std::vector<uint8_t> Token(uint8_t seed) {
    std::vector<uint8_t> t(kAuthTokenBytes);
    for (size_t i = 0; i < t.size(); ++i) t[i] = uint8_t(seed * 31 + i);
    return t;
}

// Lời đáp đúng cho challenge đang chờ của `g`.
std::vector<uint8_t> GoodProof(const AuthGuard& g, uint32_t clientId, const char* pw) {
    const AuthKey k = DeriveKey(pw, g.salt(), g.iterations());
    std::vector<uint8_t> proof(kAuthProofBytes);
    ComputeProof(k, g.pendingNonce(), clientId, proof);
    return proof;
}

// Ít vòng KDF cho test chạy nhanh: 100k vòng × vài chục ca sẽ mất hàng chục giây,
// mà thứ đang kiểm chứng là LUỒNG, không phải chi phí tính toán.
constexpr uint32_t kFastIters = 64;

void SetTestPassword(AuthGuard& g, const char* pw, uint8_t saltSeed = 1) {
    g.SetKey(DeriveKey(pw, Salt(saltSeed), kFastIters));
    g.SetRequirePassword(true);
}

// ---------------------------------------------------------------------------

void TestGuardBasics() {
    std::printf("[auth] no password configured -> everyone is let in as before...\n");
    uint64_t now = 1'000'000;
    AuthGuard g;
    RejectReason r = RejectReason::None;

    Check(g.OnHello(1, {}, now, r) == AuthGuard::Outcome::Allow,
        "no password -> Allow");
    // Bật ô "require" mà chưa đặt mật khẩu vẫn phải mở: chặn ở đây sẽ tạo ra một
    // host không ai vào được và người dùng không có cách nào biết vì sao.
    g.SetRequirePassword(true);
    Check(!g.requirePassword(), "require-password is inert until a password exists");
    Check(g.OnHello(1, {}, now, r) == AuthGuard::Outcome::Allow,
        "require without a password still allows");
}

void TestChallengeResponse() {
    std::printf("[auth] challenge -> correct proof -> Allow; wrong proof -> AuthFailed...\n");
    uint64_t now = 1'000'000;
    AuthGuard g;
    SetTestPassword(g, kPassword);
    RejectReason r = RejectReason::None;

    Check(g.OnHello(42, {}, now, r) == AuthGuard::Outcome::NeedChallenge,
        "password set -> NeedChallenge");
    Check(g.BeginChallenge(42, Nonce(1), now), "BeginChallenge accepts a fresh nonce");
    Check(g.pendingNonce().size() == kAuthNonceBytes, "nonce is held for the response");

    Check(g.OnResponse(42, GoodProof(g, 42, kPassword), now, r) == AuthGuard::Outcome::Allow,
        "correct proof -> Allow");
    Check(g.wrongTries() == 0, "a correct proof leaves no failure behind");

    // Sai mật khẩu.
    Check(g.OnHello(42, {}, now, r) == AuthGuard::Outcome::NeedChallenge, "second attempt");
    Check(g.BeginChallenge(42, Nonce(2), now), "second challenge");
    Check(g.OnResponse(42, GoodProof(g, 42, "wrong password"), now, r) ==
            AuthGuard::Outcome::Reject,
        "wrong proof -> Reject");
    Check(r == RejectReason::AuthFailed, "reason says the password was wrong");
    Check(g.wrongTries() == 1, "a wrong proof is counted");
}

void TestProofIsBoundToNonceAndClient() {
    std::printf("[auth] a proof cannot be replayed, nor lifted onto another client...\n");
    uint64_t now = 1'000'000;
    AuthGuard g;
    SetTestPassword(g, kPassword);
    RejectReason r = RejectReason::None;

    g.OnHello(42, {}, now, r);
    g.BeginChallenge(42, Nonce(3), now);
    const auto proof = GoodProof(g, 42, kPassword);
    Check(g.OnResponse(42, proof, now, r) == AuthGuard::Outcome::Allow, "first use works");

    // PHÁT LẠI: đúng lời đáp đó, lần thứ hai. Challenge đã bị tiêu ở lần trước nên
    // không còn gì để đối chiếu — đây là thứ nonce sinh ra để chặn.
    Check(g.OnResponse(42, proof, now, r) == AuthGuard::Outcome::Reject,
        "the same proof replayed is refused");

    // Cùng mật khẩu nhưng proof của client khác: clientId được trộn vào proof nên
    // không cắt-dán được từ phiên bắt tay của máy khác.
    g.OnHello(42, {}, now, r);
    g.BeginChallenge(42, Nonce(4), now);
    const auto otherClientProof = GoodProof(g, 99, kPassword); // ký cho clientId 99
    Check(g.OnResponse(42, otherClientProof, now, r) == AuthGuard::Outcome::Reject,
        "a proof signed for another clientId is refused");

    // Một nonce khác, cùng mật khẩu: cũng phải hỏng.
    AuthGuard g2;
    SetTestPassword(g2, kPassword);
    g2.OnHello(42, {}, now, r);
    g2.BeginChallenge(42, Nonce(5), now);
    const auto staleProof = GoodProof(g2, 42, kPassword);
    g2.OnHello(42, {}, now, r);
    g2.BeginChallenge(42, Nonce(6), now); // nonce mới
    Check(g2.OnResponse(42, staleProof, now, r) == AuthGuard::Outcome::Reject,
        "a proof for a previous nonce is refused");
}

void TestLockout() {
    std::printf("[auth] three wrong tries lock the door for five minutes...\n");
    uint64_t now = 1'000'000;
    AuthGuard g;
    SetTestPassword(g, kPassword);
    RejectReason r = RejectReason::None;

    for (int i = 0; i < 3; ++i) {
        g.OnHello(42, {}, now, r);
        g.BeginChallenge(42, Nonce(uint8_t(10 + i)), now);
        g.OnResponse(42, GoodProof(g, 42, "nope"), now, r);
    }
    Check(g.lockedOut(now), "locked out after 3 wrong tries");
    Check(r == RejectReason::LockedOut, "the third failure reports LockedOut");

    // Trong lúc bị khoá, ngay cả mật khẩu ĐÚNG cũng không vào được — nếu không thì
    // khoá tạm chẳng ngăn được ai.
    Check(g.OnHello(42, {}, now, r) == AuthGuard::Outcome::Reject, "locked out blocks HELLO");
    Check(r == RejectReason::LockedOut, "…and says why");

    // Hết hạn khoá.
    now += AuthGuard::kLockoutUs + 1;
    Check(!g.lockedOut(now), "lockout expires");
    Check(g.OnHello(42, {}, now, r) == AuthGuard::Outcome::NeedChallenge,
        "after the lockout expires the door opens again");

    // Tắt lockout thì đếm mãi mà không khoá (ô "Lock out after 3 wrong tries").
    AuthGuard g2;
    SetTestPassword(g2, kPassword);
    g2.SetLockoutEnabled(false);
    for (int i = 0; i < 6; ++i) {
        g2.OnHello(42, {}, now, r);
        g2.BeginChallenge(42, Nonce(uint8_t(20 + i)), now);
        g2.OnResponse(42, GoodProof(g2, 42, "nope"), now, r);
    }
    Check(!g2.lockedOut(now), "lockout disabled -> never locks");
    Check(g2.wrongTries() == 6, "…but the counter still shows the attempts");
}

void TestStrayResponseCannotLockTheOwnerOut() {
    std::printf("[auth] stray AUTH_RESPONSE packets can't lock the owner out...\n");
    uint64_t now = 1'000'000;
    AuthGuard g;
    SetTestPassword(g, kPassword);
    RejectReason r = RejectReason::None;

    // Không có challenge nào đang chờ. Ai đó ngoài mạng bắn proof rác vào cổng host.
    // Nếu những gói này tính vào bộ đếm sai thì ba gói rác là khoá được host khỏi
    // chính chủ của nó — một kiểu DoS rẻ hơn nhiều so với dò mật khẩu.
    std::vector<uint8_t> junk(kAuthProofBytes, 0xAB);
    for (int i = 0; i < 10; ++i) {
        Check(g.OnResponse(7, junk, now, r) == AuthGuard::Outcome::Reject,
            "stray response is refused");
    }
    Check(g.wrongTries() == 0, "stray responses do NOT count as wrong tries");
    Check(!g.lockedOut(now), "…so they cannot cause a lockout");

    // Challenge quá hạn cũng vậy: nguyên nhân thường là mất gói, không phải dò mật khẩu.
    g.OnHello(42, {}, now, r);
    g.BeginChallenge(42, Nonce(9), now);
    const auto good = GoodProof(g, 42, kPassword);
    now += AuthGuard::kChallengeTimeoutUs + 1;
    Check(g.OnResponse(42, good, now, r) == AuthGuard::Outcome::Reject,
        "an expired challenge is refused even with the right password");
    Check(g.wrongTries() == 0, "an expired challenge is not a wrong try");
}

void TestTrustedDevices() {
    std::printf("[auth] a remembered device skips the password; Forget puts it back...\n");
    uint64_t now = 1'000'000;
    AuthGuard g;
    SetTestPassword(g, kPassword);
    RejectReason r = RejectReason::None;

    const auto tok = Token(1);
    Check(g.Remember(42, "iPhone 15 Pro", "192.168.1.44", tok, now), "device remembered");
    Check(g.devices().size() == 1, "one row in the trusted list");
    Check(g.devices()[0].name == "iPhone 15 Pro", "…with the name the client gave");

    Check(g.OnHello(42, tok, now, r) == AuthGuard::Outcome::Allow,
        "a valid token skips the challenge");

    // Token sai của một thiết bị ĐANG được nhớ là bất thường — tính như đáp sai.
    Check(g.OnHello(42, Token(2), now, r) == AuthGuard::Outcome::NeedChallenge,
        "a wrong token falls back to the password");
    Check(g.wrongTries() == 1, "a wrong token for a known device is counted");

    // clientId lạ chìa token bừa: không phải thiết bị được nhớ, đi đường mật khẩu.
    Check(g.OnHello(777, Token(3), now, r) == AuthGuard::Outcome::NeedChallenge,
        "an unknown client with a random token gets the challenge");

    Check(g.Forget(42), "Forget removes the row");
    Check(g.devices().empty(), "trusted list is empty");
    Check(g.OnHello(42, tok, now, r) == AuthGuard::Outcome::NeedChallenge,
        "a forgotten device must enter the password again");
}

void TestPasswordChangeRevokesDevices() {
    std::printf("[auth] changing the password forgets every trusted device...\n");
    uint64_t now = 1'000'000;
    AuthGuard g;
    SetTestPassword(g, kPassword);
    RejectReason r = RejectReason::None;

    const auto tok = Token(4);
    g.Remember(42, "Pixel 8", "192.168.1.61", tok, now);
    Check(g.OnHello(42, tok, now, r) == AuthGuard::Outcome::Allow, "token works before");

    // Người ta đổi mật khẩu chính vì muốn cắt quyền của một máy nào đó. Giữ token cũ
    // sống sót nghĩa là máy bị mất cắp hôm qua vẫn vào được hôm nay.
    g.SetPassword("a brand new password", Salt(9));
    Check(g.devices().empty(), "changing the password clears the trusted list");
    Check(g.OnHello(42, tok, now, r) == AuthGuard::Outcome::NeedChallenge,
        "the old token no longer opens the door");
}

void TestGuardRefusesBadEntropy() {
    std::printf("[auth] a zero nonce is refused rather than trusted...\n");
    uint64_t now = 1'000'000;
    AuthGuard g;
    SetTestPassword(g, kPassword);

    // Nonce toàn 0 gần như chắc chắn là RandomBytes vừa hỏng mà caller không kiểm
    // tra. Nhận nó vào thì mọi challenge dùng chung một nonce cố định, và lời đáp
    // bắt được hôm qua phát lại được hôm nay.
    std::vector<uint8_t> zero(kAuthNonceBytes, 0);
    Check(!g.BeginChallenge(42, zero, now), "an all-zero nonce is refused");
    Check(!g.BeginChallenge(42, std::vector<uint8_t>(8, 0xFF), now),
        "a wrong-sized nonce is refused");

    AuthGuard g2; // chưa có mật khẩu
    Check(!g2.BeginChallenge(42, Nonce(1), now), "no password -> no challenge to issue");
}

// ---------------------------------------------------------------------------
// Bắt tay đầu-cuối qua hai máy trạng thái thật.
// ---------------------------------------------------------------------------

struct AuthRig {
    std::deque<Datagram> toHost, toClient;
    HostSession host;
    ClientSession cli;

    bool ready = false, passwordAsked = false;
    std::string dead;
    std::vector<uint8_t> savedToken;

    AuthRig() : host(HostCb(), StreamParams{1920, 1080, 60, 20'000'000}), cli(CliCb()) {}

    HostCallbacks HostCb() {
        HostCallbacks cb;
        cb.send = [this](std::span<const uint8_t> d) { toClient.emplace_back(d.begin(), d.end()); };
        cb.randomBytes = TestRandomBytes;
        return cb;
    }
    ClientCallbacks CliCb() {
        ClientCallbacks cb;
        cb.send = [this](std::span<const uint8_t> d) { toHost.emplace_back(d.begin(), d.end()); };
        cb.onReady = [this](const NegotiatedParams&) { ready = true; };
        cb.onDisconnect = [this](const char* r) { dead = r ? r : ""; };
        cb.onPasswordNeeded = [this] { passwordAsked = true; };
        cb.onDeviceToken = [this](std::span<const uint8_t> t) {
            savedToken.assign(t.begin(), t.end());
        };
        return cb;
    }

    // Đẩy hết gói đang chờ qua lại tới khi cả hai chiều lặng.
    void Pump(uint64_t now) {
        for (int i = 0; i < 20 && (!toHost.empty() || !toClient.empty()); ++i) {
            while (!toHost.empty()) {
                const auto d = toHost.front();
                toHost.pop_front();
                host.HandlePacket(d, now);
            }
            while (!toClient.empty()) {
                const auto d = toClient.front();
                toClient.pop_front();
                cli.HandlePacket(d, now);
            }
        }
    }
};

Hello MakeHello(uint32_t clientId, std::string name = "iPhone 15 Pro") {
    Hello h{};
    h.clientId = clientId;
    h.codecMask = kCodecMaskH264;
    h.maxWidth = 1920;
    h.maxHeight = 1080;
    h.desiredFps = 60;
    h.deviceName = std::move(name);
    return h;
}

void TestEndToEndHandshake() {
    std::printf("[auth] end-to-end: password gate, then a remembered device...\n");
    uint64_t now = 5'000'000;

    AuthRig rig;
    rig.host.auth().SetKey(DeriveKey(kPassword, Salt(3), kFastIters));
    rig.host.auth().SetRequirePassword(true);

    // 1) Client không biết mật khẩu: bắt tay dừng lại, giao diện được hỏi.
    rig.cli.Start(MakeHello(1234), now);
    rig.Pump(now);
    Check(!rig.ready, "no session without the password");
    Check(rig.passwordAsked, "the client asked its UI for a password");
    Check(rig.host.state() == HostSession::State::Authenticating,
        "host is waiting on the challenge, with no session yet");
    Check(rig.host.sessionId() == 0, "no sessionId is handed out before the password");

    // 2) Người dùng nhập mật khẩu; lần HELLO phát lại kế tiếp đi qua trọn vẹn.
    rig.cli.SetPassword(kPassword);
    now += kHelloRetryUs;
    rig.cli.Tick(now);
    rig.Pump(now);
    Check(rig.ready, "correct password -> session established");
    Check(rig.host.sessionId() != 0, "host handed out a sessionId");
    Check(rig.cli.sessionId() == rig.host.sessionId(), "both sides agree on the sessionId");
    Check(!rig.savedToken.empty(), "host issued a device token to remember");
    Check(rig.host.auth().devices().size() == 1, "…and added a row to the trusted list");
    Check(rig.host.auth().devices()[0].name == "iPhone 15 Pro",
        "the trusted row carries the name from HELLO");

    // 3) Lần sau chìa token ra: vào thẳng, KHÔNG cần mật khẩu.
    AuthRig rig2;
    rig2.host.auth().SetKey(DeriveKey(kPassword, Salt(3), kFastIters));
    rig2.host.auth().SetRequirePassword(true);
    rig2.host.auth().Remember(1234, "iPhone 15 Pro", "192.168.1.44", rig.savedToken, now);

    Hello h = MakeHello(1234);
    h.deviceToken = rig.savedToken;
    rig2.cli.Start(h, now); // chú ý: cli2 KHÔNG được SetPassword
    rig2.Pump(now);
    Check(rig2.ready, "a remembered device connects without the password");
    Check(!rig2.passwordAsked, "…and its UI was never asked for one");
}

void TestEndToEndWrongPassword() {
    std::printf("[auth] end-to-end: a wrong password is told apart from a busy host...\n");
    uint64_t now = 5'000'000;

    AuthRig rig;
    rig.host.auth().SetKey(DeriveKey(kPassword, Salt(3), kFastIters));
    rig.host.auth().SetRequirePassword(true);

    rig.cli.SetPassword("not the password");
    rig.cli.Start(MakeHello(1234), now);
    rig.Pump(now);

    Check(!rig.ready, "wrong password -> no session");
    Check(rig.cli.rejectReason() == RejectReason::AuthFailed,
        "the client knows it was the password, not a busy host");
    Check(rig.dead == "wrong password", "…and says so in plain words");
    Check(!rig.cli.hasPassword(),
        "the wrong password is dropped so retries don't burn the 3-try budget");
    Check(rig.host.state() == HostSession::State::Idle, "host went back to Idle");
    Check(rig.host.sessionId() == 0, "no sessionId was ever handed out");
}

void TestSpoofedStartCannotOpenASession() {
    std::printf("[auth] a forged START with sessionId 0 cannot hijack the handshake...\n");
    uint64_t now = 5'000'000;

    AuthRig rig;
    rig.host.auth().SetKey(DeriveKey(kPassword, Salt(3), kFastIters));
    rig.host.auth().SetRequirePassword(true);

    rig.cli.Start(MakeHello(1234), now);
    rig.Pump(now);
    Check(rig.host.state() == HostSession::State::Authenticating, "host is mid-handshake");

    // Trong lúc host đang chờ lời đáp, sessionId của nó vẫn là 0. Một START giả mang
    // sessionId = 0 sẽ khớp với phép so trần `h->sessionId != sessionId()` — nếu
    // HostSession không đòi thêm "sessionId phải khác 0" thì gói này vừa đẩy host
    // thẳng sang STREAMING mà chưa ai chứng minh biết mật khẩu.
    uint8_t buf[kMaxDatagram];
    const size_t n = BuildStart(buf, 0);
    Check(!rig.host.HandlePacket(std::span<const uint8_t>(buf, n), now),
        "forged START(sessionId=0) is refused");
    Check(rig.host.state() != HostSession::State::Streaming,
        "…and the host did NOT start streaming");

    // Cùng lý do với INPUT_EVENT: không được có đường nào bơm phím vào máy host mà
    // chưa qua cửa mật khẩu.
    const InputEvent ev{InputType::Key, 0, 65, 0x1E, 1, 0};
    const size_t ni = BuildInputEvents(buf, 0, 0, std::span<const InputEvent>(&ev, 1));
    Check(!rig.host.HandlePacket(std::span<const uint8_t>(buf, ni), now),
        "forged INPUT_EVENT(sessionId=0) is refused");
}

void TestAskBeforeInput() {
    std::printf("[auth] 'ask again before granting mouse and keyboard' holds input back...\n");
    uint64_t now = 5'000'000;

    AuthRig rig;
    rig.host.SetAskBeforeInput(true);
    rig.cli.Start(MakeHello(1234), now); // không mật khẩu — đang thử lớp thứ hai
    rig.Pump(now);
    Check(rig.ready, "session established");

    // Ô "Allow keyboard and mouse" vẫn bật, nhưng phiên chưa được duyệt riêng.
    Check(!rig.host.inputAllowed(),
        "a new session starts view-only even though Allow is on");
    Check(!rig.host.inputGranted(), "…because this session was not granted yet");

    rig.host.GrantInput();
    Check(rig.host.inputAllowed(), "granting input opens it");

    // Tắt ô Allow vẫn phải thắng, dù phiên đã được duyệt: hai điều kiện độc lập.
    rig.host.SetInputAllowed(false);
    Check(!rig.host.inputAllowed(), "the Allow checkbox still overrides a granted session");
}

} // namespace

void RunAuthTests() {
    TestGuardBasics();
    TestChallengeResponse();
    TestProofIsBoundToNonceAndClient();
    TestLockout();
    TestStrayResponseCannotLockTheOwnerOut();
    TestTrustedDevices();
    TestPasswordChangeRevokesDevices();
    TestGuardRefusesBadEntropy();
    TestEndToEndHandshake();
    TestEndToEndWrongPassword();
    TestSpoofedStartCannotOpenASession();
    TestAskBeforeInput();
}
