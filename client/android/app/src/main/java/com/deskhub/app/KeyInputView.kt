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
    var onKey: ((Int, Boolean) -> Unit)? = null

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
        vkFor(keyCode)?.let { vk ->
            onKey?.invoke(vk, true)
            return true
        }
        if (keyCode == KeyEvent.KEYCODE_DEL) {
            onChar?.invoke('\b'.code)
            return true
        }
        val ch = event.unicodeChar
        if (ch != 0) {
            onChar?.invoke(ch)
            return true
        }
        val base = event.getUnicodeChar(0)
        if (base != 0) {
            onChar?.invoke(base)
            return true
        }
        return super.onKeyDown(keyCode, event)
    }

    override fun onKeyUp(
        keyCode: Int,
        event: KeyEvent,
    ): Boolean {
        vkFor(keyCode)?.let { vk ->
            onKey?.invoke(vk, false)
            return true
        }
        return super.onKeyUp(keyCode, event)
    }

    private fun vkFor(keyCode: Int): Int? = NativeClient.keyToVk(keyCode).takeIf { it != 0 }
}
