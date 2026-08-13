package com.deskhub.app

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import kotlinx.coroutines.delay

object ClipboardPump {
    private const val PUMP_INTERVAL_MS = 1000L
    private const val CLIP_LABEL = "Deskhub"

    suspend fun run(
        context: Context,
        take: () -> String,
        offer: (String) -> Unit,
    ) {
        val clipboard =
            context.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager ?: return
        var dirty = true
        val listener = ClipboardManager.OnPrimaryClipChangedListener { dirty = true }
        clipboard.addPrimaryClipChangedListener(listener)
        try {
            while (true) {
                val remote = take()
                if (remote.isNotEmpty()) {
                    clipboard.setPrimaryClip(ClipData.newPlainText(CLIP_LABEL, remote))
                } else if (dirty) {
                    dirty = false
                    val local = localText(clipboard)
                    if (local.isNotEmpty()) offer(local)
                }
                delay(PUMP_INTERVAL_MS)
            }
        } finally {
            clipboard.removePrimaryClipChangedListener(listener)
        }
    }

    private fun localText(clipboard: ClipboardManager): String {
        val clip = clipboard.primaryClip ?: return ""
        if (clip.itemCount == 0) return ""
        return clip
            .getItemAt(0)
            .text
            ?.toString()
            .orEmpty()
    }
}
