package com.deskhub.app

import android.Manifest
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.SecondaryTabRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.core.content.edit
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.text.DateFormat
import java.util.Date
import kotlin.time.Duration.Companion.milliseconds

class MainActivity : ComponentActivity() {
    private var pendingShare: HostService.ShareRequest? = null

    companion object {
        private const val TAG = "Deskhub"
    }

    private val projectionConsent =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            val consent = result.data
            val request = pendingShare
            pendingShare = null
            if (result.resultCode != RESULT_OK || consent == null || request == null) {
                NativeHost.reportFailure("")
                return@registerForActivityResult
            }
            HostService.start(this, result.resultCode, consent, request)
        }

    private val notificationConsent =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { }

    private val audioConsent =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            val request = pendingShare
            if (!granted) {
                Log.i(TAG, "[audio] evt=capture_skip reason=viewer declined the recording prompt")
            }
            if (request != null) startProjectionConsent(request)
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        NativeClient.useAppDataDir(this)
        NativeHost.publishScreenSize(this)
        askForNotifications()
        val prefs = getSharedPreferences("deskhub", MODE_PRIVATE)
        prefs.edit { this.remove("passcode").apply() }
        val lastAddress = prefs.getString("addr", "").orEmpty()

        val debuggable = (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0
        if (debuggable) {
            intent?.getStringExtra("addr")?.let { addr ->
                val passcode = intent.getStringExtra("passcode").orEmpty()
                intent.removeExtra("addr")
                openStream(addr, passcode, 0)
            }
        }

        setContent {
            MaterialTheme(colorScheme = lightColorScheme()) {
                Surface(modifier = Modifier.fillMaxSize(), color = Color.White) {
                    Column(modifier = Modifier.safeDrawingPadding()) {
                        MainScreen(
                            initialAddress = lastAddress,
                            initialPasscode = NativeClient.recentPasscode(lastAddress),
                            initialSessionKey = NativeClient.recentSessionKey(lastAddress),
                            onRemember = { addr, _ ->
                                prefs.edit { putString("addr", addr) }
                            },
                            onOpenStream = ::openStream,
                            onOpenShell = ::openShell,
                            onStartSharing = ::requestSharing,
                            onStopSharing = { HostService.stop(this@MainActivity) },
                        )
                    }
                }
            }
        }
    }

    private fun askForNotifications() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return
        if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) ==
            PackageManager.PERMISSION_GRANTED
        ) {
            return
        }
        notificationConsent.launch(Manifest.permission.POST_NOTIFICATIONS)
    }

    private fun requestSharing(request: HostService.ShareRequest) {
        pendingShare = request
        NativeHost.stop()
        NativeHost.awaitStart()
        if (AudioShare.isSupported && !AudioShare.permissionGranted(this)) {
            audioConsent.launch(Manifest.permission.RECORD_AUDIO)
            return
        }
        startProjectionConsent(request)
    }

    private fun startProjectionConsent(request: HostService.ShareRequest) {
        val manager = getSystemService(MediaProjectionManager::class.java) ?: return
        pendingShare = request
        projectionConsent.launch(manager.createScreenCaptureIntent())
    }

    private fun openStream(
        addr: String,
        passcode: String,
        sourceId: Int,
        sources: List<NativeClient.Source> = emptyList(),
        sessionKey: String = "",
        openFiles: Boolean = false,
    ) {
        startActivity(
            Intent(this, StreamActivity::class.java)
                .putExtra("addr", addr)
                .putExtra("passcode", passcode)
                .putExtra("sessionKey", sessionKey)
                .putExtra("source", sourceId)
                .putExtra("openFiles", openFiles)
                .putExtra("srcIds", sources.map { it.id }.toIntArray())
                .putExtra("srcDisplayNames", sources.map { it.displayName }.toTypedArray())
                .putExtra("srcSizeLabels", sources.map { it.sizeLabel }.toTypedArray()),
        )
    }

    private fun openShell(
        addr: String,
        passcode: String,
    ) {
        startActivity(
            Intent(this, TerminalActivity::class.java)
                .putExtra("addr", addr)
                .putExtra("passcode", passcode),
        )
    }
}

private const val POLL_INTERVAL_MS = 1000L
private const val PAIRING_POLL_MS = 500L
private const val PORT_SETTLE_MS = 600L

private val HeadingColor = Color(0xFF111827)
private val MutedColor = Color(0xFF6B7280)
private val OnlineColor = Color(0xFF00913C)
private val OfflineColor = Color(0xFFC82828)

private data class PairingAsk(
    val addrPacked: Long,
    val shortKey: String,
    val body: String,
)

private fun unixDateText(unix: Long): String {
    if (unix <= 0L) return "-"
    return DateFormat
        .getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)
        .format(Date(unix * 1000L))
}

private fun pairedDeviceSubtitle(device: NativeClient.PairedDevice): String =
    buildString {
        append(device.shortKey)
        append("  ·  ")
        append(NativeClient.string(NativeClient.STR_PAIRED_COLUMN_PAIRED))
        append(' ')
        append(unixDateText(device.pairedUnix))
        append("  ·  ")
        append(NativeClient.string(NativeClient.STR_PAIRED_COLUMN_LAST_SEEN))
        append(' ')
        append(unixDateText(device.lastSeenUnix))
    }

private fun drainPairingAsks(pending: List<PairingAsk>): List<PairingAsk> {
    val requests = NativeClient.takePairingRequests()
    if (requests.isEmpty()) return pending
    val pairedKeys = NativeClient.pairedDevices().map { it.shortKey }.toSet()
    val next = pending.toMutableList()
    for (request in requests) {
        if (pairedKeys.contains(request.shortKey)) {
            NativeClient.answerPairing(request.addrPacked, true)
            continue
        }
        if (next.any { it.addrPacked == request.addrPacked }) continue
        val address = NativeClient.formatAddress(request.addrPacked)
        val body = NativeClient.pairingRequestBody(request.name, address, request.shortKey)
        next.add(PairingAsk(request.addrPacked, request.shortKey, body))
    }
    return next
}

@Composable
private fun Heading(
    text: String,
    modifier: Modifier = Modifier,
) {
    Text(
        text,
        modifier = modifier,
        style = MaterialTheme.typography.titleLarge,
        fontWeight = FontWeight.Bold,
        color = HeadingColor,
    )
}

@Composable
private fun SectionLabel(text: String) {
    Text(
        text,
        style = MaterialTheme.typography.titleMedium,
        fontWeight = FontWeight.Bold,
        color = HeadingColor,
    )
}

@Composable
private fun HeadingRow(
    text: String,
    onRefresh: () -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Heading(text, modifier = Modifier.weight(1f))
        TextButton(onClick = onRefresh) {
            Text(NativeClient.string(NativeClient.STR_REFRESH_NOW))
        }
    }
}

private enum class Section {
    CLIENT,
    HOST,
    SETTINGS,
}

