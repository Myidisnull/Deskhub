package com.deskhub.app

import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.view.Surface
import android.view.WindowManager
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

object NativeClient {
    const val PHASE_IDLE = 0
    const val PHASE_STREAMING = 2
    const val PHASE_ENDED = 3

    init {
        System.loadLibrary("deskhub")
    }

    @Suppress("DEPRECATION")
    fun screenSizePx(context: Context): Pair<Int, Int> {
        val wm =
            context.getSystemService(Context.WINDOW_SERVICE) as? WindowManager
                ?: return 0 to 0
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val b = wm.maximumWindowMetrics.bounds
            b.width() to b.height()
        } else {
            val p = android.graphics.Point()
            wm.defaultDisplay.getRealSize(p)
            p.x to p.y
        }
    }

    const val STR_CLIENT_IP_PROMPT = 3
    const val STR_QUERYING_SOURCES = 12
    const val STR_INVALID_ADDRESS_HINT = 17
    const val STR_SESSION_ENDED = 18
    const val STR_CLIENT_PASSCODE_PROMPT = 21
    const val STR_CLIENT_PASSCODE_HINT = 22
    const val STR_PASSCODE_INVALID = 23
    const val STR_CONNECT_PROMPT_TITLE = 41
    const val STR_PROJECT_URL = 36
    const val STR_PROJECT_LINK_LABEL = 37
    const val STR_CLIENT_HEADING = 33
    const val STR_REQUEST_CONTROL_LABEL = 39
    const val STR_LAN_DEVICES_HEADING = 25
    const val STR_RECENT_DEVICES_HEADING = 26
    const val STR_RECENT_DEVICES_HINT = 27
    const val STR_RECENT_DEVICES_EMPTY = 28
    const val STR_FORGET_DEVICE = 121
    const val STR_SIDEBAR_CLIENT = 30
    const val STR_SIDEBAR_SETTINGS = 31
    const val STR_SIDEBAR_HOST = 29
    const val STR_HOST_HEADING = 32
    const val STR_HOST_IP_INTRO = 1
    const val STR_NO_NETWORK_ADDRESS = 2
    const val STR_SHARING_TITLE = 7
    const val STR_SHARING_CONNECT_HINT = 9
    const val STR_NOTHING_SHARED = 10
    const val STR_STOP_SHARING = 11
    const val STR_SHARE_START_FAILED = 19
    const val STR_PASSCODE_LABEL = 24
    const val STR_SHARE_STATE_ON = 47
    const val STR_SHARE_STATE_OFF = 48
    const val STR_START_SHARING = 49
    const val STR_STARTING_SHARE = 50
    const val STR_DISCONNECT_VIEWER_ACTION = 53
    const val STR_NOT_SHARING = 55
    const val STR_CLIENT_SETTINGS_HEADING = 57
    const val STR_CLIENT_SETTINGS_HINT = 58
    const val STR_REFRESH_NOW = 51
    const val STR_UDP_PORT_LABEL = 59
    const val STR_BIND_INTERFACE_LABEL = 76
    const val STR_BIND_ALL_INTERFACES = 77
    const val STR_CLIPBOARD_SYNC_LABEL = 80
    const val STR_SHARE_AUDIO_LABEL = 127
    const val STR_ACCEPT_FILES_LABEL = 129
    const val STR_SEND_FILES_LABEL = 130
    const val STR_PLAY_AUDIO_LABEL = 128
    const val STR_KEEP_AWAKE_LABEL = 124
    const val STR_BIND_NOT_CONNECTED = 85
    const val STR_SECTION_CONNECTION = 87
    const val STR_SECTION_SESSION = 89
    const val STR_ENCRYPT_SESSION_LABEL = 106
    const val STR_ENCRYPT_SESSION_HINT = 107
    const val STR_SESSION_KEY_LABEL = 108
    const val STR_SESSION_KEY_HINT = 109
    const val STR_COPY_SESSION_KEY = 110
    const val STR_REFRESH_SESSION_KEY = 111
    const val STR_ESCROW_SESSION_KEY_LABEL = 112
    const val STR_ESCROW_SESSION_KEY_HINT = 113
    const val STR_SESSION_KEY_LIFETIME_LABEL = 114
    const val STR_SESSION_KEY_LIFETIME_PER_SHARE = 115
    const val STR_SESSION_KEY_LIFETIME_PERSISTENT = 116
    const val STR_CLIENT_SESSION_KEY_PROMPT = 117
    const val STR_CLIENT_SESSION_KEY_HINT = 118
    const val STR_SESSION_KEY_INVALID = 119
    const val STR_SECTION_LANGUAGE = 105
    const val STR_LANGUAGE_LABEL = 103
    const val STR_LANGUAGE_SYSTEM = 104
    const val STR_LANGUAGE_RESTART_HINT = 120
    const val STR_COPIED = 122
    const val STR_COPY = 123
    const val STR_LOG_MAX_FILE_MB = 91
    const val STR_LOG_COMPRESS_AFTER_DAYS = 92
    const val STR_LOG_DELETE_AFTER_DAYS = 93
    const val STR_DISCONNECT_BUTTON = 131
    const val STR_LINK_REATTACHING = 132
    const val STR_OPEN_DESKTOP_LABEL = 133
    const val STR_OPEN_FILES_LABEL = 134
    const val STR_CONNECTED_PICK_SESSION = 135
    const val STR_OPEN_SHELL_LABEL = 136
    const val STR_SHARE_TERMINAL_LABEL = 137

    const val LINK_QUALITY_UNKNOWN = 0
    const val LINK_QUALITY_GOOD = 1
    const val LINK_QUALITY_FAIR = 2
    const val LINK_QUALITY_POOR = 3

    data class LinkHealth(
        val haveRtt: Boolean = false,
        val rttMs: Int = 0,
        val lossPct: Int = 0,
        val quality: Int = LINK_QUALITY_UNKNOWN,
    )

    private external fun nativeLinkHealth(): IntArray

    fun linkHealth(): LinkHealth {
        val raw = nativeLinkHealth()
        if (raw.size < 4) return LinkHealth()
        return LinkHealth(raw[0] != 0, raw[1], raw[2], raw[3])
    }

    private external fun nativeLinkQualityText(quality: Int): String

    fun linkQualityText(quality: Int): String = nativeLinkQualityText(quality)

    private external fun nativeLinkPingText(
        haveRtt: Boolean,
        rttMs: Int,
    ): String

    fun linkPingText(health: LinkHealth): String = nativeLinkPingText(health.haveRtt, health.rttMs)

    private external fun nativeString(id: Int): String

    private external fun nativeVersionLine(): String

    fun versionLine(): String = nativeVersionLine()

    private external fun nativeUdpPortLine(port: Int): String

    fun udpPortLine(port: Int): String = nativeUdpPortLine(port)

    private external fun nativeComposeAddress(
        host: String,
        portText: String,
    ): String

    fun composeAddress(
        host: String,
        portText: String,
    ): String = nativeComposeAddress(host, portText)

    private external fun nativeAddressHost(addr: String): String

    fun addressHost(addr: String): String = nativeAddressHost(addr)

    private external fun nativeAddressPort(addr: String): Int

    fun addressPort(addr: String): Int = nativeAddressPort(addr)

    private external fun nativeSetDataDir(dir: String)

    fun useAppDataDir(context: Context) {
        nativeSetDataDir(context.filesDir.absolutePath)
    }

    private external fun nativeParseAddress(addr: String): Boolean

    private external fun nativeCouldNotConnect(addr: String): String

    private external fun nativeConnectingTo(addr: String): String

    private external fun nativeSourceQueryFailed(addr: String): String

    private external fun nativeSourceQueryEmpty(addr: String): String

    private external fun nativeHostTitle(
        addr: String,
        width: Int,
        height: Int,
    ): String

    private external fun nativeZoomLabel(zoom: Float): String

    private external fun nativeIsZoomed(zoom: Float): Boolean

    fun string(id: Int): String = nativeString(id)

    fun parseAddress(addr: String): Boolean = nativeParseAddress(addr)

    fun couldNotConnect(addr: String): String = nativeCouldNotConnect(addr)

    fun connectingTo(addr: String): String = nativeConnectingTo(addr)

    fun sourceQueryFailed(addr: String): String = nativeSourceQueryFailed(addr)

    fun sourceQueryEmpty(addr: String): String = nativeSourceQueryEmpty(addr)

    fun hostTitle(
        addr: String,
        width: Int,
        height: Int,
    ): String = nativeHostTitle(addr, width, height)

    fun zoomLabel(zoom: Float): String = nativeZoomLabel(zoom)

    fun isZoomed(zoom: Float): Boolean = nativeIsZoomed(zoom)

    private external fun nativeIsValidPasscode(passcode: String): Boolean

    private external fun nativePasscodeDigits(): Int

    fun isValidPasscode(passcode: String): Boolean = nativeIsValidPasscode(passcode)

    fun passcodeDigits(): Int = nativePasscodeDigits()

    data class ScanHit(
        val addr: String,
        val ping: String,
    )

    data class RecentDevice(
        val addr: String,
        val passcode: String,
        val status: String,
        val ping: String,
        val lastConnected: String,
        val online: Boolean,
    )

    private external fun nativeDefaultPort(): Int

    private external fun nativeClientControl(): Boolean

    private external fun nativeSetClientControl(on: Boolean)

    fun defaultPort(): Int = nativeDefaultPort()

    fun clientControl(): Boolean = nativeClientControl()

    fun setClientControl(on: Boolean) = nativeSetClientControl(on)

    private external fun nativeClipboardSync(): Boolean

    private external fun nativeSetClipboardSync(on: Boolean)

    fun clipboardSync(): Boolean = nativeClipboardSync()

    fun setClipboardSync(on: Boolean) = nativeSetClipboardSync(on)

    private external fun nativeShareAudio(): Boolean

    private external fun nativeSetShareAudio(on: Boolean)

    private external fun nativePlayAudio(): Boolean

    private external fun nativeSetPlayAudio(on: Boolean)

    fun shareAudio(): Boolean = nativeShareAudio()

    fun setShareAudio(on: Boolean) = nativeSetShareAudio(on)

    private external fun nativeAcceptFiles(): Boolean

    private external fun nativeSetAcceptFiles(on: Boolean)

    fun acceptFiles(): Boolean = nativeAcceptFiles()

    fun setAcceptFiles(on: Boolean) = nativeSetAcceptFiles(on)

    private external fun nativeShareTerminal(): Boolean

    private external fun nativeSetShareTerminal(on: Boolean)

    fun shareTerminal(): Boolean = nativeShareTerminal()

    fun setShareTerminal(on: Boolean) = nativeSetShareTerminal(on)

    fun playAudio(): Boolean = nativePlayAudio()

    fun setPlayAudio(on: Boolean) = nativeSetPlayAudio(on)

    private external fun nativeKeepAwake(): Boolean

    private external fun nativeSetKeepAwake(on: Boolean)

    fun keepAwake(): Boolean = nativeKeepAwake()

    fun setKeepAwake(on: Boolean) = nativeSetKeepAwake(on)

    private external fun nativeLanguage(): String

    private external fun nativeSetLanguage(code: String)

    fun language(): String = nativeLanguage()

    fun setLanguage(code: String) = nativeSetLanguage(code)

    private external fun nativeEncryptSession(): Boolean

    private external fun nativeSetEncryptSession(on: Boolean)

    fun encryptSession(): Boolean = nativeEncryptSession()

    fun setEncryptSession(on: Boolean) = nativeSetEncryptSession(on)

    private external fun nativeEscrowSessionKey(): Boolean

    private external fun nativeSetEscrowSessionKey(on: Boolean)

    fun escrowSessionKey(): Boolean = nativeEscrowSessionKey()

    fun setEscrowSessionKey(on: Boolean) = nativeSetEscrowSessionKey(on)

    private external fun nativeSessionKeyLifetime(): Int

    private external fun nativeSetSessionKeyLifetime(lifetime: Int)

    fun sessionKeyLifetime(): Int = nativeSessionKeyLifetime()

    fun setSessionKeyLifetime(lifetime: Int) = nativeSetSessionKeyLifetime(lifetime)

    private external fun nativeSessionKeyHex(): String

    private external fun nativeEnsureSessionKey(refresh: Boolean): Boolean

    private external fun nativeIsValidSessionKey(hex: String): Boolean

    fun sessionKeyHex(): String = nativeSessionKeyHex()

    fun ensureSessionKey(refresh: Boolean): Boolean = nativeEnsureSessionKey(refresh)

    fun isValidSessionKey(hex: String): Boolean = nativeIsValidSessionKey(hex)

    private external fun nativeClipOffer(text: String)

    private external fun nativeClipTake(): String

    fun clipOffer(text: String) = nativeClipOffer(text)

    fun clipTake(): String = nativeClipTake()

    private external fun nativeFileSend(paths: Array<String>): Boolean

    private external fun nativeFileCancel()

    private external fun nativeFileBusy(): Boolean

    private external fun nativeFileError(): String

    fun fileSend(paths: Array<String>): Boolean = nativeFileSend(paths)

    fun fileCancel() = nativeFileCancel()

    fun fileBusy(): Boolean = nativeFileBusy()

    fun fileError(): String = nativeFileError()

    private external fun nativeDeviceName(): String

    private external fun nativeSetDeviceName(name: String)

    fun deviceName(): String = nativeDeviceName()

    fun setDeviceName(name: String) = nativeSetDeviceName(name)

    private external fun nativeSettingsPort(): Int

    private external fun nativeSetSettingsPort(port: Int)

    fun settingsPort(): Int = nativeSettingsPort()

    fun setSettingsPort(port: Int) = nativeSetSettingsPort(port)

    private external fun nativeLogMaxFileMb(): Int

    private external fun nativeLogCompressAfterDays(): Int

    private external fun nativeLogDeleteAfterDays(): Int

    private external fun nativeSetLogPolicy(
        maxFileMb: Int,
        compressAfterDays: Int,
        deleteAfterDays: Int,
    )

    fun logMaxFileMb(): Int = nativeLogMaxFileMb()

    fun logCompressAfterDays(): Int = nativeLogCompressAfterDays()

    fun logDeleteAfterDays(): Int = nativeLogDeleteAfterDays()

    fun setLogPolicy(
        maxFileMb: Int,
        compressAfterDays: Int,
        deleteAfterDays: Int,
    ) = nativeSetLogPolicy(maxFileMb, compressAfterDays, deleteAfterDays)

    private external fun nativeScanStart(port: Int): Boolean

    private external fun nativeScanRestart(port: Int): Boolean

    private external fun nativeRescanSeconds(): Int

    private external fun nativeStatusRefreshNow()

    private external fun nativeRecentNote(): String

    private external fun nativeScanCancel()

    private external fun nativeScanRunning(): Boolean

    private external fun nativeScanStatusText(port: Int): String

    private external fun nativeScanHits(): Array<ScanHit>

    private external fun nativeRecentDevices(): Array<RecentDevice>

    private external fun nativeRecentTouch(
        addr: String,
        passcode: String,
    )

    private external fun nativeRecentTouchEx(
        addr: String,
        passcode: String,
        encrypted: Boolean,
        sessionKey: String,
    )

    private external fun nativeRecentPasscode(addr: String): String

    private external fun nativeRecentSessionKey(addr: String): String

    private external fun nativeRecentEncrypted(addr: String): Boolean

    private external fun nativeRecentRemove(addr: String)

    private external fun nativeWatchRecent()

    fun scanStart(port: Int): Boolean = nativeScanStart(port)

    suspend fun scanRestart(port: Int): Boolean = withContext(Dispatchers.IO) { nativeScanRestart(port) }

    fun rescanSeconds(): Int = nativeRescanSeconds()

    suspend fun statusRefreshNow() = withContext(Dispatchers.IO) { nativeStatusRefreshNow() }

    fun recentNote(): String = nativeRecentNote()

    fun scanCancel() = nativeScanCancel()

    fun scanRunning(): Boolean = nativeScanRunning()

    fun scanStatusText(port: Int): String = nativeScanStatusText(port)

    fun scanHits(): List<ScanHit> = nativeScanHits().toList()

    suspend fun recentDevices(): List<RecentDevice> = withContext(Dispatchers.IO) { nativeRecentDevices().toList() }

    suspend fun recentTouch(
        addr: String,
        passcode: String,
        encrypted: Boolean = false,
        sessionKey: String = "",
    ) = withContext(Dispatchers.IO) {
        if (encrypted || sessionKey.isNotEmpty()) {
            nativeRecentTouchEx(addr, passcode, encrypted, sessionKey)
        } else {
            nativeRecentTouch(addr, passcode)
        }
    }

    fun recentPasscode(addr: String): String = nativeRecentPasscode(addr)

    fun recentSessionKey(addr: String): String = nativeRecentSessionKey(addr)

    fun recentEncrypted(addr: String): Boolean = nativeRecentEncrypted(addr)

    suspend fun recentRemove(addr: String) =
        withContext(Dispatchers.IO) {
            nativeRecentRemove(addr)
            nativeWatchRecent()
        }

    suspend fun watchRecent() = withContext(Dispatchers.IO) { nativeWatchRecent() }

    external fun nativeStart(
        addr: String,
        sourceId: Int,
        screenW: Int,
        screenH: Int,
        passcode: String,
        sessionKey: String = "",
    ): Long

    external fun nativeStop(handle: Long)

    interface SessionListener {
        fun onStatus(
            line: String,
            phase: Int,
        )

        fun onSize(
            width: Int,
            height: Int,
        )

        fun onEnded(reason: String)
    }

    @Volatile
    var sessionListener: SessionListener? = null

    private val mainHandler = Handler(Looper.getMainLooper())

    @JvmStatic
    @JvmName("onSessionStatus")
    internal fun onSessionStatus(
        line: String,
        phase: Int,
    ) {
        mainHandler.post { sessionListener?.onStatus(line, phase) }
    }

    @JvmStatic
    @JvmName("onSessionSize")
    internal fun onSessionSize(
        width: Int,
        height: Int,
    ) {
        mainHandler.post { sessionListener?.onSize(width, height) }
    }

    @JvmStatic
    @JvmName("onSessionEnded")
    internal fun onSessionEnded(reason: String) {
        mainHandler.post { sessionListener?.onEnded(reason) }
    }

    external fun nativeSetSurface(surface: Surface?)

    external fun nativeReleaseSurface(surface: Surface)

    const val MOUSE_LEFT = 1
    const val MOUSE_RIGHT = 2

    private external fun nativeKey(
        vk: Int,
        scan: Int,
        down: Boolean,
    )

    private external fun nativeVkScancode(vk: Int): Int

    private external fun nativeKeyToVk(keyCode: Int): Int

    private external fun nativeConnectDecision(sourceIds: IntArray): Int

    private external fun nativeMouseMove(
        nx: Int,
        ny: Int,
    )

    private external fun nativeMouseButton(
        button: Int,
        down: Boolean,
    )

    private external fun nativeHotkey(
        vk: Int,
        scan: Int,
        modVk: Int,
        modScan: Int,
    )

    private external fun nativeMouseWheel(notches: Int)

    private external fun nativeCharTap(codepoint: Int)

    private external fun nativeReleaseAllInput()

    fun key(
        vk: Int,
        scan: Int,
        down: Boolean,
    ) {
        nativeKey(vk, scan, down)
    }

    fun vkScancode(vk: Int): Int = nativeVkScancode(vk)

    fun keyToVk(keyCode: Int): Int = nativeKeyToVk(keyCode)

    fun connectDecision(sources: List<Source>): Int = nativeConnectDecision(sources.map { it.id }.toIntArray())

    fun mouseMove(
        nx: Int,
        ny: Int,
    ) {
        nativeMouseMove(nx, ny)
    }

    fun mouseButton(
        button: Int,
        down: Boolean,
    ) {
        nativeMouseButton(button, down)
    }

    fun hotkey(hotkey: Hotkey) {
        nativeHotkey(hotkey.vk, hotkey.scan, hotkey.modVk, hotkey.modScan)
    }

    fun mouseWheel(notches: Int) {
        nativeMouseWheel(notches)
    }

    fun charTap(codepoint: Int) {
        nativeCharTap(codepoint)
    }

    fun releaseAllInput() {
        nativeReleaseAllInput()
    }

    external fun nativeVideoFrame(
        viewportW: Float,
        viewportH: Float,
        aspect: Float,
        zoom: Float,
        panX: Float,
        panY: Float,
    ): FloatArray

    private external fun nativeTakeScrollNotches(
        dragPoints: Float,
        carry: DoubleArray,
    ): Int

    fun takeScrollNotches(
        dragPoints: Float,
        carry: DoubleArray,
    ): Int = nativeTakeScrollNotches(dragPoints, carry)

    private external fun nativeCursorClamp(
        cx: Float,
        cy: Float,
        rectX: Float,
        rectY: Float,
        rectW: Float,
        rectH: Float,
        viewportW: Float,
        viewportH: Float,
    ): FloatArray

    private external fun nativeCursorMove(
        cx: Float,
        cy: Float,
        dx: Float,
        dy: Float,
        rectX: Float,
        rectY: Float,
        rectW: Float,
        rectH: Float,
        viewportW: Float,
        viewportH: Float,
    ): FloatArray

    private external fun nativeCursorPoint(
        cx: Float,
        cy: Float,
        rectX: Float,
        rectY: Float,
        rectW: Float,
        rectH: Float,
    ): FloatArray

    private external fun nativeCursorNormalize(
        cx: Float,
        cy: Float,
        rectX: Float,
        rectY: Float,
        rectW: Float,
        rectH: Float,
    ): IntArray

    data class Cursor(
        val x: Float = 0.5f,
        val y: Float = 0.5f,
    )

    fun cursorClamped(
        cursor: Cursor,
        video: Rect,
        viewport: Size,
    ): Cursor =
        nativeCursorClamp(
            cursor.x,
            cursor.y,
            video.left,
            video.top,
            video.width,
            video.height,
            viewport.width,
            viewport.height,
        ).let { Cursor(it[0], it[1]) }

    fun cursorMoved(
        cursor: Cursor,
        delta: Offset,
        video: Rect,
        viewport: Size,
    ): Cursor =
        nativeCursorMove(
            cursor.x,
            cursor.y,
            delta.x,
            delta.y,
            video.left,
            video.top,
            video.width,
            video.height,
            viewport.width,
            viewport.height,
        ).let { Cursor(it[0], it[1]) }

    fun cursorScreenPoint(
        cursor: Cursor,
        video: Rect,
    ): Offset? =
        nativeCursorPoint(
            cursor.x,
            cursor.y,
            video.left,
            video.top,
            video.width,
            video.height,
        ).takeIf { it.size == 2 }?.let { Offset(it[0], it[1]) }

    fun cursorMouseMove(
        cursor: Cursor,
        video: Rect,
    ) {
        val n =
            nativeCursorNormalize(
                cursor.x,
                cursor.y,
                video.left,
                video.top,
                video.width,
                video.height,
            )
        if (n.size == 2) mouseMove(n[0], n[1])
    }

    private external fun nativeApplyGesture(
        zoom: Float,
        panX: Float,
        panY: Float,
        factor: Float,
        centroidX: Float,
        centroidY: Float,
        panDeltaX: Float,
        panDeltaY: Float,
        viewportW: Float,
        viewportH: Float,
        aspect: Float,
    ): FloatArray

    data class Transform(
        val zoom: Float,
        val panX: Float,
        val panY: Float,
    )

    fun applyGesture(
        current: Transform,
        factor: Float,
        centroidX: Float,
        centroidY: Float,
        panDeltaX: Float,
        panDeltaY: Float,
        viewportW: Float,
        viewportH: Float,
        aspect: Float,
    ): Transform {
        val r =
            nativeApplyGesture(
                current.zoom,
                current.panX,
                current.panY,
                factor,
                centroidX,
                centroidY,
                panDeltaX,
                panDeltaY,
                viewportW,
                viewportH,
                aspect,
            )
        return Transform(r[0], r[1], r[2])
    }

    private external fun nativeHotkeys(): Array<Hotkey>

    data class Hotkey(
        val label: String,
        val vk: Int,
        val scan: Int,
        val modVk: Int,
        val modScan: Int,
    )

    val hotkeys: List<Hotkey> by lazy { nativeHotkeys().toList() }

    data class Source(
        val id: Int,
        val displayName: String,
        val sizeLabel: String,
    )

    data class HostCaps(
        val acceptsInput: Boolean = false,
        val terminal: Boolean = false,
        val audio: Boolean = false,
        val files: Boolean = false,
    )

    data class HostQuery(
        val sources: List<Source>,
        val caps: HostCaps,
    )

    private external fun nativeLinkStatusPrefix(): String

    fun linkStatusPrefix(): String = nativeLinkStatusPrefix()

    fun composeStatusLine(raw: String): String {
        val prefix = linkStatusPrefix()
        return when {
            prefix.isEmpty() -> raw
            raw.isEmpty() -> prefix
            else -> "$prefix · $raw"
        }
    }

    data class Snapshot(
        val phase: Int,
        val statusLine: String,
        val endReason: String,
        val videoWidth: Int,
        val videoHeight: Int,
    )

    external fun nativeSnapshot(): Snapshot?

    private external fun nativeListSources(
        addr: String,
        passcode: String,
    ): Array<Any>?

    suspend fun listSources(
        addr: String,
        passcode: String,
    ): HostQuery? =
        withContext(Dispatchers.IO) {
            val raw = nativeListSources(addr, passcode) ?: return@withContext null
            if (raw.size < 2) return@withContext null
            val capsArr = raw[0] as? BooleanArray ?: return@withContext null

            @Suppress("UNCHECKED_CAST")
            val sources = (raw[1] as? Array<Source>)?.toList() ?: emptyList()
            val caps =
                HostCaps(
                    acceptsInput = capsArr.getOrElse(0) { false },
                    terminal = capsArr.getOrElse(1) { false },
                    audio = capsArr.getOrElse(2) { false },
                    files = capsArr.getOrElse(3) { false },
                )
            HostQuery(sources, caps)
        }
}
