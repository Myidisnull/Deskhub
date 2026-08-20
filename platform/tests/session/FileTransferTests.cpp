#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/protocol/Wire.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/net/SessionTransport.h"
#include "deskhubp/session/AuthNegotiation.h"
#include "deskhubp/session/FileHost.h"
#include "deskhubp/ffi/TransferFfi.h"
#include "deskhubp/session/FileUpload.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/system/FileStore.h"
#include "deskhubp/system/HostIdentity.h"
#include "deskhubp/system/PairedDevicesFile.h"
#include "deskhubp/system/TrustStoreFile.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {

constexpr uint16_t kFileTestPort = 47836;
constexpr const char* kFilePasscode = "0417";
constexpr uint32_t kAuthTimeoutMs = 4000;

std::filesystem::path Scratch(const std::string& leaf) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "deskhub-file-tests";
    std::error_code ec;
    std::filesystem::remove_all(root / leaf, ec);
    std::filesystem::create_directories(root / leaf, ec);
    return root / leaf;
}

std::vector<uint8_t> Pattern(size_t size, uint8_t seed) {
    std::vector<uint8_t> bytes(size);
    for (size_t i = 0; i < size; ++i) bytes[i] = uint8_t(i * 31 + seed);
    return bytes;
}

std::filesystem::path WriteFile(const std::filesystem::path& dir, const std::string& name,
    const std::vector<uint8_t>& bytes) {
    const std::filesystem::path path = dir / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return path;
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

bool WaitUntil(const std::function<bool()>& done, int millis) {
    for (int i = 0; i < millis; ++i) {
        if (done()) return true;
        SleepUs(1000);
    }
    return done();
}

struct HostRig {
    deskhubp::SessionTransport sock{};
    deskhubp::FileHost files{};
    std::thread pump{};
    std::atomic<bool> stop{false};

    ~HostRig() {
        Shutdown();
    }

    bool Start(const deskhubp::HostIdentity& identity, const std::filesystem::path& dir) {
        sock.SetRecvTimeout(1);
        deskhubp::QuicSettings settings;
        settings.certPemPath = identity.certPath;
        settings.keyPemPath = identity.keyPath;
        if (!sock.Listen(settings, kFileTestPort, "127.0.0.1")) return false;

        deskhubp::HostAuthConfig auth;
        auth.identity = identity;
        auth.SetPasscode(deskhubp::LoadOrCreateAuthSalt(), kFilePasscode);
        auth.allowNewPairings = true;
        sock.SetHostAuth(std::move(auth), deskhubp::TransportAuthCallbacks{});
        sock.SetOnPeerGone([this](const NetAddr& peer) { files.OnPeerGone(peer); });

        if (!files.Start(sock, dir, deskhubp::FileHostCallbacks{})) return false;

        pump = std::thread([this] {
            uint8_t buf[deskhub::kMaxRecordSize];
            while (!stop.load(std::memory_order_acquire)) {
                NetAddr from;
                const int n = sock.RecvFrom(buf, sizeof(buf), from);
                if (n <= 0) continue;
                const std::span<const uint8_t> message(buf, size_t(n));
                const auto header = deskhub::ParseCommonHeader(message);
                if (header && header->chan == deskhub::Chan::File)
                    files.HandleMessage(from, message);
            }
        });
        return true;
    }

    void Shutdown() {
        if (pump.joinable()) {
            stop.store(true, std::memory_order_release);
            pump.join();
        }
        files.Stop();
        sock.Close();
    }
};

struct ViewerRig {
    deskhubp::SessionTransport sock{};
    std::unique_ptr<deskhubp::FileUpload> upload{};
    std::thread pump{};
    std::atomic<bool> stop{false};
    std::atomic<bool> finished{false};
    deskhub::FileSenderState state = deskhub::FileSenderState::Idle;
    deskhub::TransferReason reason = deskhub::TransferReason::Accepted;
    NetAddr host{};

    ~ViewerRig() {
        Shutdown();
    }

    bool Start(const deskhub::Fingerprint& hostKey) {
        if (!ParseNetAddr(std::string("127.0.0.1:") + std::to_string(kFileTestPort), host))
            return false;
        sock.SetRecvTimeout(1);
        if (!sock.Connect(deskhubp::QuicSettings{}, host, "127.0.0.1")) return false;
        if (!sock.WaitEstablished(host, kAuthTimeoutMs)) return false;

        deskhubp::ClientAuthConfig auth;
        auth.identity = deskhubp::LoadOrCreateHostIdentity("file-test-viewer");
        auth.passcode = kFilePasscode;
        auth.hostFingerprint = hostKey;
        auth.clientName = "file-test-viewer";

        deskhub::AuthResultCode code = deskhub::AuthResultCode::NotPaired;
        bool hostProved = false;
        if (!sock.RunClientAuth(host, std::move(auth), kAuthTimeoutMs, code, hostProved))
            return false;

        deskhubp::FileUploadCallbacks hooks;
        hooks.send = [this](std::span<const uint8_t> message) {
            return sock.SendRecordOn(host, deskhubp::kQuicFileStream, message);
        };
        hooks.onFinished = [this](deskhub::FileSenderState s, deskhub::TransferReason r) {
            state = s;
            reason = r;
            finished.store(true, std::memory_order_release);
        };
        upload = std::make_unique<deskhubp::FileUpload>(std::move(hooks));

        pump = std::thread([this] {
            uint8_t buf[deskhub::kMaxRecordSize];
            while (!stop.load(std::memory_order_acquire)) {
                NetAddr from;
                const int n = sock.RecvFrom(buf, sizeof(buf), from);
                if (n > 0) {
                    const std::span<const uint8_t> message(buf, size_t(n));
                    const auto header = deskhub::ParseCommonHeader(message);
                    if (header && header->chan == deskhub::Chan::File)
                        upload->HandleMessage(message);
                }
                upload->Pump();
            }
        });
        return true;
    }

    void Shutdown() {
        if (pump.joinable()) {
            stop.store(true, std::memory_order_release);
            pump.join();
        }
        sock.Close();
    }
};

void TestFileStoreNeverOverwritesOrLeavesScraps() {
    std::printf("[store] files land whole, beside what is already there...\n");
    const std::filesystem::path dir = Scratch("store");
    deskhubp::FileStore store;
    Check(store.SetDirectory(dir), "the destination folder is created");

    const std::vector<uint8_t> bytes = Pattern(5000, 7);
    Check(store.Open(0, "notes.txt", bytes.size()) == "notes.txt", "a free name is used as is");
    Check(store.Write(0, bytes), "the bytes are written");
    Check(!std::filesystem::exists(dir / "notes.txt"),
        "and nothing bears the final name until the file is whole");
    store.Close(0, true);
    Check(ReadFile(dir / "notes.txt") == bytes, "the finished file is byte for byte");

    Check(store.Open(1, "notes.txt", 10) == "notes (2).txt", "a second arrival is renamed");
    Check(store.Write(1, Pattern(10, 3)), "its bytes are written");
    store.Close(1, true);
    Check(ReadFile(dir / "notes.txt") == bytes, "the first file is untouched");
    Check(std::filesystem::exists(dir / "notes (2).txt"), "and the second sits beside it");

    Check(store.Open(2, "huge.bin", uint64_t(1) << 62).empty(),
        "a file larger than the disk is refused before a byte is written");

    Check(store.Open(3, "abandoned.bin", 100) == "abandoned.bin", "a third file opens");
    Check(store.Write(3, Pattern(100, 1)), "and takes bytes");
    store.Close(3, false);
    Check(!std::filesystem::exists(dir / "abandoned.bin"), "a discarded file leaves nothing");

    size_t scraps = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
        if (entry.path().extension() == deskhubp::kTransferPartSuffix) ++scraps;
    Check(scraps == 0, "and no half-written part file is left behind");
}

void TestABatchCrossesARealConnection() {
    std::printf("[files] a batch crosses a real QUIC connection and lands whole...\n");
    const std::filesystem::path source = Scratch("send");
    const std::filesystem::path landing = Scratch("land");

    const deskhubp::HostIdentity identity = deskhubp::LoadOrCreateHostIdentity("file-test-host");
    HostRig host;
    Check(host.Start(identity, landing), "the host takes files in");

    const std::vector<uint8_t> small = Pattern(64, 1);
    const std::vector<uint8_t> big = Pattern(deskhub::kMaxFileChunkBytes * 9 + 123, 2);
    const std::vector<uint8_t> empty;
    const std::vector<std::filesystem::path> paths{
        WriteFile(source, "notes.txt", small),
        WriteFile(source, "payload.bin", big),
        WriteFile(source, "nothing.dat", empty),
    };

    ViewerRig viewer;
    Check(viewer.Start(identity.fingerprint), "a viewer connects and proves itself");
    Check(viewer.upload->Begin(paths), "the batch is offered");

    Check(WaitUntil([&viewer] { return viewer.finished.load(std::memory_order_acquire); }, 20000),
        "the transfer finishes inside the deadline");
    Check(viewer.state == deskhub::FileSenderState::Done, "and finishes as done");
    Check(host.files.Transfers().size() == 1, "the host lists the viewer that sent them");

    Check(ReadFile(landing / "notes.txt") == small, "the small file is byte for byte");
    Check(ReadFile(landing / "payload.bin") == big, "the multi-chunk file is byte for byte");
    Check(std::filesystem::exists(landing / "nothing.dat"), "the empty file exists");
    Check(std::filesystem::file_size(landing / "nothing.dat") == 0, "and is empty");

    viewer.Shutdown();
    host.Shutdown();
}

void TestAHostThatTakesNoFilesRefuses() {
    std::printf("[files] a host with file transfer switched off refuses the batch...\n");
    const std::filesystem::path source = Scratch("send-refused");
    const std::filesystem::path landing = Scratch("land-refused");

    const deskhubp::HostIdentity identity = deskhubp::LoadOrCreateHostIdentity("file-test-host");
    HostRig host;
    Check(host.Start(identity, landing), "the host starts");
    host.files.SetAccepting(false);

    const std::vector<std::filesystem::path> paths{
        WriteFile(source, "notes.txt", Pattern(128, 5))};

    ViewerRig viewer;
    Check(viewer.Start(identity.fingerprint), "a viewer connects");
    Check(viewer.upload->Begin(paths), "the batch is offered anyway");

    Check(WaitUntil([&viewer] { return viewer.finished.load(std::memory_order_acquire); }, 10000),
        "the viewer is answered inside the deadline");
    Check(viewer.state == deskhub::FileSenderState::Refused, "the batch is refused");
    Check(viewer.reason == deskhub::TransferReason::NotAccepting, "for the stated reason");

    std::error_code ec;
    Check(std::filesystem::is_empty(landing, ec), "and nothing reached the folder");

    viewer.Shutdown();
    host.Shutdown();
}

void TestTheSendSurfaceTheClientPageDrives() {
    std::printf("[files] the standalone send surface carries a batch on its own...\n");
    const std::filesystem::path source = Scratch("ffi-send");
    const std::filesystem::path landing = Scratch("ffi-land");

    const deskhubp::HostIdentity identity = deskhubp::LoadOrCreateHostIdentity("file-test-host");
    HostRig host;
    Check(host.Start(identity, landing), "a host takes files in");

    char problem[256] = {};
    const char* missing[] = {"/no/such/file/at/all"};
    Check(dh_send_check(missing, 1, problem, int(sizeof(problem))) == 0,
        "a file that is not there is refused before anything opens");
    Check(problem[0] != '\0', "with a reason the page can show");
    Check(dh_send_check(nullptr, 0, problem, int(sizeof(problem))) == 0,
        "and so is an empty selection");

    const std::vector<uint8_t> bytes = Pattern(deskhub::kMaxFileChunkBytes * 3 + 7, 9);
    const std::filesystem::path file = WriteFile(source, "from-the-client-page.bin", bytes);
    const std::string path = file.string();
    const char* paths[] = {path.c_str()};
    Check(dh_send_check(paths, 1, problem, int(sizeof(problem))) == 1, "a real file passes");

    const std::string address = std::string("127.0.0.1:") + std::to_string(kFileTestPort);
    Check(dh_send_start("nonsense", kFilePasscode, "page", paths, 1) == nullptr,
        "an address that cannot be read starts nothing");
    Check(dh_send_start(address.c_str(), kFilePasscode, "page", nullptr, 0) == nullptr,
        "and neither does an empty batch");

    DHSend* send = dh_send_start(address.c_str(), kFilePasscode, "client-page", paths, 1);
    Check(send != nullptr, "a sound batch starts");
    if (send == nullptr) return;

    DHSendProgress progress{};
    uint64_t highest = 0;
    bool wentBackwards = false;
    bool spokeTooSoon = false;
    const auto poll = [send, &progress, &highest, &wentBackwards, &spokeTooSoon] {
        dh_send_snapshot(send, &progress);
        if (progress.bytes < highest) wentBackwards = true;
        highest = progress.bytes;
        if (!progress.finished && progress.total > 0 && progress.bytes < progress.total &&
            std::string(progress.message) == deskhub::ui::kTransferDone)
            spokeTooSoon = true;
        return progress.finished;
    };
    Check(WaitUntil(poll, 20000), "the batch finishes inside the deadline");
    Check(!wentBackwards, "the byte count only ever climbs, however often it is polled");
    Check(!spokeTooSoon, "and it never says the files arrived while they are still going");
    Check(progress.state == DHSendDone, "and reports it is done");
    Check(progress.bytes == progress.total && progress.total == bytes.size(),
        "having carried every byte it announced");
    Check(std::string(progress.message) != std::string(), "with something to show the user");

    dh_send_stop(send);
    Check(ReadFile(landing / "from-the-client-page.bin") == bytes,
        "and the file landed byte for byte");

    dh_send_snapshot(nullptr, &progress);
    Check(progress.state == DHSendIdle, "a null handle answers as idle, never crashes");
    dh_send_cancel(nullptr);
    dh_send_stop(nullptr);

    host.Shutdown();
}

void TestBeginRefusesWhatItCannotSend() {
    std::printf("[files] the viewer checks the files before it offers anything...\n");
    const std::filesystem::path dir = Scratch("begin");
    deskhubp::FileUpload upload(deskhubp::FileUploadCallbacks{
        [](std::span<const uint8_t>) { return true; }, {}, {}});

    Check(!upload.Begin({}), "an empty selection is refused");
    Check(!upload.Begin({dir}), "a directory is refused");
    Check(!upload.Begin({dir / "missing.txt"}), "a file that is not there is refused");
    Check(!upload.LastError().empty(), "and the reason is available to show");

    std::vector<std::filesystem::path> tooMany;
    for (size_t i = 0; i <= deskhub::kMaxTransferFiles; ++i)
        tooMany.push_back(WriteFile(dir, "f" + std::to_string(i), Pattern(4, uint8_t(i))));
    Check(!upload.Begin(tooMany), "more files than one batch carries is refused");
}

}

void RunFileTransferPlatformTests() {
    TestFileStoreNeverOverwritesOrLeavesScraps();
    TestBeginRefusesWhatItCannotSend();
    TestTheSendSurfaceTheClientPageDrives();
    TestABatchCrossesARealConnection();
    TestAHostThatTakesNoFilesRefuses();
}
