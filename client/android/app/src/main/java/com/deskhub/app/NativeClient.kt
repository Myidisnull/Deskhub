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
    const val PHASE_CONNECTING = 1
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

    const val STR_SESSION_ENDED = 18

    private external fun nativeString(id: Int): String

    private external fun nativeConnectingTo(addr: String): String

    private external fun nativeHostTitle(
        addr: String,
        width: Int,
        height: Int,
    ): String

    private external fun nativeZoomLabel(zoom: Float): String

    private external fun nativeIsZoomed(zoom: Float): Boolean

    fun string(id: Int): String = nativeString(id)

    fun connectingTo(addr: String): String = nativeConnectingTo(addr)

    fun hostTitle(
        addr: String,
        width: Int,
        height: Int,
    ): String = nativeHostTitle(addr, width, height)

    fun zoomLabel(zoom: Float): String = nativeZoomLabel(zoom)

    fun isZoomed(zoom: Float): Boolean = nativeIsZoomed(zoom)

    private external fun nativeListSources(addr: String): Array<Source>

    external fun nativeStart(
        addr: String,
        sourceId: Int,
        screenW: Int,
        screenH: Int,
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

    private external fun nativeKeyTap(
        vk: Int,
        scan: Int,
    )

    private external fun nativeKeyChord(
        modVk: Int,
        modScan: Int,
        vk: Int,
        scan: Int,
    )

    private external fun nativeMouseMove(
        nx: Int,
        ny: Int,
    )

    private external fun nativeMouseMoveRel(
        dx: Int,
        dy: Int,
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

    fun keyTap(
        vk: Int,
        scan: Int,
    ) {
        nativeKeyTap(vk, scan)
    }

    fun keyChord(
        modVk: Int,
        modScan: Int,
        vk: Int,
        scan: Int,
    ) {
        nativeKeyChord(modVk, modScan, vk, scan)
    }

    fun mouseMove(
        nx: Int,
        ny: Int,
    ) {
        nativeMouseMove(nx, ny)
    }

    fun mouseMoveRel(
        dx: Int,
        dy: Int,
    ) {
        nativeMouseMoveRel(dx, dy)
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

    external fun nativePhase(): Int

    external fun nativeStatusLine(): String

    external fun nativeEndReason(): String

    external fun nativeVideoWidth(): Int

    external fun nativeVideoHeight(): Int

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
        val width: Int,
        val height: Int,
        val name: String,
        val displayName: String,
        val sizeLabel: String,
        val pickerLabel: String,
    )

    suspend fun listSources(addr: String): List<Source> =
        withContext(Dispatchers.IO) { nativeListSources(addr).toList() }
}