private sealed interface Step {
    data object Address : Step

    data class Querying(
        val seq: Long,
    ) : Step

    data class Connected(
        val address: String,
        val passcode: String,
        val sessionKey: String,
        val sources: List<NativeClient.Source>,
        val caps: NativeClient.HostCaps,
        val openFiles: Boolean = false,
    ) : Step

    data class Picking(
        val connected: Connected,
    ) : Step
}

@Composable
private fun MainScreen(
    initialAddress: String,
    initialPasscode: String,
    initialSessionKey: String = "",
    onRemember: (String, String) -> Unit,
    onOpenStream: (String, String, Int, List<NativeClient.Source>, String, Boolean) -> Unit,
    onOpenShell: (String, String) -> Unit,
    onStartSharing: (HostService.ShareRequest) -> Unit,
    onStopSharing: () -> Unit,
) {
    var step by remember { mutableStateOf<Step>(Step.Address) }
    var address by remember { mutableStateOf(NativeClient.addressHost(initialAddress)) }
    var connectPort by remember { mutableStateOf(portFieldText(initialAddress)) }
    var passcode by remember { mutableStateOf(initialPasscode) }
    var sessionKey by remember { mutableStateOf(initialSessionKey) }
    var deviceName by remember {
        mutableStateOf(NativeClient.deviceName().ifBlank { Build.MODEL.orEmpty() })
    }
    var connectError by remember { mutableStateOf("") }
    var querySeq by remember { mutableLongStateOf(0L) }
    var scanHits by remember { mutableStateOf(emptyList<NativeClient.ScanHit>()) }
    var recentDevices by remember { mutableStateOf(emptyList<NativeClient.RecentDevice>()) }
    var scanStatus by remember { mutableStateOf("") }
    var recentNote by remember { mutableStateOf("") }
    var pendingPick by remember { mutableStateOf<PendingPick?>(null) }
    var section by remember { mutableStateOf(Section.CLIENT) }
    var port by remember { mutableIntStateOf(NativeClient.settingsPort()) }
    var pairingAsks by remember { mutableStateOf(emptyList<PairingAsk>()) }
    val scope = rememberCoroutineScope()
    val rescanTicks = remember { NativeClient.rescanSeconds() }

    BackHandler(enabled = step != Step.Address) { step = Step.Address }

    DisposableEffect(Unit) {
        onDispose {
            NativeClient.scanCancel()
            if (NativeHost.shareState == NativeHost.ShareState.IDLE) NativeHost.stop()
        }
    }

    LaunchedEffect(Unit) {
        while (true) {
            pairingAsks = drainPairingAsks(pairingAsks)
            delay(PAIRING_POLL_MS.milliseconds)
        }
    }

    LaunchedEffect(port) {
        NativeClient.watchRecent()
        NativeClient.scanRestart(port)
        var idleTicks = 0
        while (true) {
            scanHits = NativeClient.scanHits()
            recentDevices = NativeClient.recentDevices()
            scanStatus = NativeClient.scanStatusText(port)
            recentNote = NativeClient.recentNote()
            if (NativeClient.scanRunning()) {
                idleTicks = 0
            } else {
                idleTicks++
                if (idleTicks >= rescanTicks) {
                    idleTicks = 0
                    NativeClient.scanStart(port)
                }
            }
            delay(POLL_INTERVAL_MS.milliseconds)
        }
    }

    LaunchedEffect(port) {
        var receiving = false
        while (true) {
            val accept = NativeClient.acceptFiles()
            val busy =
                NativeHost.shareState == NativeHost.ShareState.SHARING ||
                    NativeHost.shareState == NativeHost.ShareState.STARTING
            val wanted = accept && !busy
            if (wanted && !receiving) {
                receiving = NativeHost.startFiles(port, NativeHost.passcode())
            } else if (!wanted && receiving) {
                if (NativeHost.shareState == NativeHost.ShareState.IDLE) NativeHost.stop()
                receiving = false
            } else if (receiving && !NativeHost.isRunning()) {
                receiving = false
            }
            delay(POLL_INTERVAL_MS.milliseconds)
        }
    }

    val connect: (String) -> Unit = connectLambda@{ addr ->
        if (!NativeClient.parseAddress(addr)) {
            connectError = NativeClient.string(NativeClient.STR_INVALID_ADDRESS_HINT)
            return@connectLambda
        }
        val code = passcode.trim()
        if (!NativeClient.isValidPasscode(code)) {
            connectError = NativeClient.string(NativeClient.STR_PASSCODE_INVALID)
            return@connectLambda
        }
        var key = sessionKey.trim()
        if (key.isEmpty()) key = NativeClient.recentSessionKey(addr)
        if (key.isNotEmpty() && !NativeClient.isValidSessionKey(key)) {
            connectError = NativeClient.string(NativeClient.STR_SESSION_KEY_INVALID)
            return@connectLambda
        }
        if (NativeClient.recentEncrypted(addr) && key.isEmpty()) {
            connectError = NativeClient.string(NativeClient.STR_SESSION_KEY_INVALID)
            return@connectLambda
        }
        connectError = ""
        deviceName = deviceName.trim().ifBlank { Build.MODEL.orEmpty() }
        NativeClient.setDeviceName(deviceName)
        val mine = Step.Querying(++querySeq)
        step = mine
        scope.launch {
            val queried = NativeClient.listSources(addr, code)
            if (queried == null) {
                if (step == mine) {
                    step = Step.Address
                    connectError = NativeClient.sourceQueryFailed(addr)
                }
                return@launch
            }
            if (queried.sources.isEmpty() && !queried.caps.files && !queried.caps.terminal) {
                if (step == mine) {
                    step = Step.Address
                    connectError = NativeClient.sourceQueryEmpty(addr)
                }
                return@launch
            }
            onRemember(addr, code)
            NativeClient.recentTouch(addr, code, key.isNotEmpty(), key)
            NativeClient.watchRecent()
            recentDevices = NativeClient.recentDevices()
            if (step == mine) {
                step =
                    Step.Connected(
                        address = addr,
                        passcode = code,
                        sessionKey = key,
                        sources = queried.sources,
                        caps = queried.caps,
                    )
            }
        }
    }

    val pickDevice: (String, String) -> Unit = { addr, code ->
        connectError = ""
        pendingPick = PendingPick(addr, code)
    }

    when (val s = step) {
        is Step.Address, is Step.Querying ->
            HomeScreen(
                section = section,
                onSectionChange = { section = it },
                address = address,
                onAddressChange = { address = it },
                connectPort = connectPort,
                onConnectPortChange = { connectPort = it },
                passcode = passcode,
                onPasscodeChange = { passcode = it },
                sessionKey = sessionKey,
                onSessionKeyChange = { sessionKey = it },
                deviceName = deviceName,
                onDeviceNameChange = { deviceName = it },
                busy = step is Step.Querying,
                error = connectError,
                onConnect = connect,
                scanHits = scanHits,
                recentDevices = recentDevices,
                scanStatus = scanStatus,
                recentNote = recentNote,
                onPickDevice = pickDevice,
                onRescan = { scope.launch { NativeClient.scanRestart(port) } },
                onRefreshStatus = { scope.launch { NativeClient.statusRefreshNow() } },
                onForgetDevice = { addr ->
                    scope.launch {
                        NativeClient.recentRemove(addr)
                        recentDevices = NativeClient.recentDevices()
                        recentNote = NativeClient.recentNote()
                    }
                },
                port = port,
                onPortChange = { chosen ->
                    NativeClient.setSettingsPort(chosen)
                    port = chosen
                },
                onStartSharing = onStartSharing,
                onStopSharing = onStopSharing,
            )

        is Step.Connected ->
            ConnectedScreen(
                address = s.address,
                sources = s.sources,
                caps = s.caps,
                onOpenDesktop = {
                    val decision = NativeClient.connectDecision(s.sources)
                    if (decision < 0) {
                        step = Step.Picking(s.copy(openFiles = false))
                    } else if (s.sources.isNotEmpty()) {
                        onOpenStream(s.address, s.passcode, decision, s.sources, s.sessionKey, false)
                    }
                },
                onOpenFiles = {
                    if (!s.caps.files || s.sources.isEmpty()) return@ConnectedScreen
                    if (s.sources.size > 1) {
                        step = Step.Picking(s.copy(openFiles = true))
                    } else {
                        onOpenStream(
                            s.address,
                            s.passcode,
                            s.sources.first().id,
                            s.sources,
                            s.sessionKey,
                            true,
                        )
                    }
                },
                onOpenShell = {
                    if (!s.caps.terminal) return@ConnectedScreen
                    onOpenShell(s.address, s.passcode)
                },
                onDisconnect = { step = Step.Address },
            )

        is Step.Picking ->
            SourcePickerScreen(
                address = s.connected.address,
                sources = s.connected.sources,
                onPick = { source ->
                    val c = s.connected
                    step =
                        Step.Connected(
                            address = c.address,
                            passcode = c.passcode,
                            sessionKey = c.sessionKey,
                            sources = c.sources,
                            caps = c.caps,
                        )
                    onOpenStream(
                        c.address,
                        c.passcode,
                        source.id,
                        c.sources,
                        c.sessionKey,
                        c.openFiles,
                    )
                },
            )
    }

    pendingPick?.let { pick ->
        PasscodeDialog(
            addr = pick.addr,
            initial = pick.passcode,
            onDismiss = { pendingPick = null },
            onConfirm = { chosenAddr, code ->
                pendingPick = null
                address = NativeClient.addressHost(chosenAddr)
                connectPort = portFieldText(chosenAddr)
                passcode = code
                sessionKey = NativeClient.recentSessionKey(chosenAddr)
                connect(chosenAddr)
            },
        )
    }

    pairingAsks.firstOrNull()?.let { ask ->
        AlertDialog(
            onDismissRequest = {},
            title = { Text(NativeClient.string(NativeClient.STR_PAIRING_REQUEST_TITLE)) },
            text = { Text(ask.body) },
            confirmButton = {
                TextButton(
                    onClick = {
                        NativeClient.answerPairing(ask.addrPacked, true)
                        pairingAsks = pairingAsks.filterNot { it.addrPacked == ask.addrPacked }
                    },
                ) {
                    Text(NativeClient.string(NativeClient.STR_PAIRING_ALLOW))
                }
            },
            dismissButton = {
                TextButton(
                    onClick = {
                        NativeClient.answerPairing(ask.addrPacked, false)
                        pairingAsks = pairingAsks.filterNot { it.addrPacked == ask.addrPacked }
                    },
                ) {
                    Text(NativeClient.string(NativeClient.STR_PAIRING_DENY))
                }
            },
        )
    }
}

