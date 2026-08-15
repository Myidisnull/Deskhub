package com.deskhub.app

import android.content.Context
import android.graphics.Paint
import android.graphics.Typeface
import android.os.Bundle
import android.text.InputType
import android.view.KeyEvent
import android.view.View
import android.view.WindowManager
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.view.inputmethod.InputMethodManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import kotlinx.coroutines.delay
import kotlin.math.max

private const val TERM_POLL_MS = 33L
private const val DEFAULT_COLS = 80
private const val DEFAULT_ROWS = 24
private const val DEFAULT_BG = 0xFF101218.toInt()
private const val CURSOR_COLOR = 0xFFE0E0E0.toInt()

class TerminalActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        NativeClient.useAppDataDir(this)
        if (NativeClient.keepAwake()) {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }

        val addr = intent.getStringExtra("addr").orEmpty()
        val passcode = intent.getStringExtra("passcode").orEmpty()
        val opened = NativeTerminal.open(addr, passcode, DEFAULT_COLS, DEFAULT_ROWS)

        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                TerminalScreen(
                    address = addr,
                    opened = opened,
                    onDismiss = { finish() },
                )
            }
        }
    }

    override fun onDestroy() {
        NativeTerminal.stop()
        super.onDestroy()
    }
}

private class TermInputView(
    context: Context,
) : View(context) {
    var onChar: ((Int) -> Unit)? = null
    var onSpecial: ((Int) -> Unit)? = null

    init {
        isFocusable = true
        isFocusableInTouchMode = true
    }

    override fun onCheckIsTextEditor(): Boolean = true

    override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection {
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT or
            InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD or
            InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        outAttrs.imeOptions = EditorInfo.IME_ACTION_NONE or
            EditorInfo.IME_FLAG_NO_FULLSCREEN or
            EditorInfo.IME_FLAG_NO_EXTRACT_UI

        return object : BaseInputConnection(this, false) {
            override fun commitText(
                text: CharSequence,
                newCursorPosition: Int,
            ): Boolean {
                for (ch in text) onChar?.invoke(ch.code)
                return true
            }

            override fun deleteSurroundingText(
                beforeLength: Int,
                afterLength: Int,
            ): Boolean {
                repeat(beforeLength) { onSpecial?.invoke(NativeTerminal.KEY_BACKSPACE) }
                return true
            }
        }
    }

    override fun onKeyDown(
        keyCode: Int,
        event: KeyEvent,
    ): Boolean {
        specialFor(keyCode)?.let { key ->
            onSpecial?.invoke(key)
            return true
        }
        val ch = event.unicodeChar
        if (ch != 0) {
            onChar?.invoke(ch)
            return true
        }
        return super.onKeyDown(keyCode, event)
    }

    private fun specialFor(keyCode: Int): Int? =
        when (keyCode) {
            KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> NativeTerminal.KEY_ENTER
            KeyEvent.KEYCODE_DEL -> NativeTerminal.KEY_BACKSPACE
            KeyEvent.KEYCODE_TAB -> NativeTerminal.KEY_TAB
            KeyEvent.KEYCODE_ESCAPE -> NativeTerminal.KEY_ESCAPE
            KeyEvent.KEYCODE_DPAD_UP -> NativeTerminal.KEY_UP
            KeyEvent.KEYCODE_DPAD_DOWN -> NativeTerminal.KEY_DOWN
            KeyEvent.KEYCODE_DPAD_RIGHT -> NativeTerminal.KEY_RIGHT
            KeyEvent.KEYCODE_DPAD_LEFT -> NativeTerminal.KEY_LEFT
            KeyEvent.KEYCODE_MOVE_HOME -> NativeTerminal.KEY_HOME
            KeyEvent.KEYCODE_MOVE_END -> NativeTerminal.KEY_END
            KeyEvent.KEYCODE_PAGE_UP -> NativeTerminal.KEY_PAGE_UP
            KeyEvent.KEYCODE_PAGE_DOWN -> NativeTerminal.KEY_PAGE_DOWN
            KeyEvent.KEYCODE_INSERT -> NativeTerminal.KEY_INSERT
            KeyEvent.KEYCODE_FORWARD_DEL -> NativeTerminal.KEY_DELETE
            in KeyEvent.KEYCODE_F1..KeyEvent.KEYCODE_F12 ->
                NativeTerminal.KEY_F1 + (keyCode - KeyEvent.KEYCODE_F1)
            else -> null
        }
}

