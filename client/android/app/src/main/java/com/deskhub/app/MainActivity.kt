package com.deskhub.app

import android.content.Context
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = getSharedPreferences("deskhub", Context.MODE_PRIVATE)

        val debuggable = (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0
        if (debuggable) {
            intent?.getStringExtra("addr")?.let { addr ->
                intent.removeExtra("addr")
                openStream(addr, 0)
            }
        }

        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                Surface(modifier = Modifier.fillMaxSize()) {
                    Column(modifier = Modifier.safeDrawingPadding()) {
                        MainScreen(
                            initialAddress = prefs.getString("addr", "").orEmpty(),
                            onRemember = { addr -> prefs.edit().putString("addr", addr).apply() },
                            onOpenStream = ::openStream,
                        )
                    }
                }
            }
        }
    }

    private fun openStream(
        addr: String,
        sourceId: Int,
        sources: List<NativeClient.Source> = emptyList(),
    ) {
        startActivity(
            Intent(this, StreamActivity::class.java)
                .putExtra("addr", addr)
                .putExtra("source", sourceId)
                .putExtra("srcIds", sources.map { it.id }.toIntArray())
                .putExtra("srcW", sources.map { it.width }.toIntArray())
                .putExtra("srcH", sources.map { it.height }.toIntArray())
                .putExtra("srcNames", sources.map { it.name }.toTypedArray())
                .putExtra("srcDisplayNames", sources.map { it.displayName }.toTypedArray())
                .putExtra("srcSizeLabels", sources.map { it.sizeLabel }.toTypedArray())
                .putExtra("srcPickerLabels", sources.map { it.pickerLabel }.toTypedArray()),
        )
    }
}

private sealed interface Step {
    data object Address : Step

    data class Querying(
        val seq: Long,
    ) : Step

    data class Picking(
        val sources: List<NativeClient.Source>,
    ) : Step
}

@Composable
private fun MainScreen(
    initialAddress: String,
    onRemember: (String) -> Unit,
    onOpenStream: (String, Int, List<NativeClient.Source>) -> Unit,
) {
    var step by remember { mutableStateOf<Step>(Step.Address) }
    var address by remember { mutableStateOf(initialAddress) }
    var querySeq by remember { mutableStateOf(0L) }
    val scope = rememberCoroutineScope()

    BackHandler(enabled = step != Step.Address) { step = Step.Address }

    val connect: (String) -> Unit = { addr ->
        onRemember(addr)
        val mine = Step.Querying(++querySeq)
        step = mine
        scope.launch {
            val sources = NativeClient.listSources(addr)
            if (step == mine) {
                if (sources.size <= 1) {
                    step = Step.Address
                    onOpenStream(addr, sources.firstOrNull()?.id ?: 0, sources)
                } else {
                    step = Step.Picking(sources)
                }
            }
        }
    }

    when (val s = step) {
        is Step.Address ->
            AddressScreen(
                address = address,
                onAddressChange = { address = it },
                busy = false,
                onConnect = connect,
            )

        is Step.Querying ->
            AddressScreen(
                address = address,
                onAddressChange = {},
                busy = true,
                onConnect = {},
            )

        is Step.Picking ->
            SourcePickerScreen(
                address = address,
                sources = s.sources,
                onPick = { source ->
                    step = Step.Address
                    onOpenStream(address, source.id, s.sources)
                },
            )
    }
}

@Composable
private fun AddressScreen(
    address: String,
    onAddressChange: (String) -> Unit,
    busy: Boolean,
    onConnect: (String) -> Unit,
) {
    val trimmed = address.trim()
    val go = { if (trimmed.isNotEmpty() && !busy) onConnect(trimmed) }

    Column(
        modifier =
            Modifier
                .fillMaxSize()
                .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        OutlinedTextField(
            value = address,
            onValueChange = onAddressChange,
            label = { Text("Host IP address") },
            singleLine = true,
            enabled = !busy,
            modifier = Modifier.fillMaxWidth(),
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Go),
            keyboardActions = KeyboardActions(onGo = { go() }),
        )

        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Button(
                onClick = go,
                enabled = trimmed.isNotEmpty() && !busy,
            ) { Text("Connect") }

            if (busy) {
                CircularProgressIndicator(modifier = Modifier.size(20.dp), strokeWidth = 2.dp)
            }
        }
    }
}

@Composable
private fun SourcePickerScreen(
    address: String,
    sources: List<NativeClient.Source>,
    onPick: (NativeClient.Source) -> Unit,
) {
    var pickedId by remember { mutableStateOf(sources.first().id) }

    Column(modifier = Modifier.fillMaxSize()) {
        Column(
            modifier =
                Modifier
                    .weight(1f)
                    .verticalScroll(rememberScrollState())
                    .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(text = address, style = MaterialTheme.typography.titleMedium)

            sources.forEach { source ->
                Row(
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .clickable { pickedId = source.id }
                            .padding(vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    RadioButton(
                        selected = source.id == pickedId,
                        onClick = { pickedId = source.id },
                    )
                    Column {
                        Text(
                            text = source.displayName,
                            style = MaterialTheme.typography.bodyLarge,
                        )
                        Text(
                            text = source.sizeLabel,
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                }
            }
        }

        Button(
            onClick = { sources.firstOrNull { it.id == pickedId }?.let(onPick) },
            modifier =
                Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
        ) { Text("Start viewing") }
    }
}