private data class PendingPick(
    val addr: String,
    val passcode: String,
)

private fun portFieldText(addr: String): String {
    val explicit = NativeClient.addressPort(addr)
    val port = if (explicit != 0) explicit else NativeClient.defaultPort()
    return port.toString()
}

private fun copyToClipboard(
    context: Context,
    text: String,
) {
    val clipboard =
        context.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager ?: return
    clipboard.setPrimaryClip(ClipData.newPlainText(context.getString(R.string.app_name), text))
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
        Toast
            .makeText(
                context,
                NativeClient.string(NativeClient.STR_COPIED),
                Toast.LENGTH_SHORT,
            ).show()
    }
}

private data class LanguageOption(
    val code: String,
    val label: String,
)

private fun languageOptions(): List<LanguageOption> =
    listOf(
        LanguageOption("", NativeClient.string(NativeClient.STR_LANGUAGE_SYSTEM)),
        LanguageOption("en", "English"),
        LanguageOption("zh-Hans", "简体中文"),
        LanguageOption("fr", "Français"),
        LanguageOption("de", "Deutsch"),
        LanguageOption("ru", "Русский"),
        LanguageOption("ja", "日本語"),
        LanguageOption("ko", "한국어"),
        LanguageOption("ar", "العربية"),
    )

