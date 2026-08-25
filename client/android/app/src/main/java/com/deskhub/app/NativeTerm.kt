package com.deskhub.app

object NativeTerm {
    const val STATE_DECIDING = 2
    const val STATE_LIVE = 4
    const val STATE_REFUSED = 6
    const val STATE_ENDED = 8

    const val KEY_CHAR = 0
    const val KEY_ENTER = 1
    const val KEY_BACKSPACE = 2
    const val KEY_TAB = 3
    const val KEY_ESCAPE = 4
    const val KEY_UP = 5
    const val KEY_DOWN = 6
    const val KEY_RIGHT = 7
    const val KEY_LEFT = 8
    const val KEY_HOME = 9
    const val KEY_END = 10
    const val KEY_PAGE_UP = 11
    const val KEY_PAGE_DOWN = 12
    const val KEY_DELETE = 14

    data class Grid(
        val rows: Int,
        val cols: Int,
        val cursorRow: Int,
        val cursorCol: Int,
        val cursorVisible: Boolean,
        val scrollbackRows: Int,
        val scrollOffset: Int,
        val revision: Long,
        val cells: List<Cell>,
    )

    data class Cell(
        val codepoint: Int,
        val fgR: Int,
        val fgG: Int,
        val fgB: Int,
        val bgR: Int,
        val bgG: Int,
        val bgB: Int,
        val attrs: Int,
    )

    fun open(
        address: String,
        passcode: String,
        cols: Int,
        rows: Int,
    ): Long = nativeOpen(address, passcode, cols, rows)

    fun stop(handle: Long) {
        if (handle != 0L) nativeStop(handle)
    }

    fun state(handle: Long): Int = if (handle == 0L) 0 else nativeState(handle)

    fun message(handle: Long): String = if (handle == 0L) "" else nativeMessage(handle)

    fun snapshot(
        handle: Long,
        scrollOffset: Int,
    ): Grid? {
        if (handle == 0L) return null
        val raw = nativeSnapshot(handle, scrollOffset) ?: return null
        if (raw.size < 10) return null
        val rows = raw[0]
        val cols = raw[1]
        val cellCount = raw[9]
        val cells = ArrayList<Cell>(cellCount)
        var i = 10
        repeat(cellCount) {
            if (i + 7 >= raw.size) return@repeat
            cells +=
                Cell(
                    codepoint = raw[i],
                    fgR = raw[i + 1],
                    fgG = raw[i + 2],
                    fgB = raw[i + 3],
                    bgR = raw[i + 4],
                    bgG = raw[i + 5],
                    bgB = raw[i + 6],
                    attrs = raw[i + 7],
                )
            i += 8
        }
        return Grid(
            rows = rows,
            cols = cols,
            cursorRow = raw[2],
            cursorCol = raw[3],
            cursorVisible = raw[4] != 0,
            scrollbackRows = raw[5],
            scrollOffset = raw[6],
            revision = (raw[7].toLong() shl 32) or (raw[8].toLong() and 0xffffffffL),
            cells = cells,
        )
    }

    fun sendKey(
        handle: Long,
        key: Int,
        codepoint: Int = 0,
        shift: Boolean = false,
        alt: Boolean = false,
        ctrl: Boolean = false,
    ) {
        if (handle != 0L) nativeSendKey(handle, key, codepoint, shift, alt, ctrl)
    }

    fun sendText(
        handle: Long,
        text: String,
    ) {
        if (handle != 0L && text.isNotEmpty()) nativeSendText(handle, text)
    }

    fun resize(
        handle: Long,
        cols: Int,
        rows: Int,
    ) {
        if (handle != 0L) nativeResize(handle, cols, rows)
    }

    private external fun nativeOpen(
        address: String,
        passcode: String,
        cols: Int,
        rows: Int,
    ): Long

    private external fun nativeStop(handle: Long)

    private external fun nativeState(handle: Long): Int

    private external fun nativeMessage(handle: Long): String

    private external fun nativeSnapshot(
        handle: Long,
        scrollOffset: Int,
    ): IntArray?

    private external fun nativeSendKey(
        handle: Long,
        key: Int,
        codepoint: Int,
        shift: Boolean,
        alt: Boolean,
        ctrl: Boolean,
    )

    private external fun nativeSendText(
        handle: Long,
        text: String,
    )

    private external fun nativeResize(
        handle: Long,
        cols: Int,
        rows: Int,
    )
}
