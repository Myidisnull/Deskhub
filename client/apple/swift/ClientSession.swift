import AVFoundation

nonisolated enum Phase: Int, Sendable {
    case idle = 0
    case connecting = 1
    case streaming = 2
    case ended = 3
}

nonisolated enum MouseButton: Int32, Sendable {
    case left = 1
    case right = 2
    case middle = 3
    case x1 = 4
    case x2 = 5
}

struct Source: Identifiable, Sendable, Hashable {
    let id: UInt8
    let name: String
    let displayName: String
    let sizeLabel: String
    let pickerLabel: String
}

nonisolated enum DeskhubClient {
    static func string(_ id: DHStringId) -> String {
        String(cString: dh_string(id))
    }

    static func connectingTo(_ address: String) -> String {
        buffered(320) { dh_connecting_to(address, $0, $1) }
    }

    static func couldNotConnect(_ address: String) -> String {
        buffered(320) { dh_could_not_connect(address, $0, $1) }
    }

    static func hostTitle(_ address: String, width: UInt32, height: UInt32) -> String {
        buffered(320) { dh_host_title(address, width, height, $0, $1) }
    }

    static func zoomLabel(_ zoom: Double) -> String {
        buffered(32) { dh_zoom_label(zoom, $0, $1) }
    }

    static func isZoomed(_ zoom: Double) -> Bool {
        dh_is_zoomed(zoom)
    }

    static func buffered(
        _ capacity: Int, _ fill: (UnsafeMutablePointer<CChar>, Int32) -> Int32
    ) -> String {
        var buf = [CChar](repeating: 0, count: capacity)
        _ = fill(&buf, Int32(capacity))
        return String(cString: buf)
    }

    static func viewerBaseTitle(_ sourceName: String) -> String {
        buffered(320) { dh_viewer_base_title(sourceName, $0, $1) }
    }

    static func pointerSubtitle(locked: Bool, statusLine: String) -> String {
        buffered(320) { dh_pointer_subtitle(DHPointerLock(locked: locked), statusLine, $0, $1) }
    }

    static func ffiList<Raw, Item>(
        _ capacity: Int, _ empty: Raw,
        _ fill: (UnsafeMutablePointer<Raw>?, Int32) -> Int32,
        _ transform: (Raw) -> Item
    ) -> [Item] {
        var buf = [Raw](repeating: empty, count: capacity)
        let count = buf.withUnsafeMutableBufferPointer { ptr in
            fill(ptr.baseAddress, Int32(ptr.count))
        }
        guard count > 0 else { return [] }
        return (0 ..< Int(count)).map { transform(buf[$0]) }
    }

    static func cString(_ tuple: some Any) -> String {
        withUnsafeBytes(of: tuple) { rawBuf in
            guard let base = rawBuf.baseAddress else { return "" }
            return String(cString: base.assumingMemoryBound(to: CChar.self))
        }
    }

    static func normalizedAddress(_ raw: String) -> String? {
        let addr = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !addr.isEmpty, dh_parse_address(addr) else { return nil }
        return addr
    }

    static func connectDecision(_ sources: [Source]) -> (showPicker: Bool, sourceId: UInt8) {
        let infos = sources.map { source -> DHSourceInfo in
            var info = DHSourceInfo()
            info.sourceId = source.id
            return info
        }
        var sourceId: UInt8 = 0
        let showPicker = infos.withUnsafeBufferPointer {
            dh_connect_decision($0.baseAddress, Int32($0.count), &sourceId)
        }
        return (showPicker, sourceId)
    }

    static func listSources(address: String) -> [Source] {
        ffiList(16, DHSourceInfo(), { dh_list_sources(address, $0, $1) }) { info in
            Source(
                id: info.sourceId,
                name: cString(info.name),
                displayName: cString(info.displayName),
                sizeLabel: cString(info.sizeLabel),
                pickerLabel: cString(info.pickerLabel)
            )
        }
    }
}

