package com.deskhub.app

import android.os.Bundle
import android.view.ViewGroup
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import kotlinx.coroutines.delay
import kotlin.math.max
import kotlin.time.Duration.Companion.milliseconds

private val TermBackground = Color(0xFF101218)
private val TermCursor = Color(0xE0E0E0E0)

class TerminalActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val address = intent.getStringExtra("addr").orEmpty()
        val passcode = intent.getStringExtra("passcode").orEmpty()
        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                TerminalScreen(address = address, passcode = passcode, onClose = { finish() })
            }
        }
    }
}

@Composable
private fun TerminalScreen(
    address: String,
    passcode: String,
    onClose: () -> Unit,
) {
    var handle by remember { mutableLongStateOf(0L) }
    var state by remember { mutableIntStateOf(0) }
    var message by remember { mutableStateOf("") }
    var grid by remember { mutableStateOf<NativeTerm.Grid?>(null) }
    var scrollOffset by remember { mutableIntStateOf(0) }
    var latchCtrl by remember { mutableStateOf(false) }
    var latchAlt by remember { mutableStateOf(false) }
    var cellWidthPx by remember { mutableIntStateOf(1) }
    var cellHeightPx by remember { mutableIntStateOf(1) }
    var lastRevision by remember { mutableLongStateOf(-1L) }
    val density = LocalDensity.current
    val textMeasurer = rememberTextMeasurer()

    DisposableEffect(address, passcode) {
        val opened =
            NativeTerm.open(
                address = address,
                passcode = passcode,
                cols = 100,
                rows = 30,
            )
        handle = opened
        onDispose {
            NativeTerm.stop(handle)
            handle = 0L
        }
    }

    LaunchedEffect(handle) {
        if (handle == 0L) return@LaunchedEffect
        while (true) {
            state = NativeTerm.state(handle)
            message = NativeTerm.message(handle)
            val snap = NativeTerm.snapshot(handle, scrollOffset)
            if (snap != null) {
                if (snap.revision != lastRevision || snap.scrollOffset != scrollOffset) {
                    grid = snap
                    scrollOffset = snap.scrollOffset
                    lastRevision = snap.revision
                }
            }
            if (state >= NativeTerm.STATE_REFUSED) break
            delay(33.milliseconds)
        }
    }

    Column(modifier = Modifier.fillMaxSize().background(TermBackground)) {
        Box(
            modifier =
                Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .onSizeChanged { size ->
                        val fontPx = with(density) { 13.sp.toPx() }
                        val cols = max(1, (size.width / fontPx * 0.6f).toInt())
                        val rows = max(1, (size.height / fontPx).toInt())
                        cellWidthPx = max(1, size.width / cols)
                        cellHeightPx = max(1, size.height / rows)
                        NativeTerm.resize(handle, cols, rows)
                    }.pointerInput(Unit) {
                        detectDragGestures { change, drag ->
                            change.consume()
                            val rows = (drag.y / cellHeightPx).toInt()
                            if (rows != 0) scrollOffset = (scrollOffset + rows).coerceAtLeast(0)
                        }
                    },
        ) {
            val current = grid
            if (current != null) {
                Canvas(modifier = Modifier.fillMaxSize()) {
                    val cellW = size.width / max(1, current.cols)
                    val cellH = size.height / max(1, current.rows)
                    for (row in 0 until current.rows) {
                        for (col in 0 until current.cols) {
                            val idx = row * current.cols + col
                            if (idx >= current.cells.size) continue
                            val cell = current.cells[idx]
                            val origin = Offset(col * cellW, row * cellH)
                            drawRect(
                                color =
                                    Color(
                                        red = cell.bgR / 255f,
                                        green = cell.bgG / 255f,
                                        blue = cell.bgB / 255f,
                                    ),
                                topLeft = origin,
                                size = Size(cellW, cellH),
                            )
                            if (cell.codepoint == 32) continue
                            val glyph = cell.codepoint.toChar().toString()
                            drawText(
                                textMeasurer = textMeasurer,
                                text = glyph,
                                topLeft = origin,
                                style =
                                    TextStyle(
                                        color =
                                            Color(
                                                red = cell.fgR / 255f,
                                                green = cell.fgG / 255f,
                                                blue = cell.fgB / 255f,
                                            ),
                                        fontSize = 13.sp,
                                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
                                    ),
                            )
                        }
                    }
                    if (current.cursorVisible) {
                        drawRect(
                            color = TermCursor.copy(alpha = 0.6f),
                            topLeft =
                                Offset(
                                    current.cursorCol * cellW,
                                    current.cursorRow * cellH,
                                ),
                            size = Size(cellW, cellH),
                        )
                    }
                }
            }

            AndroidView(
                factory = { context ->
                    KeyInputView(context).apply {
                        layoutParams =
                            ViewGroup.LayoutParams(1, 1)
                        onChar = { code ->
                            scrollOffset = 0
                            when {
                                latchCtrl || latchAlt ->
                                    NativeTerm.sendKey(
                                        handle,
                                        NativeTerm.KEY_CHAR,
                                        codepoint = code,
                                        alt = latchAlt,
                                        ctrl = latchCtrl,
                                    )

                                code == '\n'.code || code == '\r'.code ->
                                    NativeTerm.sendKey(handle, NativeTerm.KEY_ENTER)

                                code == '\b'.code ->
                                    NativeTerm.sendKey(handle, NativeTerm.KEY_BACKSPACE)

                                else -> NativeTerm.sendKey(handle, NativeTerm.KEY_CHAR, codepoint = code)
                            }
                            latchCtrl = false
                            latchAlt = false
                        }
                    }
                },
                modifier = Modifier.fillMaxSize(),
            )
        }

        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState())
                    .padding(horizontal = 8.dp, vertical = 4.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            termKey("Esc") { NativeTerm.sendKey(handle, NativeTerm.KEY_ESCAPE) }
            termKey("Tab") { NativeTerm.sendKey(handle, NativeTerm.KEY_TAB) }
            termKey("Ctrl", latchCtrl) { latchCtrl = !latchCtrl }
            termKey("Alt", latchAlt) { latchAlt = !latchAlt }
            termKey("←") { NativeTerm.sendKey(handle, NativeTerm.KEY_LEFT) }
            termKey("↓") { NativeTerm.sendKey(handle, NativeTerm.KEY_DOWN) }
            termKey("↑") { NativeTerm.sendKey(handle, NativeTerm.KEY_UP) }
            termKey("→") { NativeTerm.sendKey(handle, NativeTerm.KEY_RIGHT) }
            termKey("^C") {
                NativeTerm.sendKey(handle, NativeTerm.KEY_CHAR, codepoint = 'c'.code, ctrl = true)
                latchCtrl = false
                latchAlt = false
            }
        }

        Row(
            modifier = Modifier.fillMaxWidth().padding(8.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text(
                text = message,
                style = MaterialTheme.typography.bodySmall,
                color = Color.LightGray,
                maxLines = 1,
                modifier = Modifier.weight(1f),
            )
            if (scrollOffset > 0) {
                OutlinedButton(onClick = { scrollOffset = 0 }) {
                    Text("↓ $scrollOffset")
                }
            }
            Button(onClick = onClose) { Text(NativeClient.string(NativeClient.STR_DISCONNECT_BUTTON)) }
        }
    }
}

@Composable
private fun termKey(
    label: String,
    active: Boolean = false,
    onClick: () -> Unit,
) {
    OutlinedButton(onClick = onClick) {
        Text(
            text = label,
            color = if (active) Color(0xFF3B82F6) else Color.White,
        )
    }
}
