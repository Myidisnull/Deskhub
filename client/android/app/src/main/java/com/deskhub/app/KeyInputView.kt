package com.deskhub.app

import android.content.Context
import android.view.KeyEvent

class KeyInputView(
    context: Context,
) : ImeInputView(context) {
    var onKey: ((Int, Boolean) -> Unit)? = null

    override fun onBackspace() {
        onChar?.invoke('\b'.code)
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
            onBackspace()
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