@Composable
private fun PasscodeDialog(
    addr: String,
    initial: String,
    onDismiss: () -> Unit,
    onConfirm: (String, String) -> Unit,
) {
    val host = NativeClient.addressHost(addr)
    var typed by remember(addr, initial) { mutableStateOf(initial) }
    var typedPort by remember(addr) { mutableStateOf(portFieldText(addr)) }
    val ready = NativeClient.isValidPasscode(typed.trim())
    val confirm = {
        if (ready) onConfirm(NativeClient.composeAddress(host, typedPort), typed.trim())
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(NativeClient.string(NativeClient.STR_CONNECT_PROMPT_TITLE)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text(host, style = MaterialTheme.typography.titleMedium)
                OutlinedTextField(
                    value = typedPort,
                    onValueChange = { entered ->
                        typedPort = entered.filter { it.isDigit() }.take(5)
                    },
                    label = { Text(NativeClient.string(NativeClient.STR_UDP_PORT_LABEL)) },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                )
                OutlinedTextField(
                    value = typed,
                    onValueChange = { entered ->
                        typed = entered.filter { it.isDigit() }.take(NativeClient.passcodeDigits())
                    },
                    label = { Text(NativeClient.string(NativeClient.STR_CLIENT_PASSCODE_PROMPT)) },
                    supportingText = {
                        Text(NativeClient.string(NativeClient.STR_CLIENT_PASSCODE_HINT))
                    },
                    singleLine = true,
                    keyboardOptions =
                        KeyboardOptions(
                            keyboardType = KeyboardType.NumberPassword,
                            imeAction = ImeAction.Go,
                        ),
                    keyboardActions = KeyboardActions(onGo = { confirm() }),
                )
            }
        },
        confirmButton = { TextButton(onClick = confirm, enabled = ready) { Text("Connect") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

@Composable
private fun HomeScreen(
    section: Section,
    onSectionChange: (Section) -> Unit,
    address: String,
    onAddressChange: (String) -> Unit,
    connectPort: String,
    onConnectPortChange: (String) -> Unit,
    passcode: String,
    onPasscodeChange: (String) -> Unit,
    sessionKey: String,
    onSessionKeyChange: (String) -> Unit,
    deviceName: String,
    onDeviceNameChange: (String) -> Unit,
    busy: Boolean,
    error: String,
    onConnect: (String) -> Unit,
    scanHits: List<NativeClient.ScanHit>,
    recentDevices: List<NativeClient.RecentDevice>,
    scanStatus: String,
    recentNote: String,
    onPickDevice: (String, String) -> Unit,
    onRescan: () -> Unit,
    onRefreshStatus: () -> Unit,
    onForgetDevice: (String) -> Unit,
    port: Int,
    onPortChange: (Int) -> Unit,
    onStartSharing: (HostService.ShareRequest) -> Unit,
    onStopSharing: () -> Unit,
) {
    Column(modifier = Modifier.fillMaxSize()) {
        SecondaryTabRow(
            section.ordinal,
            Modifier,
        ) {
            Tab(
                selected = section == Section.CLIENT,
                onClick = { onSectionChange(Section.CLIENT) },
                text = { Text(NativeClient.string(NativeClient.STR_SIDEBAR_CLIENT)) },
            )
            Tab(
                selected = section == Section.HOST,
                onClick = { onSectionChange(Section.HOST) },
                text = { Text(NativeClient.string(NativeClient.STR_SIDEBAR_HOST)) },
            )
            Tab(
                selected = section == Section.SETTINGS,
                onClick = { onSectionChange(Section.SETTINGS) },
                text = { Text(NativeClient.string(NativeClient.STR_SIDEBAR_SETTINGS)) },
            )
        }

        Column(modifier = Modifier.weight(1f)) {
            when (section) {
                Section.CLIENT ->
                    AddressScreen(
                        address = address,
                        onAddressChange = onAddressChange,
                        connectPort = connectPort,
                        onConnectPortChange = onConnectPortChange,
                        passcode = passcode,
                        onPasscodeChange = onPasscodeChange,
                        sessionKey = sessionKey,
                        onSessionKeyChange = onSessionKeyChange,
                        deviceName = deviceName,
                        onDeviceNameChange = onDeviceNameChange,
                        busy = busy,
                        error = error,
                        onConnect = onConnect,
                        scanHits = scanHits,
                        recentDevices = recentDevices,
                        scanStatus = scanStatus,
                        recentNote = recentNote,
                        onPickDevice = onPickDevice,
                        onRescan = onRescan,
                        onRefreshStatus = onRefreshStatus,
                        onForgetDevice = onForgetDevice,
                    )

                Section.HOST ->
                    HostScreen(
                        port = port,
                        onStartSharing = onStartSharing,
                        onStopSharing = onStopSharing,
                    )

                Section.SETTINGS -> SettingsScreen(port = port, onPortChange = onPortChange)
            }
        }
    }
}

@Composable
private fun HostScreen(
    port: Int,
    onStartSharing: (HostService.ShareRequest) -> Unit,
    onStopSharing: () -> Unit,
) {
    var passcode by remember { mutableStateOf(NativeHost.passcode()) }
    var state by remember { mutableStateOf(NativeHost.shareState) }
    var error by remember { mutableStateOf(NativeHost.shareError) }
    var rows by remember { mutableStateOf(emptyList<NativeHost.HostRow>()) }
    var addresses by remember { mutableStateOf(NativeHost.localAddresses()) }

    LaunchedEffect(Unit) {
        while (true) {
            state = NativeHost.shareState
            error = NativeHost.shareError
            rows = if (state == NativeHost.ShareState.SHARING) NativeHost.hostRows() else emptyList()
            addresses = NativeHost.localAddresses()
            if (state == NativeHost.ShareState.SHARING && !NativeHost.isRunning()) onStopSharing()
            delay(POLL_INTERVAL_MS)
        }
    }

    val sharing = state == NativeHost.ShareState.SHARING
    val starting = state == NativeHost.ShareState.STARTING
    val ready = NativeClient.isValidPasscode(passcode.trim())

    Column(
        modifier =
            Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Heading(NativeClient.string(NativeClient.STR_HOST_HEADING))

        if (!NativeHost.isSupported) {
            Text(
                NativeClient.string(NativeClient.STR_SHARE_START_FAILED),
                color = MaterialTheme.colorScheme.error,
            )
            return@Column
        }

        Text(
            NativeClient.string(
                if (sharing) NativeClient.STR_SHARE_STATE_ON else NativeClient.STR_SHARE_STATE_OFF,
            ),
            style = MaterialTheme.typography.titleMedium,
            color = if (sharing) OnlineColor else MutedColor,
        )

        OutlinedTextField(
            value = passcode,
            onValueChange = { typed ->
                passcode = typed.filter { it.isDigit() }.take(NativeClient.passcodeDigits())
            },
            label = { Text(NativeClient.string(NativeClient.STR_PASSCODE_LABEL)) },
            singleLine = true,
            enabled = !sharing && !starting,
            modifier = Modifier.fillMaxWidth(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
        )

        var bindIp by remember { mutableStateOf(NativeHost.bindIp()) }
        var bindMenuOpen by remember { mutableStateOf(false) }
        val bindStale = bindIp.isNotEmpty() && addresses.none { it.ip == bindIp }
        val bindLabel =
            when {
                bindIp.isEmpty() -> NativeClient.string(NativeClient.STR_BIND_ALL_INTERFACES)
                bindStale ->
                    "$bindIp (${NativeClient.string(NativeClient.STR_BIND_NOT_CONNECTED)})"
                else -> bindIp
            }
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                NativeClient.string(NativeClient.STR_BIND_INTERFACE_LABEL),
                modifier = Modifier.weight(1f),
            )
            Box {
                TextButton(
                    onClick = { bindMenuOpen = true },
                    enabled = !sharing && !starting,
                ) { Text(bindLabel) }
                DropdownMenu(
                    expanded = bindMenuOpen,
                    onDismissRequest = { bindMenuOpen = false },
                ) {
                    DropdownMenuItem(
                        text = {
                            Text(NativeClient.string(NativeClient.STR_BIND_ALL_INTERFACES))
                        },
                        onClick = {
                            bindIp = ""
                            NativeHost.setBindIp("")
                            bindMenuOpen = false
                        },
                    )
                    addresses.forEach { address ->
                        DropdownMenuItem(
                            text = { Text("${address.ip}  (${address.name})") },
                            onClick = {
                                bindIp = address.ip
                                NativeHost.setBindIp(address.ip)
                                bindMenuOpen = false
                            },
                        )
                    }
                }
            }
        }

        Button(
            onClick = {
                if (sharing) {
                    onStopSharing()
                    return@Button
                }
                val trimmed = passcode.trim()
                NativeHost.savePasscode(trimmed)
                val defaults = NativeHost.shareDefaults()
                onStartSharing(
                    HostService.ShareRequest(
                        fps = defaults.fps,
                        bitrateMbps = defaults.bitrateMbps,
                        maxDim = defaults.maxDim,
                        port = port,
                        passcode = trimmed,
                    ),
                )
            },
            enabled = sharing || (ready && !starting),
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text(
                NativeClient.string(
                    when {
                        sharing -> NativeClient.STR_STOP_SHARING
                        starting -> NativeClient.STR_STARTING_SHARE
                        else -> NativeClient.STR_START_SHARING
                    },
                ),
            )
        }

        Text(
            if (sharing) {
                NativeHost.sharingStatus(port, passcode.trim())
            } else {
                NativeHost.idleStatus(port)
            },
            style = MaterialTheme.typography.bodyMedium,
            color = MutedColor,
        )

        if (!ready) {
            Text(
                NativeClient.string(NativeClient.STR_PASSCODE_INVALID),
                color = MaterialTheme.colorScheme.error,
            )
        }

        if (error.isNotEmpty()) {
            Text(error, color = MaterialTheme.colorScheme.error)
        }

        Heading(NativeClient.string(NativeClient.STR_HOST_IP_INTRO))
        if (addresses.isEmpty()) {
            Text(
                NativeClient.string(NativeClient.STR_NO_NETWORK_ADDRESS),
                style = MaterialTheme.typography.bodyMedium,
                color = MutedColor,
            )
        } else {
            val context = LocalContext.current
            for (address in addresses.filter { bindIp.isEmpty() || it.ip == bindIp }) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(address.name, modifier = Modifier.weight(1f), color = MutedColor)
                    Text(address.ip, fontWeight = FontWeight.Bold, color = HeadingColor)
                    TextButton(onClick = { copyToClipboard(context, address.ip) }) {
                        Text(NativeClient.string(NativeClient.STR_COPY))
                    }
                }
            }
        }

        Text(
            NativeClient.string(NativeClient.STR_SHARING_CONNECT_HINT),
            style = MaterialTheme.typography.bodySmall,
            color = MutedColor,
        )

        HostRowList(rows = rows, sharing = sharing)
    }
}

