package com.deskhub.app

import android.content.Context
import android.text.InputType
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection

class KeyInputView(
    context: Context,
) : View(context) {
    var onChar: ((Int) -> Unit)? = null

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
                repeat(beforeLength) { onChar?.invoke('\b'.code) }
                return true
            }
        }
    }

    override fun onKeyDown(
        keyCode: Int,
        event: KeyEvent,
    ): Boolean {
        if (keyCode == KeyEvent.KEYCODE_DEL) {
            onChar?.invoke('\b'.code)
            return true
        }
        val ch = event.unicodeChar
        if (ch != 0) {
            onChar?.invoke(ch)
            return true
        }
        return super.onKeyDown(keyCode, event)
    }
}