@Composable
private fun TerminalScreen(
    address: String,
    opened: Boolean,
    onDismiss: () -> Unit,
) {
    var grid by remember { mutableStateOf<NativeTerminal.Grid?>(null) }
    var termState by remember { mutableIntStateOf(NativeTerminal.STATE_IDLE) }
    var message by remember { mutableStateOf("") }
    var latchCtrl by remember { mutableStateOf(false) }
    var latchAlt by remember { mutableStateOf(false) }
    var keyboardOn by remember { mutableStateOf(false) }
    var keyView by remember { mutableStateOf<TermInputView?>(null) }
    var canvasSize by remember { mutableStateOf(IntSize.Zero) }

    val paint =
        remember {
            Paint().apply {
                typeface = Typeface.MONOSPACE
                isAntiAlias = true
            }
        }

    val sendChar: (Int) -> Unit = { codepoint ->
        if (latchCtrl || latchAlt) {
            NativeTerminal.sendKey(
                NativeTerminal.KEY_CHAR,
                codepoint,
                alt = latchAlt,
                ctrl = latchCtrl,
            )
            latchCtrl = false
            latchAlt = false
        } else if (codepoint == '\n'.code) {
            NativeTerminal.sendKey(NativeTerminal.KEY_ENTER)
        } else {
            NativeTerminal.sendKey(NativeTerminal.KEY_CHAR, codepoint)
        }
    }
    val sendSpecial: (Int) -> Unit = { key ->
        NativeTerminal.sendKey(key, 0, alt = latchAlt, ctrl = latchCtrl)
        latchCtrl = false
        latchAlt = false
    }

    LaunchedEffect(opened) {
        if (!opened) {
            termState = NativeTerminal.STATE_FAILED
            message = NativeClient.couldNotConnect(address)
            return@LaunchedEffect
        }
        var revision = -1L
        while (true) {
            termState = NativeTerminal.state()
            message = NativeTerminal.message()
            val fresh = NativeTerminal.grid(0)
            if (fresh != null && fresh.revision != revision) {
                revision = fresh.revision
                grid = fresh
            }
            delay(TERM_POLL_MS)
        }
    }

    LaunchedEffect(canvasSize, paint) {
        if (canvasSize.width <= 0 || canvasSize.height <= 0) return@LaunchedEffect
        paint.textSize = canvasSize.width / 46f
        val cellW = max(1f, paint.measureText("M"))
        val cellH = max(1f, paint.fontMetrics.descent - paint.fontMetrics.ascent)
        NativeTerminal.resize(
            (canvasSize.width / cellW).toInt().coerceAtLeast(1),
            (canvasSize.height / cellH).toInt().coerceAtLeast(1),
        )
    }

    LaunchedEffect(keyboardOn) {
        val v = keyView ?: return@LaunchedEffect
        val imm = v.context.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
        if (!keyboardOn) {
            imm.hideSoftInputFromWindow(v.windowToken, 0)
            return@LaunchedEffect
        }
        v.requestFocus()
        imm.showSoftInput(v, 0)
    }

    if (termState == NativeTerminal.STATE_DECIDING) {
        TrustDialog(
            verdict = NativeTerminal.verdict(),
            fingerprint = NativeTerminal.fingerprint(),
            onAccept = { NativeTerminal.acceptKey() },
            onReject = { NativeTerminal.rejectKey() },
        )
    }

    Column(
        modifier =
            Modifier
                .fillMaxSize()
                .background(Color.Black)
                .safeDrawingPadding(),
    ) {
        Box(modifier = Modifier.weight(1f)) {
            Canvas(
                modifier =
                    Modifier
                        .fillMaxSize()
                        .onSizeChanged { canvasSize = it }
                        .pointerInput(Unit) {
                            detectTapGestures(onTap = { keyboardOn = true })
                        },
            ) {
                drawIntoCanvas { canvas ->
                    val g = grid ?: return@drawIntoCanvas
                    val native = canvas.nativeCanvas
                    val cellW = max(1f, paint.measureText("M"))
                    val metrics = paint.fontMetrics
                    val cellH = max(1f, metrics.descent - metrics.ascent)

                    paint.color = DEFAULT_BG
                    native.drawRect(0f, 0f, size.width, size.height, paint)

                    for (row in 0 until g.rows) {
                        val top = row * cellH
                        for (col in 0 until g.cols) {
                            val left = col * cellW
                            val bg = 0xFF000000.toInt() or g.background(row, col)
                            if (bg != DEFAULT_BG) {
                                paint.color = bg
                                native.drawRect(left, top, left + cellW, top + cellH, paint)
                            }
                            val cp = g.codepoint(row, col)
                            if (cp == ' '.code) continue
                            val attrs = g.attrs(row, col)
                            paint.color = 0xFF000000.toInt() or g.foreground(row, col)
                            paint.isFakeBoldText = (attrs and NativeTerminal.ATTR_BOLD) != 0
                            paint.isUnderlineText =
                                (attrs and NativeTerminal.ATTR_UNDERLINE) != 0
                            native.drawText(
                                String(Character.toChars(cp)),
                                left,
                                top - metrics.ascent,
                                paint,
                            )
                        }
                    }
                    paint.isFakeBoldText = false
                    paint.isUnderlineText = false

                    if (g.cursorVisible) {
                        paint.color = CURSOR_COLOR
                        paint.alpha = 160
                        val left = g.cursorCol * cellW
                        val top = g.cursorRow * cellH
                        native.drawRect(left, top, left + cellW, top + cellH, paint)
                        paint.alpha = 255
                    }
                }
            }

            AndroidView(
                factory = { ctx ->
                    TermInputView(ctx)
                        .apply {
                            onChar = { cp -> sendChar(cp) }
                            onSpecial = { key -> sendSpecial(key) }
                        }.also { keyView = it }
                },
                modifier = Modifier.size(1.dp),
            )

            if (termState >= NativeTerminal.STATE_REFUSED) {
                Box(
                    modifier =
                        Modifier
                            .fillMaxSize()
                            .background(Color.Black.copy(alpha = 0.7f))
                            .clickable(onClick = onDismiss),
                    contentAlignment = Alignment.Center,
                ) {
                    Column(
                        modifier = Modifier.padding(24.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        Text(message, color = Color.White)
                        TextButton(onClick = onDismiss) { Text("Back") }
                    }
                }
            }
        }

        ExtraKeysRow(
            latchCtrl = latchCtrl,
            latchAlt = latchAlt,
            onToggleCtrl = { latchCtrl = !latchCtrl },
            onToggleAlt = { latchAlt = !latchAlt },
            onKey = sendSpecial,
            onCtrlC = {
                NativeTerminal.sendKey(NativeTerminal.KEY_CHAR, 'c'.code, ctrl = true)
                latchCtrl = false
                latchAlt = false
            },
            onKeyboard = { keyboardOn = !keyboardOn },
        )

        Text(
            text =
                message.ifBlank {
                    NativeClient.string(NativeClient.STR_TERMINAL_EXTRA_KEYS_HINT)
                },
            style = MaterialTheme.typography.bodySmall,
            color = Color.White.copy(alpha = 0.7f),
            maxLines = 1,
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp),
        )
    }
}