@Composable
private fun HostRowList(
    rows: List<NativeHost.HostRow>,
    sharing: Boolean,
) {
    if (!sharing || rows.isEmpty()) {
        Text(
            NativeClient.string(
                if (sharing) NativeClient.STR_NOTHING_SHARED else NativeClient.STR_NOT_SHARING,
            ),
            style = MaterialTheme.typography.bodyMedium,
            color = MutedColor,
        )
        return
    }

    for (row in rows) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    if (row.viewer) row.client else row.source,
                    color = if (row.online) OnlineColor else HeadingColor,
                )
                Text(
                    if (row.viewer) "${row.rtt}  ${row.mbps}" else "${row.size}  ${row.viewers}",
                    style = MaterialTheme.typography.bodySmall,
                    color = MutedColor,
                )
            }
            if (row.viewer) {
                TextButton(onClick = { NativeHost.kickViewer(row.sourceId, row.viewerAddr) }) {
                    Text(NativeClient.string(NativeClient.STR_DISCONNECT_VIEWER_ACTION))
                }
            }
        }
    }
}

@Composable
private fun SettingsScreen(
    port: Int,
    onPortChange: (Int) -> Unit,
) {
    var typed by remember(port) { mutableStateOf(port.toString()) }

    LaunchedEffect(typed) {
        val chosen = typed.toIntOrNull()
        if (chosen == null || chosen !in 1..65535 || chosen == port) return@LaunchedEffect
        delay(PORT_SETTLE_MS.milliseconds)
        onPortChange(chosen)
    }

    Column(
        modifier =
            Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Heading(NativeClient.string(NativeClient.STR_CLIENT_SETTINGS_HEADING))
        Text(
            NativeClient.string(NativeClient.STR_CLIENT_SETTINGS_HINT),
            style = MaterialTheme.typography.bodyMedium,
            color = MutedColor,
        )

        SectionLabel(NativeClient.string(NativeClient.STR_SECTION_LANGUAGE))
        var language by remember { mutableStateOf(NativeClient.language()) }
        var languageMenuOpen by remember { mutableStateOf(false) }
        val languages = remember { languageOptions() }
        val languageLabel =
            languages.firstOrNull { it.code == language }?.label
                ?: NativeClient.string(NativeClient.STR_LANGUAGE_SYSTEM)
        Box(modifier = Modifier.fillMaxWidth()) {
            OutlinedTextField(
                value = languageLabel,
                onValueChange = {},
                readOnly = true,
                label = { Text(NativeClient.string(NativeClient.STR_LANGUAGE_LABEL)) },
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .clickable { languageMenuOpen = true },
                enabled = false,
            )
            DropdownMenu(
                expanded = languageMenuOpen,
                onDismissRequest = { languageMenuOpen = false },
            ) {
                languages.forEach { option ->
                    DropdownMenuItem(
                        text = { Text(option.label) },
                        onClick = {
                            language = option.code
                            NativeClient.setLanguage(option.code)
                            languageMenuOpen = false
                        },
                    )
                }
            }
        }
        Text(
            NativeClient.string(NativeClient.STR_LANGUAGE_RESTART_HINT),
            style = MaterialTheme.typography.bodySmall,
            color = MutedColor,
        )

        SectionLabel(NativeClient.string(NativeClient.STR_SECTION_CONNECTION))
        OutlinedTextField(
            value = typed,
            onValueChange = { entered -> typed = entered.filter { it.isDigit() }.take(5) },
            label = { Text(NativeClient.string(NativeClient.STR_UDP_PORT_LABEL)) },
            supportingText = { Text(NativeClient.udpPortLine(port)) },
            singleLine = true,
            modifier = Modifier.fillMaxWidth(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
        )

        SectionLabel(NativeClient.string(NativeClient.STR_SECTION_SESSION))
        var clipboardSync by remember { mutableStateOf(NativeClient.clipboardSync()) }
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Checkbox(
                checked = clipboardSync,
                onCheckedChange = {
                    clipboardSync = it
                    NativeClient.setClipboardSync(it)
                },
            )
            Text(NativeClient.string(NativeClient.STR_CLIPBOARD_SYNC_LABEL))
        }
        var shareAudio by remember { mutableStateOf(NativeClient.shareAudio()) }
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Checkbox(
                checked = shareAudio,
                onCheckedChange = {
                    shareAudio = it
                    NativeClient.setShareAudio(it)
                },
            )
            Text(NativeClient.string(NativeClient.STR_SHARE_AUDIO_LABEL))
        }
        var acceptFiles by remember { mutableStateOf(NativeClient.acceptFiles()) }
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Checkbox(
                checked = acceptFiles,
                onCheckedChange = {
                    acceptFiles = it
                    NativeClient.setAcceptFiles(it)
                },
            )
            Text(NativeClient.string(NativeClient.STR_ACCEPT_FILES_LABEL))
        }
        var shareTerminal by remember { mutableStateOf(NativeClient.shareTerminal()) }
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Checkbox(
                checked = shareTerminal,
                onCheckedChange = {
                    shareTerminal = it
                    NativeClient.setShareTerminal(it)
                },
            )
            Text(NativeClient.string(NativeClient.STR_SHARE_TERMINAL_LABEL))
        }
        var playAudio by remember { mutableStateOf(NativeClient.playAudio()) }
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Checkbox(
                checked = playAudio,
                onCheckedChange = {
                    playAudio = it
                    NativeClient.setPlayAudio(it)
                },
            )
            Text(NativeClient.string(NativeClient.STR_PLAY_AUDIO_LABEL))
        }
        var keepAwake by remember { mutableStateOf(NativeClient.keepAwake()) }
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Checkbox(
                checked = keepAwake,
                onCheckedChange = {
                    keepAwake = it
                    NativeClient.setKeepAwake(it)
                },
            )
            Text(NativeClient.string(NativeClient.STR_KEEP_AWAKE_LABEL))
        }
        var encryptSession by remember { mutableStateOf(NativeClient.encryptSession()) }
        var escrowSessionKey by remember { mutableStateOf(NativeClient.escrowSessionKey()) }
        var sessionKeyLifetime by remember { mutableIntStateOf(NativeClient.sessionKeyLifetime()) }
        var hostSessionKey by remember { mutableStateOf(NativeClient.sessionKeyHex()) }
        val context = LocalContext.current
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Checkbox(
                checked = encryptSession,
                onCheckedChange = {
                    encryptSession = it
                    NativeClient.setEncryptSession(it)
                    if (!it) {
                        escrowSessionKey = false
                        NativeClient.setEscrowSessionKey(false)
                    } else {
                        NativeClient.ensureSessionKey(false)
                        hostSessionKey = NativeClient.sessionKeyHex()
                    }
                },
            )
            Column {
                Text(NativeClient.string(NativeClient.STR_ENCRYPT_SESSION_LABEL))
                Text(
                    NativeClient.string(NativeClient.STR_ENCRYPT_SESSION_HINT),
                    style = MaterialTheme.typography.bodySmall,
                    color = MutedColor,
                )
            }
        }
        if (encryptSession) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Checkbox(
                    checked = escrowSessionKey,
                    onCheckedChange = {
                        escrowSessionKey = it
                        NativeClient.setEscrowSessionKey(it)
                    },
                )
                Column {
                    Text(NativeClient.string(NativeClient.STR_ESCROW_SESSION_KEY_LABEL))
                    Text(
                        NativeClient.string(NativeClient.STR_ESCROW_SESSION_KEY_HINT),
                        style = MaterialTheme.typography.bodySmall,
                        color = MutedColor,
                    )
                }
            }
            Text(NativeClient.string(NativeClient.STR_SESSION_KEY_LIFETIME_LABEL))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilterChip(
                    selected = sessionKeyLifetime == 0,
                    onClick = {
                        sessionKeyLifetime = 0
                        NativeClient.setSessionKeyLifetime(0)
                    },
                    label = {
                        Text(NativeClient.string(NativeClient.STR_SESSION_KEY_LIFETIME_PER_SHARE))
                    },
                )
                FilterChip(
                    selected = sessionKeyLifetime == 1,
                    onClick = {
                        sessionKeyLifetime = 1
                        NativeClient.setSessionKeyLifetime(1)
                    },
                    label = {
                        Text(NativeClient.string(NativeClient.STR_SESSION_KEY_LIFETIME_PERSISTENT))
                    },
                )
            }
            Text(NativeClient.string(NativeClient.STR_SESSION_KEY_LABEL))
            Text(
                hostSessionKey.ifEmpty { "—" },
                style = MaterialTheme.typography.bodyMedium,
                fontFamily = FontFamily.Monospace,
            )
            Text(
                NativeClient.string(NativeClient.STR_SESSION_KEY_HINT),
                style = MaterialTheme.typography.bodySmall,
                color = MutedColor,
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                TextButton(
                    onClick = { copyToClipboard(context, hostSessionKey) },
                    enabled = hostSessionKey.isNotEmpty(),
                ) {
                    Text(NativeClient.string(NativeClient.STR_COPY_SESSION_KEY))
                }
                TextButton(
                    onClick = {
                        NativeClient.ensureSessionKey(true)
                        hostSessionKey = NativeClient.sessionKeyHex()
                    },
                ) {
                    Text(NativeClient.string(NativeClient.STR_REFRESH_SESSION_KEY))
                }
            }
        }

        SectionLabel(NativeClient.string(NativeClient.STR_PAIRED_HEADING))
        Text(
            NativeClient.string(NativeClient.STR_PAIRED_HINT),
            style = MaterialTheme.typography.bodyMedium,
            color = MutedColor,
        )
        var pairedDevices by remember { mutableStateOf(NativeClient.pairedDevices()) }
        var allowPairing by remember { mutableStateOf(NativeClient.allowPairing()) }
        var ownFingerprint by remember { mutableStateOf(NativeClient.ownFingerprint()) }
        var confirmForgetAll by remember { mutableStateOf(false) }
        val refreshPaired = {
            pairedDevices = NativeClient.pairedDevices()
            allowPairing = NativeClient.allowPairing()
            ownFingerprint = NativeClient.ownFingerprint()
        }
        LaunchedEffect(Unit) { refreshPaired() }
        if (pairedDevices.isEmpty()) {
            Text(
                NativeClient.string(NativeClient.STR_PAIRED_EMPTY),
                style = MaterialTheme.typography.bodyMedium,
                color = MutedColor,
            )
        } else {
            for (device in pairedDevices) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            device.name.ifEmpty { "(unnamed)" },
                            color = HeadingColor,
                        )
                        Text(
                            pairedDeviceSubtitle(device),
                            style = MaterialTheme.typography.bodySmall,
                            color = MutedColor,
                        )
                    }
                    TextButton(
                        onClick = {
                            NativeClient.pairedForget(device.fingerprint)
                            refreshPaired()
                        },
                    ) {
                        Text(NativeClient.string(NativeClient.STR_PAIRED_FORGET))
                    }
                }
            }
        }
        TextButton(
            onClick = { confirmForgetAll = true },
            enabled = pairedDevices.isNotEmpty(),
        ) {
            Text(NativeClient.string(NativeClient.STR_PAIRED_FORGET_ALL))
        }
        Text(
            NativeClient.string(NativeClient.STR_PAIRED_FORGET_NOTE),
            style = MaterialTheme.typography.bodySmall,
            color = MutedColor,
        )
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Checkbox(
                checked = allowPairing,
                onCheckedChange = {
                    allowPairing = it
                    NativeClient.setAllowPairing(it)
                },
            )
            Column {
                Text(NativeClient.string(NativeClient.STR_ALLOW_PAIRING_LABEL))
                Text(
                    NativeClient.string(NativeClient.STR_ALLOW_PAIRING_HINT),
                    style = MaterialTheme.typography.bodySmall,
                    color = MutedColor,
                )
            }
        }
        SectionLabel(NativeClient.string(NativeClient.STR_THIS_MACHINE_HEADING))
        Text(
            ownFingerprint,
            style = MaterialTheme.typography.bodyMedium,
            fontFamily = FontFamily.Monospace,
            color = HeadingColor,
        )
        Text(
            NativeClient.string(NativeClient.STR_THIS_MACHINE_HINT),
            style = MaterialTheme.typography.bodySmall,
            color = MutedColor,
        )
        if (confirmForgetAll) {
            AlertDialog(
                onDismissRequest = { confirmForgetAll = false },
                title = { Text(NativeClient.string(NativeClient.STR_PAIRED_FORGET_ALL)) },
                text = { Text(NativeClient.string(NativeClient.STR_PAIRED_FORGET_ALL_PROMPT)) },
                confirmButton = {
                    TextButton(
                        onClick = {
                            NativeClient.pairedForgetAll()
                            confirmForgetAll = false
                            refreshPaired()
                        },
                    ) {
                        Text(NativeClient.string(NativeClient.STR_PAIRED_FORGET_ALL))
                    }
                },
                dismissButton = {
                    TextButton(onClick = { confirmForgetAll = false }) { Text("Cancel") }
                },
            )
        }

        SectionLabel("Logs")
        var logMaxMb by remember { mutableStateOf(NativeClient.logMaxFileMb().toString()) }
        var logCompressDays by remember {
            mutableStateOf(NativeClient.logCompressAfterDays().toString())
        }
        var logDeleteDays by remember {
            mutableStateOf(NativeClient.logDeleteAfterDays().toString())
        }
        LaunchedEffect(logMaxMb, logCompressDays, logDeleteDays) {
            val maxMb = logMaxMb.toIntOrNull() ?: return@LaunchedEffect
            val compress = logCompressDays.toIntOrNull() ?: return@LaunchedEffect
            val delete = logDeleteDays.toIntOrNull() ?: return@LaunchedEffect
            if (maxMb < 1) return@LaunchedEffect
            delay(PORT_SETTLE_MS.milliseconds)
            if (maxMb == NativeClient.logMaxFileMb() &&
                compress == NativeClient.logCompressAfterDays() &&
                delete == NativeClient.logDeleteAfterDays()
            ) {
                return@LaunchedEffect
            }
            NativeClient.setLogPolicy(maxMb, compress, delete)
            logMaxMb = NativeClient.logMaxFileMb().toString()
            logCompressDays = NativeClient.logCompressAfterDays().toString()
            logDeleteDays = NativeClient.logDeleteAfterDays().toString()
        }
        OutlinedTextField(
            value = logMaxMb,
            onValueChange = { entered -> logMaxMb = entered.filter { it.isDigit() }.take(4) },
            label = { Text(NativeClient.string(NativeClient.STR_LOG_MAX_FILE_MB)) },
            singleLine = true,
            modifier = Modifier.fillMaxWidth(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
        )
        OutlinedTextField(
            value = logCompressDays,
            onValueChange = { entered ->
                logCompressDays = entered.filter { it.isDigit() }.take(4)
            },
            label = { Text(NativeClient.string(NativeClient.STR_LOG_COMPRESS_AFTER_DAYS)) },
            singleLine = true,
            modifier = Modifier.fillMaxWidth(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
        )
        OutlinedTextField(
            value = logDeleteDays,
            onValueChange = { entered -> logDeleteDays = entered.filter { it.isDigit() }.take(4) },
            label = { Text(NativeClient.string(NativeClient.STR_LOG_DELETE_AFTER_DAYS)) },
            singleLine = true,
            modifier = Modifier.fillMaxWidth(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
        )

        ProjectFooter()
    }
}

@Composable
private fun AddressScreen(
    address: String,
    onAddressChange: (String) -> Unit,
    connectPort: String,
    onConnectPortChange: (String) -> Unit,
    passcode: String,
    onPasscodeChange: (String) -> Unit,
    sessionKey: String,
    onSessionKeyChange: (String) -> Unit,
    deviceName: String,
    onDeviceNameChange: (String) -> Unit,
    busy: Boolean,
    error: String,
    onConnect: (String) -> Unit,
    scanHits: List<NativeClient.ScanHit>,
    recentDevices: List<NativeClient.RecentDevice>,
    scanStatus: String,
    recentNote: String,
    onPickDevice: (String, String) -> Unit,
    onRescan: () -> Unit,
    onRefreshStatus: () -> Unit,
    onForgetDevice: (String) -> Unit = {},
) {
    val trimmed = address.trim()
    val ready = trimmed.isNotEmpty() && NativeClient.isValidPasscode(passcode.trim()) && !busy
    val go = { if (ready) onConnect(NativeClient.composeAddress(trimmed, connectPort)) }

    Column(
        modifier =
            Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Heading(NativeClient.string(NativeClient.STR_CLIENT_HEADING))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            OutlinedTextField(
                value = address,
                onValueChange = onAddressChange,
                label = { Text(NativeClient.string(NativeClient.STR_CLIENT_IP_PROMPT)) },
                singleLine = true,
                enabled = !busy,
                modifier = Modifier.weight(1f),
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Go),
                keyboardActions = KeyboardActions(onGo = { go() }),
            )

            OutlinedTextField(
                value = connectPort,
                onValueChange = { typed ->
                    onConnectPortChange(typed.filter { it.isDigit() }.take(5))
                },
                label = { Text(NativeClient.string(NativeClient.STR_UDP_PORT_LABEL)) },
                singleLine = true,
                enabled = !busy,
                modifier = Modifier.width(110.dp),
                keyboardOptions =
                    KeyboardOptions(
                        keyboardType = KeyboardType.Number,
                        imeAction = ImeAction.Go,
                    ),
                keyboardActions = KeyboardActions(onGo = { go() }),
            )
        }

        OutlinedTextField(
            value = passcode,
            onValueChange = { typed ->
                onPasscodeChange(typed.filter { it.isDigit() }.take(NativeClient.passcodeDigits()))
            },
            label = { Text(NativeClient.string(NativeClient.STR_CLIENT_PASSCODE_PROMPT)) },
            supportingText = { Text(NativeClient.string(NativeClient.STR_CLIENT_PASSCODE_HINT)) },
            singleLine = true,
            enabled = !busy,
            modifier = Modifier.fillMaxWidth(),
            keyboardOptions =
                KeyboardOptions(
                    keyboardType = KeyboardType.NumberPassword,
                    imeAction = ImeAction.Next,
                ),
        )

        OutlinedTextField(
            value = sessionKey,
            onValueChange = onSessionKeyChange,
            label = { Text(NativeClient.string(NativeClient.STR_CLIENT_SESSION_KEY_PROMPT)) },
            supportingText = {
                Text(NativeClient.string(NativeClient.STR_CLIENT_SESSION_KEY_HINT))
            },
            singleLine = true,
            enabled = !busy,
            modifier = Modifier.fillMaxWidth(),
            keyboardOptions =
                KeyboardOptions(
                    keyboardType = KeyboardType.Ascii,
                    imeAction = ImeAction.Go,
                ),
            keyboardActions = KeyboardActions(onGo = { go() }),
        )

        OutlinedTextField(
            value = deviceName,
            onValueChange = onDeviceNameChange,
            label = { Text("Your name") },
            singleLine = true,
            enabled = !busy,
            modifier = Modifier.fillMaxWidth(),
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Go),
            keyboardActions = KeyboardActions(onGo = { go() }),
        )

        Button(
            onClick = go,
            enabled = ready,
            modifier = Modifier.fillMaxWidth(),
        ) { Text("Connect") }

        if (busy) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                CircularProgressIndicator(modifier = Modifier.size(20.dp), strokeWidth = 2.dp)
                Text(NativeClient.string(NativeClient.STR_QUERYING_SOURCES))
            }
        }

        var control by remember { mutableStateOf(NativeClient.clientControl()) }
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Checkbox(
                checked = control,
                onCheckedChange = {
                    control = it
                    NativeClient.setClientControl(it)
                },
                enabled = !busy,
            )
            Text(NativeClient.string(NativeClient.STR_REQUEST_CONTROL_LABEL))
        }

        DeviceSection(
            heading = NativeClient.string(NativeClient.STR_LAN_DEVICES_HEADING),
            note = scanStatus,
            rows = scanHits.map { DeviceRow(it.addr, it.ping, "", null) },
            enabled = !busy,
            onRefresh = onRescan,
            onPick = { addr -> onPickDevice(addr, NativeClient.recentPasscode(addr)) },
        )

        DeviceSection(
            heading = NativeClient.string(NativeClient.STR_RECENT_DEVICES_HEADING),
            note = recentNote,
            rows =
                recentDevices.map {
                    DeviceRow(it.addr, it.ping, "${it.status}  ${it.lastConnected}", it.online)
                },
            enabled = !busy,
            onRefresh = onRefreshStatus,
            onPick = { addr ->
                val known = recentDevices.firstOrNull { it.addr == addr }?.passcode
                onPickDevice(addr, known ?: NativeClient.recentPasscode(addr))
            },
            onForget = onForgetDevice,
        )

        if (error.isNotEmpty()) {
            Text(error, color = MaterialTheme.colorScheme.error)
        }
    }
}

