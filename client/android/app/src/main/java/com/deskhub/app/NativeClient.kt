package com.deskhub.app

import android.content.Context
import android.os.Build
import android.view.Surface
import android.view.WindowManager
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

    private external fun nativeListSources(addr: String): Array<String>

    external fun nativeStart(
        addr: String,
        sourceId: Int,
        screenW: Int,
        screenH: Int,
    ): Long

    external fun nativeStop(generation: Long)

    external fun nativeSetSurface(surface: Surface?)

    external fun nativeReleaseSurface(surface: Surface)

    const val MOUSE_LEFT = 1
    const val MOUSE_RIGHT = 2

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

    private external fun nativeCharTap(codepoint: Int)

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

    fun charTap(codepoint: Int) {
        nativeCharTap(codepoint)
    }

    external fun nativePhase(): Int

    external fun nativeStatusLine(): String

    external fun nativeEndReason(): String

    external fun nativeVideoWidth(): Int

    external fun nativeVideoHeight(): Int

    data class Source(
        val id: Int,
        val width: Int,
        val height: Int,
        val name: String,
    )

    suspend fun listSources(addr: String): List<Source> =
        withContext(Dispatchers.IO) {
            nativeListSources(addr).mapNotNull { line ->
                val f = line.split('\t', limit = 4)
                if (f.size < 4) return@mapNotNull null
                val id = f[0].toIntOrNull() ?: return@mapNotNull null
                Source(id, f[1].toIntOrNull() ?: 0, f[2].toIntOrNull() ?: 0, f[3])
            }
        }
}