@Composable
private fun ExtraKeysRow(
    latchCtrl: Boolean,
    latchAlt: Boolean,
    onToggleCtrl: () -> Unit,
    onToggleAlt: () -> Unit,
    onKey: (Int) -> Unit,
    onCtrlC: () -> Unit,
    onKeyboard: () -> Unit,
) {
    Row(
        modifier =
            Modifier
                .fillMaxWidth()
                .horizontalScroll(rememberScrollState())
                .padding(horizontal = 8.dp, vertical = 4.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        TermKeyButton("Esc") { onKey(NativeTerminal.KEY_ESCAPE) }
        TermKeyButton("Tab") { onKey(NativeTerminal.KEY_TAB) }
        TermKeyButton("Ctrl", active = latchCtrl, onClick = onToggleCtrl)
        TermKeyButton("Alt", active = latchAlt, onClick = onToggleAlt)
        TermKeyButton("←") { onKey(NativeTerminal.KEY_LEFT) }
        TermKeyButton("↓") { onKey(NativeTerminal.KEY_DOWN) }
        TermKeyButton("↑") { onKey(NativeTerminal.KEY_UP) }
        TermKeyButton("→") { onKey(NativeTerminal.KEY_RIGHT) }
        TermKeyButton("^C", onClick = onCtrlC)
        TermKeyButton("⌨", onClick = onKeyboard)
    }
}

@Composable
private fun TermKeyButton(
    label: String,
    active: Boolean = false,
    onClick: () -> Unit,
) {
    OutlinedButton(
        onClick = onClick,
        contentPadding = PaddingValues(horizontal = 12.dp, vertical = 2.dp),
    ) {
        Text(
            label,
            color = if (active) MaterialTheme.colorScheme.primary else Color.White,
        )
    }
}

@Composable
private fun TrustDialog(
    verdict: Int,
    fingerprint: String,
    onAccept: () -> Unit,
    onReject: () -> Unit,
) {
    val changed = verdict == NativeClient.TRUST_CHANGED
    AlertDialog(
        onDismissRequest = onReject,
        title = {
            Text(
                NativeClient.string(
                    if (changed) {
                        NativeClient.STR_TRUST_CHANGED_TITLE
                    } else {
                        NativeClient.STR_TRUST_NEW_HOST_TITLE
                    },
                ),
            )
        },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text(
                    NativeClient.string(
                        if (changed) {
                            NativeClient.STR_TRUST_CHANGED_BODY
                        } else {
                            NativeClient.STR_TRUST_NEW_HOST_BODY
                        },
                    ),
                )
                Text(
                    "${NativeClient.string(NativeClient.STR_TRUST_FINGERPRINT_LABEL)} " +
                        fingerprint,
                    style = MaterialTheme.typography.bodySmall,
                )
            }
        },
        confirmButton = {
            TextButton(onClick = onAccept) {
                Text(NativeClient.string(NativeClient.STR_TRUST_ACCEPT))
            }
        },
        dismissButton = {
            TextButton(onClick = onReject) {
                Text(NativeClient.string(NativeClient.STR_TRUST_REJECT))
            }
        },
    )
}
