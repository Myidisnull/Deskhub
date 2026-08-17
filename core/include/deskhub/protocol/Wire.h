#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace deskhub {

inline constexpr uint8_t kProtocolVersion = 1;
inline constexpr size_t kMaxDatagram = 1200;
inline constexpr size_t kCommonHeaderSize = 8;
inline constexpr size_t kVideoHeaderSize = 16;
inline constexpr size_t kFecHeaderSize = 16;

inline constexpr uint16_t kFecGroupSize = 8;

inline constexpr size_t kMaxFecGroups = 256;

inline constexpr size_t kFecLenPrefix = 2;
inline constexpr size_t kEncryptedPayloadOverhead = 24;
inline constexpr size_t kMaxVideoPayload =
    kMaxDatagram - kCommonHeaderSize - kFecHeaderSize - kFecLenPrefix - kEncryptedPayloadOverhead;

inline constexpr size_t kInputHeaderSize = 5;
inline constexpr size_t kInputEventSize = 19;
inline constexpr size_t kMaxInputEvents =
    (kMaxDatagram - kCommonHeaderSize - kInputHeaderSize) / kInputEventSize;

inline constexpr size_t kNackHeaderSize = 5;
inline constexpr size_t kMaxNackIndices = 255;
static_assert(kCommonHeaderSize + kNackHeaderSize + 2 * kMaxNackIndices <= kMaxDatagram);

inline constexpr size_t kClipboardHeaderSize = 8;
inline constexpr size_t kMaxClipboardChunkPayload =
    kMaxDatagram - kCommonHeaderSize - kClipboardHeaderSize;
inline constexpr size_t kMaxClipboardTextBytes = 32768;
inline constexpr size_t kMaxClipboardChunks =
    (kMaxClipboardTextBytes + kMaxClipboardChunkPayload - 1) / kMaxClipboardChunkPayload;

enum class Chan : uint8_t { Control = 0,
    Video = 1,
    Input = 2,
    Audio = 3 };

enum class MsgType : uint8_t {
    Hello = 0x01,
    HelloAck = 0x02,
    Start = 0x03,
    Bye = 0x04,
    ListSources = 0x05,
    SourceList = 0x06,
    VideoPacket = 0x10,
    FecPacket = 0x11,
    InputEvent = 0x20,
    Ping = 0x30,
    Pong = 0x31,
    Feedback = 0x32,
    RequestKeyframe = 0x33,
    Reconfig = 0x34,
    SetFocus = 0x35,
    Nack = 0x36,
    InvalidateRef = 0x37,
    Clipboard = 0x40,
    Noise1 = 0x50,
    Noise2 = 0x51,
    Noise3 = 0x52,
    NoiseDecline = 0x53,
};

inline constexpr uint16_t kFeatureEncryptCapable = 1u << 0;

inline constexpr uint8_t kVideoFlagIdr = 1u << 0;
inline constexpr uint8_t kVideoFlagFrameEnd = 1u << 1;

inline constexpr uint16_t kCodecMaskH264 = 1u << 0;
enum class Codec : uint8_t { H264 = 0,
    Rejected = 0xFF };

struct CommonHeader {
    uint8_t ver;
    MsgType type;
    uint8_t flags;
    Chan chan;
    uint32_t sessionId;
};

inline constexpr uint16_t kDeskhubPort = 47777;
inline constexpr size_t kMaxSources = 8;
inline constexpr size_t kMaxSourceNameBytes = 64;

struct SourceInfo {
    uint8_t sourceId = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    std::string name{};
};

inline constexpr size_t kMaxClientNameBytes = 64;

inline constexpr size_t kPasscodeDigits = 4;

inline constexpr bool IsValidPasscode(std::string_view p) {
    if (p.size() != kPasscodeDigits) return false;
    for (char c : p)
        if (c < '0' || c > '9') return false;
    return true;
}

inline constexpr uint32_t kPasscodeRange = 10000;

inline std::string PasscodeFromRandom(uint32_t value) {
    std::string out(kPasscodeDigits, '0');
    uint32_t left = value % kPasscodeRange;
    for (size_t i = kPasscodeDigits; i-- > 0;) {
        out[i] = char('0' + left % 10);
        left /= 10;
    }
    return out;
}

struct Hello {
    uint32_t clientId;
    uint16_t codecMask;
    uint16_t maxWidth;
    uint16_t maxHeight;
    uint8_t desiredFps;
    uint16_t features;
    uint8_t sourceId = 0;
    std::string passcode{};
    std::string clientName{};
};

enum class RejectReason : uint8_t {
    None = 0,
    Busy = 1,
    CodecMismatch = 2,
    WrongPasscode = 3,
    EncryptionRequired = 4,
    WrongSessionKey = 5,
};

struct HelloAck {
    uint32_t sessionId;
    Codec codec;
    uint16_t width;
    uint16_t height;
    uint8_t fps;
    uint32_t bitrateBps;
    uint64_t timebaseUs;
    RejectReason reason = RejectReason::None;
};

struct PingPong {
    uint32_t pingId;
    uint64_t sendTimeUs;
};

struct Feedback {
    uint16_t lostFrames;
    uint8_t lossPct;
    uint16_t rttMs;
    uint32_t recvBitrateKbps;
};

struct Reconfig {
    uint16_t width;
    uint16_t height;
    uint32_t bitrateBps;
    uint8_t fps = 0;
};

enum class InputType : uint8_t {
    Key = 1,
    MouseMove = 2,
    MouseButton = 3,
    MouseWheel = 4,
};

