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
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

private const val TAG = "Deskhub"

class MainActivity : ComponentActivity() {
    private var pendingShare: HostService.ShareRequest? = null

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
        FilesHost.bind(application)
        askForNotifications()
        val prefs = getSharedPreferences("deskhub", Context.MODE_PRIVATE)
        prefs.edit().remove("passcode").apply()
        val lastAddress = prefs.getString("addr", "").orEmpty()

        val debuggable = (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0
        var startSection = Section.CLIENT
        if (debuggable) {
            startSection = sectionExtra(intent)
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
                            initialSection = startSection,
                            initialAddress = lastAddress,
                            initialPasscode = NativeClient.recentPasscode(lastAddress),
                            onRemember = { addr, _ ->
                                prefs.edit().putString("addr", addr).apply()
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
    ) {
        startActivity(
            Intent(this, StreamActivity::class.java)
                .putExtra("addr", addr)
                .putExtra("passcode", passcode)
                .putExtra("source", sourceId)
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
private const val PORT_SETTLE_MS = 600L

private val HeadingColor = Color(0xFF111827)
private val MutedColor = Color(0xFF6B7280)
private val OnlineColor = Color(0xFF00913C)
private val OfflineColor = Color(0xFFC82828)

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
    DEVICES,
    SETTINGS,
}

private fun sectionExtra(intent: Intent?): Section {
    val name = intent?.getStringExtra("section") ?: return Section.CLIENT
    return Section.entries.firstOrNull { it.name.equals(name, ignoreCase = true) } ?: Section.CLIENT
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
    initialSection: Section,
    initialAddress: String,
    initialPasscode: String,
    onRemember: (String, String) -> Unit,
    onOpenStream: (String, String, Int, List<NativeClient.Source>) -> Unit,
    onOpenShell: (String, String) -> Unit,
    onStartSharing: (HostService.ShareRequest) -> Unit,
    onStopSharing: () -> Unit,
) {
    var step by remember { mutableStateOf<Step>(Step.Address) }
    var address by remember { mutableStateOf(NativeClient.addressHost(initialAddress)) }
    var connectPort by remember { mutableStateOf(portFieldText(initialAddress)) }
    var passcode by remember { mutableStateOf(initialPasscode) }
    var deviceName by remember {
        mutableStateOf(NativeClient.deviceName().ifBlank { Build.MODEL.orEmpty() })
    }
    var connectError by remember { mutableStateOf("") }
    var querySeq by remember { mutableStateOf(0L) }
    var deviceRows by remember { mutableStateOf(emptyList<NativeClient.DeviceRow>()) }
    var scanStatus by remember { mutableStateOf("") }
    var pendingPick by remember { mutableStateOf<PendingPick?>(null) }
    var sendingTo by remember { mutableStateOf<FileSendDriver?>(null) }
    var section by remember { mutableStateOf(initialSection) }
    var port by remember { mutableStateOf(NativeClient.settingsPort()) }
    val scope = rememberCoroutineScope()
    val rescanTicks = remember { NativeClient.rescanSeconds() }

    BackHandler(enabled = step != Step.Address) { step = Step.Address }

    DisposableEffect(Unit) {
        onDispose { NativeClient.scanCancel() }
    }

    LaunchedEffect(port) {
        NativeClient.watchRecent()
        NativeClient.scanRestart(port)
        var idleTicks = 0
        while (true) {
            deviceRows = NativeClient.deviceRows()
            scanStatus = NativeClient.scanStatusText(port)
            if (NativeClient.scanRunning()) {
                idleTicks = 0
            } else {
                idleTicks++
                if (idleTicks >= rescanTicks) {
                    idleTicks = 0
                    NativeClient.scanStart(port)
                }
            }
            delay(POLL_INTERVAL_MS)
        }
    }

    val connect: (String) -> Unit = connectLambda@{ addr ->
        if (!NativeClient.parseAddress(addr)) {
            connectError = NativeClient.string(NativeClient.STR_INVALID_ADDRESS_HINT)
            return@connectLambda
        }
        val code = passcode.trim()
        if (code.isNotEmpty() && !NativeClient.isValidPasscode(code)) {
            connectError = NativeClient.string(NativeClient.STR_PASSCODE_INVALID)
            return@connectLambda
        }
        connectError = ""
        deviceName = deviceName.trim().ifBlank { Build.MODEL.orEmpty() }
        NativeClient.setDeviceName(deviceName)
        val mine = Step.Querying(++querySeq)
        step = mine
        scope.launch {
            val queried = NativeClient.listSources(addr, code)
            if (queried.isNullOrEmpty()) {
                if (step == mine) {
                    step = Step.Address
                    connectError =
                        if (queried == null) {
                            NativeClient.sourceQueryFailed(addr)
                        } else {
                            NativeClient.sourceQueryEmpty(addr)
                        }
                }
                return@launch
            }
            onRemember(addr, code)
            NativeClient.recentTouch(addr, code)
            NativeClient.watchRecent()
            deviceRows = NativeClient.deviceRows()
            if (step == mine) {
                val decision = NativeClient.connectDecision(queried)
                if (decision >= 0) {
                    step = Step.Address
                    onOpenStream(addr, code, decision, queried)
                } else {
                    step = Step.Picking(queried)
                }
            }
        }
    }

    val openShell: (String) -> Unit = shellLambda@{ addr ->
        if (!NativeClient.parseAddress(addr)) {
            connectError = NativeClient.string(NativeClient.STR_INVALID_ADDRESS_HINT)
            return@shellLambda
        }
        val code = passcode.trim()
        if (code.isNotEmpty() && !NativeClient.isValidPasscode(code)) {
            connectError = NativeClient.string(NativeClient.STR_PASSCODE_INVALID)
            return@shellLambda
        }
        connectError = ""
        deviceName = deviceName.trim().ifBlank { Build.MODEL.orEmpty() }
        NativeClient.setDeviceName(deviceName)
        val mine = Step.Querying(++querySeq)
        step = mine
        scope.launch {
            val shared = NativeClient.hostHasTerminal(addr, code)
            if (step == mine) step = Step.Address
            if (!shared) {
                connectError = NativeClient.string(NativeClient.STR_HOST_HAS_NO_TERMINAL)
                return@launch
            }
            onRemember(addr, code)
            NativeClient.recentTouch(addr, code)
            NativeClient.watchRecent()
            deviceRows = NativeClient.deviceRows()
            onOpenShell(addr, code)
        }
    }

    val openFileSend: (String) -> Unit = sendLambda@{ addr ->
        if (!NativeClient.parseAddress(addr)) {
            connectError = NativeClient.string(NativeClient.STR_INVALID_ADDRESS_HINT)
            return@sendLambda
        }
        val code = passcode.trim()
        if (code.isNotEmpty() && !NativeClient.isValidPasscode(code)) {
            connectError = NativeClient.string(NativeClient.STR_PASSCODE_INVALID)
            return@sendLambda
        }
        connectError = ""
        deviceName = deviceName.trim().ifBlank { Build.MODEL.orEmpty() }
        NativeClient.setDeviceName(deviceName)
        val mine = Step.Querying(++querySeq)
        step = mine
        scope.launch {
            val takes = NativeClient.hostTakesFiles(addr, code)
            if (step == mine) step = Step.Address
            if (!takes) {
                connectError = NativeClient.string(NativeClient.STR_TRANSFER_HOST_NOT_TAKING)
                return@launch
            }
            onRemember(addr, code)
            NativeClient.recentTouch(addr, code)
            NativeClient.watchRecent()
            deviceRows = NativeClient.deviceRows()
            sendingTo = StandaloneFileSendDriver(addr, code, deviceName)
        }
    }

    val pickDevice: (String, String) -> Unit = { addr, code ->
        connectError = ""
        pendingPick = PendingPick(addr, code)
    }

    sendingTo?.let { driver ->
        FileSendDialog(
            driver = driver,
            subtitle = address,
            onDismiss = { sendingTo = null },
        )
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
                deviceName = deviceName,
                onDeviceNameChange = { deviceName = it },
                busy = step is Step.Querying,
                error = connectError,
                onConnect = connect,
                onOpenShell = openShell,
                onOpenFileSend = openFileSend,
                deviceRows = deviceRows,
                scanStatus = scanStatus,
                onPickDevice = pickDevice,
                onRescan = { scope.launch { NativeClient.scanRestart(port) } },
                onRefreshStatus = { scope.launch { NativeClient.statusRefreshNow() } },
                port = port,
                onPortChange = { chosen ->
                    NativeClient.setSettingsPort(chosen)
                    port = chosen
                },
                onStartSharing = onStartSharing,
                onStopSharing = onStopSharing,
            )

        is Step.Picking ->
            SourcePickerScreen(
                address = address,
                sources = s.sources,
                onPick = { source ->
                    step = Step.Address
                    onOpenStream(address, passcode.trim(), source.id, s.sources)
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
                connect(chosenAddr)
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
    clipboard.setPrimaryClip(ClipData.newPlainText("Deskhub", text))
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
        Toast.makeText(context, "Copied", Toast.LENGTH_SHORT).show()
    }
}

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
    val ready = typed.trim().isEmpty() || NativeClient.isValidPasscode(typed.trim())
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
                        typed =
                            entered.filter { it.isDigit() }.take(NativeClient.passcodeDigits())
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
    deviceName: String,
    onDeviceNameChange: (String) -> Unit,
    busy: Boolean,
    error: String,
    onConnect: (String) -> Unit,
    onOpenShell: (String) -> Unit,
    onOpenFileSend: (String) -> Unit,
    deviceRows: List<NativeClient.DeviceRow>,
    scanStatus: String,
    onPickDevice: (String, String) -> Unit,
    onRescan: () -> Unit,
    onRefreshStatus: () -> Unit,
    port: Int,
    onPortChange: (Int) -> Unit,
    onStartSharing: (HostService.ShareRequest) -> Unit,
    onStopSharing: () -> Unit,
) {
    Column(modifier = Modifier.fillMaxSize()) {
        TabRow(selectedTabIndex = section.ordinal) {
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
                selected = section == Section.DEVICES,
                onClick = { onSectionChange(Section.DEVICES) },
                text = { Text(NativeClient.string(NativeClient.STR_SIDEBAR_DEVICES)) },
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
                        deviceName = deviceName,
                        onDeviceNameChange = onDeviceNameChange,
                        busy = busy,
                        error = error,
                        onConnect = onConnect,
                        onOpenShell = onOpenShell,
                        onOpenFileSend = onOpenFileSend,
                        deviceRows = deviceRows,
                        scanStatus = scanStatus,
                        onPickDevice = onPickDevice,
                        onRescan = onRescan,
                        onRefreshStatus = onRefreshStatus,
                    )

                Section.HOST ->
                    HostScreen(
                        port = port,
                        onStartSharing = onStartSharing,
                        onStopSharing = onStopSharing,
                    )

                Section.DEVICES -> DevicesScreen()

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
    var pairingQueue by remember { mutableStateOf(emptyList<NativeHost.PairingRequest>()) }

    LaunchedEffect(Unit) {
        while (true) {
            state = NativeHost.shareState
            error = NativeHost.shareError
            rows = if (state == NativeHost.ShareState.SHARING) NativeHost.hostRows() else emptyList()
            addresses = NativeHost.localAddresses()
            if (state == NativeHost.ShareState.SHARING) {
                val fresh = NativeHost.takePairingRequests()
                if (fresh.isNotEmpty()) {
                    val queued = pairingQueue.map { it.addrPacked }.toSet()
                    pairingQueue = pairingQueue + fresh.filter { it.addrPacked !in queued }
                }
            } else if (pairingQueue.isNotEmpty()) {
                pairingQueue = emptyList()
            }
            if (state == NativeHost.ShareState.SHARING && !NativeHost.isRunning()) onStopSharing()
            delay(POLL_INTERVAL_MS)
        }
    }

    pairingQueue.firstOrNull()?.let { request ->
        AlertDialog(
            onDismissRequest = {},
            title = { Text(NativeClient.string(NativeClient.STR_PAIRING_REQUEST_TITLE)) },
            text = { Text(request.body) },
            confirmButton = {
                TextButton(
                    onClick = {
                        NativeHost.answerPairing(request.addrPacked, true)
                        pairingQueue = pairingQueue.drop(1)
                    },
                ) { Text(NativeClient.string(NativeClient.STR_PAIRING_ALLOW)) }
            },
            dismissButton = {
                TextButton(
                    onClick = {
                        NativeHost.answerPairing(request.addrPacked, false)
                        pairingQueue = pairingQueue.drop(1)
                    },
                ) { Text(NativeClient.string(NativeClient.STR_PAIRING_DENY)) }
            },
        )
    }

    val sharing = state == NativeHost.ShareState.SHARING
    val starting = state == NativeHost.ShareState.STARTING
    val trimmedCode = passcode.trim()
    val ready = trimmedCode.isEmpty() || NativeClient.isValidPasscode(trimmedCode)

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

        var takeFiles by remember { mutableStateOf(NativeClient.takeFiles()) }
        var receiving by remember { mutableStateOf(NativeHost.filesActive()) }
        LaunchedEffect(Unit) {
            while (true) {
                receiving = NativeHost.filesActive()
                delay(POLL_INTERVAL_MS)
            }
        }
        Button(
            onClick = {
                takeFiles = !takeFiles
                NativeClient.setTakeFiles(takeFiles)
            },
            enabled = !sharing && !starting,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text(
                NativeClient.string(
                    if (takeFiles) {
                        NativeClient.STR_TRANSFER_STOP_TAKING_BUTTON
                    } else {
                        NativeClient.STR_TRANSFER_ACCEPT_LABEL
                    },
                ),
            )
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
            enabled = sharing || (ready && !starting && !receiving),
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

        if (receiving) {
            Text(
                NativeClient.string(NativeClient.STR_TRANSFER_BLOCKS_SCREEN_NOTE),
                style = MaterialTheme.typography.bodyMedium,
                color = MutedColor,
            )
        }

        Text(
            if (sharing || receiving) {
                NativeHost.sharingStatus(port, passcode.trim(), sharing)
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
                        Text("Copy")
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
private fun DevicesScreen() {
    var devices by remember { mutableStateOf(NativeClient.pairedDevices()) }
    var allowPairing by remember { mutableStateOf(NativeClient.allowPairing()) }
    var confirmForgetAll by remember { mutableStateOf(false) }
    val dateText: (Long) -> String = { unix ->
        if (unix <= 0) {
            "-"
        } else {
            java.text
                .SimpleDateFormat("yyyy-MM-dd HH:mm", java.util.Locale.US)
                .format(java.util.Date(unix * 1000))
        }
    }

    if (confirmForgetAll) {
        AlertDialog(
            onDismissRequest = { confirmForgetAll = false },
            title = { Text(NativeClient.string(NativeClient.STR_PAIRED_FORGET_ALL)) },
            text = { Text(NativeClient.string(NativeClient.STR_PAIRED_FORGET_ALL_PROMPT)) },
            confirmButton = {
                TextButton(
                    onClick = {
                        NativeClient.pairedForgetAll()
                        devices = NativeClient.pairedDevices()
                        confirmForgetAll = false
                    },
                ) { Text(NativeClient.string(NativeClient.STR_PAIRED_FORGET_ALL)) }
            },
            dismissButton = {
                TextButton(onClick = { confirmForgetAll = false }) { Text("Cancel") }
            },
        )
    }

    Column(
        modifier =
            Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Heading(NativeClient.string(NativeClient.STR_PAIRED_HEADING))
        Text(
            NativeClient.string(NativeClient.STR_PAIRED_HINT),
            style = MaterialTheme.typography.bodyMedium,
            color = MutedColor,
        )

        if (devices.isEmpty()) {
            Text(
                NativeClient.string(NativeClient.STR_PAIRED_EMPTY),
                style = MaterialTheme.typography.bodyMedium,
                color = MutedColor,
            )
        } else {
            for (device in devices) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            device.name.ifBlank { "(unnamed)" },
                            color = HeadingColor,
                        )
                        Text(
                            "${device.shortKey}  ·  ${dateText(device.pairedUnix)}  ·  " +
                                dateText(device.lastSeenUnix),
                            style = MaterialTheme.typography.bodySmall,
                            color = MutedColor,
                        )
                    }
                    TextButton(
                        onClick = {
                            NativeClient.pairedForget(device.fingerprint)
                            devices = NativeClient.pairedDevices()
                        },
                    ) { Text(NativeClient.string(NativeClient.STR_PAIRED_FORGET)) }
                }
            }
        }

        TextButton(
            onClick = { confirmForgetAll = true },
            enabled = devices.isNotEmpty(),
        ) { Text(NativeClient.string(NativeClient.STR_PAIRED_FORGET_ALL)) }

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
            Text(NativeClient.string(NativeClient.STR_ALLOW_PAIRING_LABEL))
        }
        Text(
            NativeClient.string(NativeClient.STR_ALLOW_PAIRING_HINT),
            style = MaterialTheme.typography.bodySmall,
            color = MutedColor,
        )

        SectionLabel(NativeClient.string(NativeClient.STR_THIS_MACHINE_HEADING))
        Text(
            NativeClient.ownFingerprint(),
            style = MaterialTheme.typography.bodySmall,
            color = HeadingColor,
        )
        Text(
            NativeClient.string(NativeClient.STR_THIS_MACHINE_HINT),
            style = MaterialTheme.typography.bodySmall,
            color = MutedColor,
        )
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
        delay(PORT_SETTLE_MS)
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
    deviceName: String,
    onDeviceNameChange: (String) -> Unit,
    busy: Boolean,
    error: String,
    onConnect: (String) -> Unit,
    onOpenShell: (String) -> Unit,
    onOpenFileSend: (String) -> Unit,
    deviceRows: List<NativeClient.DeviceRow>,
    scanStatus: String,
    onPickDevice: (String, String) -> Unit,
    onRescan: () -> Unit,
    onRefreshStatus: () -> Unit,
) {
    val trimmed = address.trim()
    val code = passcode.trim()
    val codeOk = code.isEmpty() || NativeClient.isValidPasscode(code)
    val ready = trimmed.isNotEmpty() && codeOk && !busy
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

        OutlinedButton(
            onClick = { onOpenShell(NativeClient.composeAddress(trimmed, connectPort)) },
            enabled = ready,
            modifier = Modifier.fillMaxWidth(),
        ) { Text(NativeClient.string(NativeClient.STR_OPEN_SHELL_LABEL)) }

        OutlinedButton(
            onClick = { onOpenFileSend(NativeClient.composeAddress(trimmed, connectPort)) },
            enabled = ready,
            modifier = Modifier.fillMaxWidth(),
        ) { Text(NativeClient.string(NativeClient.STR_OPEN_FILES_LABEL)) }

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
            heading = NativeClient.string(NativeClient.STR_DEVICES_HEADING),
            note = scanStatus,
            rows =
                deviceRows.map { row ->
                    DeviceRow(
                        row.addr,
                        row.ping,
                        listOf(row.origin, row.status, row.lastConnected)
                            .filter { it.isNotEmpty() }
                            .joinToString("  "),
                        if (row.known) row.online else null,
                    )
                },
            enabled = !busy,
            onRefresh = {
                onRefreshStatus()
                onRescan()
            },
            onPick = { addr ->
                val known = deviceRows.firstOrNull { it.addr == addr }?.passcode.orEmpty()
                onPickDevice(addr, known.ifEmpty { NativeClient.recentPasscode(addr) })
            },
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
            }
        }

        if (note.isNotEmpty()) {
            Text(note, style = MaterialTheme.typography.bodySmall, color = MutedColor)
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
