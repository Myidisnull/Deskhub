package com.deskhub.app

import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

object PairingAsks {
    var queue by mutableStateOf(emptyList<NativeHost.PairingRequest>())
        private set

    fun drain() {
        val hosting =
            NativeHost.shareState == NativeHost.ShareState.SHARING || NativeHost.filesActive()
        if (!hosting) {
            if (queue.isNotEmpty()) queue = emptyList()
            return
        }
        val fresh = NativeHost.takePairingRequests()
        if (fresh.isEmpty()) return
        val queued = queue.map { it.addrPacked }.toSet()
        queue = queue + fresh.filter { it.addrPacked !in queued }
    }

    fun answerFirst(allow: Boolean) {
        val request = queue.firstOrNull() ?: return
        NativeHost.answerPairing(request.addrPacked, allow)
        queue = queue.drop(1)
    }
}

@Composable
fun PairingPrompt() {
    PairingAsks.queue.firstOrNull()?.let { request ->
        AlertDialog(
            onDismissRequest = {},
            title = { Text(NativeClient.string(NativeClient.STR_PAIRING_REQUEST_TITLE)) },
            text = { Text(request.body) },
            confirmButton = {
                TextButton(onClick = { PairingAsks.answerFirst(true) }) {
                    Text(NativeClient.string(NativeClient.STR_PAIRING_ALLOW))
                }
            },
            dismissButton = {
                TextButton(onClick = { PairingAsks.answerFirst(false) }) {
                    Text(NativeClient.string(NativeClient.STR_PAIRING_DENY))
                }
            },
        )
    }
}