struct SessionHandlers: Sendable {
    var onStatus: @Sendable (String) -> Void = { _ in }
    var onSize: @Sendable (UInt32, UInt32) -> Void = { _, _ in }
    var onClosed: @Sendable (String) -> Void = { _ in }
}

private final class HandlerBox {
    let handlers: SessionHandlers

    init(_ handlers: SessionHandlers) {
        self.handlers = handlers
    }

    static func unwrap(_ user: UnsafeMutableRawPointer?) -> SessionHandlers? {
        guard let user else { return nil }
        return Unmanaged<HandlerBox>.fromOpaque(user).takeUnretainedValue().handlers
    }
}

final class ClientSession: @unchecked Sendable {
    private let handle: OpaquePointer
    private let handlerBox: UnsafeMutableRawPointer

    private init(handle: OpaquePointer, handlerBox: UnsafeMutableRawPointer) {
        self.handle = handle
        self.handlerBox = handlerBox
    }

    static func start(
        address: String, sourceId: UInt8, handlers: SessionHandlers
    ) -> ClientSession? {
        let box = Unmanaged.passRetained(HandlerBox(handlers)).toOpaque()

        var callbacks = DHSessionCallbacks()
        callbacks.user = box
        callbacks.onStatus = { line, user in
            HandlerBox.unwrap(user)?.onStatus(line.map { String(cString: $0) } ?? "")
        }
        callbacks.onSize = { width, height, user in
            HandlerBox.unwrap(user)?.onSize(width, height)
        }
        callbacks.onClosed = { reason, user in
            HandlerBox.unwrap(user)?.onClosed(reason.map { String(cString: $0) } ?? "")
        }

        guard let handle = dh_session_start(address, sourceId, nil, &callbacks) else {
            Unmanaged<HandlerBox>.fromOpaque(box).release()
            return nil
        }
        return ClientSession(handle: handle, handlerBox: box)
    }

    func stop() {
        dh_session_stop(handle)
        Unmanaged<HandlerBox>.fromOpaque(handlerBox).release()
    }

    func setLayer(_ layer: AVSampleBufferDisplayLayer?) {
        let ptr = layer.map { Unmanaged.passUnretained($0).toOpaque() }
        dh_session_set_layer(handle, ptr)
    }

    func key(vk: Int32, scan: Int32, down: Bool) {
        dh_session_key(handle, vk, scan, down)
    }

    func hotkey(_ hotkey: Hotkey) {
        dh_session_hotkey(handle, hotkey.vk, hotkey.scan, hotkey.modVk, hotkey.modScan)
    }

    func charTap(_ codepoint: UInt32) {
        dh_session_char_tap(handle, codepoint)
    }

    func releaseAllInput() {
        dh_session_release_all_input(handle)
    }

    func mouseMove(nx: Int32, ny: Int32) {
        dh_session_mouse_move(handle, nx, ny)
    }

    func mouseMoveRel(dx: Int32, dy: Int32) {
        dh_session_mouse_move_rel(handle, dx, dy)
    }

    func mouseButton(_ button: MouseButton, down: Bool) {
        dh_session_mouse_button(handle, button.rawValue, down)
    }

    func mouseWheelNotches(_ notches: Int32) {
        dh_session_mouse_wheel_notches(handle, notches)
    }

    struct Snapshot {
        let phase: Phase
        let statusLine: String
        let endReason: String
        let videoWidth: UInt32
        let videoHeight: UInt32
    }

    func snapshot() -> Snapshot {
        var raw = DHSessionState()
        dh_session_snapshot(handle, &raw)
        return Snapshot(
            phase: Phase(rawValue: Int(raw.phase.rawValue)) ?? .idle,
            statusLine: DeskhubClient.cString(raw.statusLine),
            endReason: DeskhubClient.cString(raw.endReason),
            videoWidth: raw.videoWidth,
            videoHeight: raw.videoHeight
        )
    }

    func phase() -> Phase {
        Phase(rawValue: Int(dh_session_phase(handle).rawValue)) ?? .idle
    }
}