enum class MouseButton : uint8_t { Left = 1,
    Right = 2,
    Middle = 3,
    X1 = 4,
    X2 = 5 };

inline constexpr int32_t kScanExtended = 0x100;

struct InputEvent {
    InputType type = InputType::Key;
    uint64_t timestampUs = 0;
    int32_t a = 0;
    int32_t b = 0;
    uint8_t state = 0;
    uint8_t absolute = 0;
};

inline constexpr bool IsStateEvent(InputType t) {
    return t == InputType::Key || t == InputType::MouseButton;
}

struct VideoHeader {
    uint32_t frameId;
    uint64_t timestampUs;
    uint16_t pktIndex;
    uint16_t pktCount;
};

struct VideoPacketView {
    VideoHeader hdr;
    bool idr;
    bool frameEnd;
    std::span<const uint8_t> payload;
};

struct FecHeader {
    uint32_t frameId;
    uint64_t timestampUs;
    uint16_t pktCount;
    uint8_t groupIndex;
};

struct FecPacketView {
    FecHeader hdr;
    bool idr;
    std::span<const uint8_t> parity;
};

struct ClipboardChunkView {
    uint32_t revision = 0;
    uint16_t chunkIndex = 0;
    uint16_t chunkCount = 0;
    std::span<const uint8_t> payload{};
};

size_t BuildHello(std::span<uint8_t> out, const Hello& m);
size_t BuildHelloAck(std::span<uint8_t> out, const HelloAck& m);
size_t BuildStart(std::span<uint8_t> out, uint32_t sessionId);
size_t BuildListSources(std::span<uint8_t> out, std::string_view passcode = {});
size_t BuildSourceList(std::span<uint8_t> out, std::span<const SourceInfo> sources);
size_t BuildBye(std::span<uint8_t> out, uint32_t sessionId);
size_t BuildPing(std::span<uint8_t> out, uint32_t sessionId, const PingPong& m);
size_t BuildPong(std::span<uint8_t> out, uint32_t sessionId, const PingPong& m);
size_t BuildFeedback(std::span<uint8_t> out, uint32_t sessionId, const Feedback& m);
size_t BuildRequestKeyframe(std::span<uint8_t> out, uint32_t sessionId);
size_t BuildReconfig(std::span<uint8_t> out, uint32_t sessionId, const Reconfig& m);
size_t BuildSetFocus(std::span<uint8_t> out, uint32_t sessionId, bool focused);
size_t BuildNack(std::span<uint8_t> out, uint32_t sessionId, uint32_t frameId,
    std::span<const uint16_t> indices);
size_t BuildInvalidateRef(std::span<uint8_t> out, uint32_t sessionId, uint32_t frameId);
size_t BuildVideoPacket(std::span<uint8_t> out, uint32_t sessionId, const VideoHeader& vh,
    bool idr, bool frameEnd, std::span<const uint8_t> payload);
size_t BuildFecPacket(std::span<uint8_t> out, uint32_t sessionId, const FecHeader& fh,
    bool idr, std::span<const uint8_t> parity);
size_t BuildInputEvents(std::span<uint8_t> out, uint32_t sessionId, uint32_t firstSeq,
    std::span<const InputEvent> events);
size_t BuildClipboardChunk(std::span<uint8_t> out, uint32_t sessionId,
    const ClipboardChunkView& chunk);

void WriteCommonHeader(std::span<uint8_t> out, const CommonHeader& h);
std::optional<CommonHeader> ParseCommonHeader(std::span<const uint8_t> datagram);
std::span<const uint8_t> PayloadOf(std::span<const uint8_t> datagram);
size_t BuildNoise1(std::span<uint8_t> out, std::span<const uint8_t> body, uint8_t sourceId = 0);
size_t BuildNoise2(std::span<uint8_t> out, std::span<const uint8_t> body);
size_t BuildNoise3(std::span<uint8_t> out, std::span<const uint8_t> body, uint8_t sourceId = 0);
size_t BuildNoiseDecline(std::span<uint8_t> out);

std::optional<Hello> ParseHello(std::span<const uint8_t> payload);
std::string ParseListSourcesPasscode(std::span<const uint8_t> payload);
size_t ParseSourceList(std::span<const uint8_t> payload, std::span<SourceInfo> out);
std::optional<HelloAck> ParseHelloAck(std::span<const uint8_t> payload);
std::optional<PingPong> ParsePingPong(std::span<const uint8_t> payload);
std::optional<Feedback> ParseFeedback(std::span<const uint8_t> payload);
std::optional<Reconfig> ParseReconfig(std::span<const uint8_t> payload);
std::optional<bool> ParseSetFocus(std::span<const uint8_t> payload);
size_t ParseNack(std::span<const uint8_t> payload, uint32_t& frameId,
    std::span<uint16_t> out);
std::optional<uint32_t> ParseInvalidateRef(std::span<const uint8_t> payload);
std::optional<VideoPacketView> ParseVideoPacket(const CommonHeader& h,
    std::span<const uint8_t> payload);
std::optional<FecPacketView> ParseFecPacket(const CommonHeader& h,
    std::span<const uint8_t> payload);
size_t ParseInputEvents(std::span<const uint8_t> payload, uint32_t& firstSeq,
    std::span<InputEvent> out);
std::optional<ClipboardChunkView> ParseClipboardChunk(std::span<const uint8_t> payload);
std::string TruncateClipboardText(std::string_view text);

}