@Composable
private fun ProjectFooter() {
    val uriHandler = LocalUriHandler.current
    val url = NativeClient.string(NativeClient.STR_PROJECT_URL)

    Column(
        modifier = Modifier.padding(top = 8.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Text(
            NativeClient.string(NativeClient.STR_PROJECT_LINK_LABEL),
            color = MaterialTheme.colorScheme.primary,
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier.clickable { uriHandler.openUri(url) },
        )
        Text(
            NativeClient.versionLine(),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

private data class DeviceRow(
    val addr: String,
    val ping: String,
    val detail: String,
    val online: Boolean?,
)

@Composable
private fun DeviceSection(
    heading: String,
    note: String,
    rows: List<DeviceRow>,
    enabled: Boolean,
    onRefresh: () -> Unit,
    onPick: (String) -> Unit,
    onForget: ((String) -> Unit)? = null,
) {
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        HeadingRow(heading, onRefresh)

        for (row in rows) {
            val tint =
                when (row.online) {
                    true -> OnlineColor
                    false -> OfflineColor
                    null -> HeadingColor
                }
            Row(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .clickable(enabled = enabled) { onPick(row.addr) }
                        .padding(vertical = 6.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(row.addr, color = tint)
                    if (row.detail.isNotBlank()) {
                        Text(
                            row.detail,
                            style = MaterialTheme.typography.bodySmall,
                            color = MutedColor,
                        )
                    }
                }
                Text(row.ping, style = MaterialTheme.typography.bodySmall, color = tint)
                if (onForget != null) {
                    TextButton(
                        onClick = { onForget(row.addr) },
                        enabled = enabled,
                    ) {
                        Text(NativeClient.string(NativeClient.STR_FORGET_DEVICE))
                    }
                }
            }
        }

        if (note.isNotEmpty()) {
            Text(note, style = MaterialTheme.typography.bodySmall, color = MutedColor)
        }
    }
}

@Composable
private fun ConnectedScreen(
    address: String,
    sources: List<NativeClient.Source>,
    caps: NativeClient.HostCaps,
    onOpenDesktop: () -> Unit,
    onOpenFiles: () -> Unit,
    onOpenShell: () -> Unit,
    onDisconnect: () -> Unit,
) {
    Column(
        modifier =
            Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Heading(NativeClient.string(NativeClient.STR_CLIENT_HEADING))
        Text(
            text = NativeClient.string(NativeClient.STR_CONNECTED_PICK_SESSION),
            style = MaterialTheme.typography.titleMedium,
        )
        Text(text = address, color = MutedColor)
        if (sources.isNotEmpty()) {
            Button(onClick = onOpenDesktop, modifier = Modifier.fillMaxWidth()) {
                Text(NativeClient.string(NativeClient.STR_OPEN_DESKTOP_LABEL))
            }
            if (caps.files) {
                OutlinedButton(onClick = onOpenFiles, modifier = Modifier.fillMaxWidth()) {
                    Text(NativeClient.string(NativeClient.STR_OPEN_FILES_LABEL))
                }
            }
            if (caps.terminal) {
                OutlinedButton(onClick = onOpenShell, modifier = Modifier.fillMaxWidth()) {
                    Text(NativeClient.string(NativeClient.STR_OPEN_SHELL_LABEL))
                }
            }
        } else if (caps.files) {
            Text(
                text = NativeClient.string(NativeClient.STR_ACCEPT_FILES_LABEL),
                color = MutedColor,
            )
        } else if (caps.terminal) {
            Button(onClick = onOpenShell, modifier = Modifier.fillMaxWidth()) {
                Text(NativeClient.string(NativeClient.STR_OPEN_SHELL_LABEL))
            }
        }
        OutlinedButton(onClick = onDisconnect, modifier = Modifier.fillMaxWidth()) {
            Text(NativeClient.string(NativeClient.STR_DISCONNECT_BUTTON))
        }
    }
}

@Composable
private fun SourcePickerScreen(
    address: String,
    sources: List<NativeClient.Source>,
    onPick: (NativeClient.Source) -> Unit,
) {
    var pickedId by remember { mutableIntStateOf(sources.first().id) }

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
