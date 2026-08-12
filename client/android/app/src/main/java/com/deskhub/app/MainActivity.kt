package com.deskhub.app

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.material3.MaterialTheme
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
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

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

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        NativeClient.useAppDataDir(this)
        NativeHost.publishScreenSize(this)
        askForNotifications()
        val prefs = getSharedPreferences("deskhub", Context.MODE_PRIVATE)
        prefs.edit().remove("passcode").apply()
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
                            onRemember = { addr, _ ->
                                prefs.edit().putString("addr", addr).apply()
                            },
                            onOpenStream = ::openStream,
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
        val manager = getSystemService(MediaProjectionManager::class.java) ?: return
        pendingShare = request
        NativeHost.awaitStart()
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

    data class Picking(
        val sources: List<NativeClient.Source>,
    ) : Step
}

@Composable
private fun MainScreen(
    initialAddress: String,
    initialPasscode: String,
    onRemember: (String, String) -> Unit,
    onOpenStream: (String, String, Int, List<NativeClient.Source>) -> Unit,
    onStartSharing: (HostService.ShareRequest) -> Unit,
    onStopSharing: () -> Unit,
) {
    var step by remember { mutableStateOf<Step>(Step.Address) }
    var address by remember { mutableStateOf(NativeClient.addressHost(initialAddress)) }
    var connectPort by remember { mutableStateOf(portFieldText(initialAddress)) }
    var passcode by remember { mutableStateOf(initialPasscode) }
    var connectError by remember { mutableStateOf("") }
    var querySeq by remember { mutableStateOf(0L) }
    var scanHits by remember { mutableStateOf(emptyList<NativeClient.ScanHit>()) }
    var recentDevices by remember { mutableStateOf(emptyList<NativeClient.RecentDevice>()) }
    var scanStatus by remember { mutableStateOf("") }
    var recentNote by remember { mutableStateOf("") }
    var pendingPick by remember { mutableStateOf<PendingPick?>(null) }
    var section by remember { mutableStateOf(Section.CLIENT) }
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
            delay(POLL_INTERVAL_MS)
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
        connectError = ""
        val mine = Step.Querying(++querySeq)
        step = mine
        scope.launch {
            val queried = NativeClient.listSources(addr, code)
            if (queried != null) {
                onRemember(addr, code)
                NativeClient.recentTouch(addr, code)
                NativeClient.watchRecent()
                recentDevices = NativeClient.recentDevices()
            }
            val sources = queried.orEmpty()
            if (step == mine) {
                val decision = NativeClient.connectDecision(sources)
                if (decision >= 0) {
                    step = Step.Address
                    onOpenStream(addr, code, decision, sources)
                } else {
                    step = Step.Picking(sources)
                }
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
            for (address in addresses) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    Text(address.name, modifier = Modifier.weight(1f), color = MutedColor)
                    Text(address.ip, fontWeight = FontWeight.Bold, color = HeadingColor)
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

        OutlinedTextField(
            value = typed,
            onValueChange = { entered -> typed = entered.filter { it.isDigit() }.take(5) },
            label = { Text(NativeClient.string(NativeClient.STR_UDP_PORT_LABEL)) },
            supportingText = { Text(NativeClient.udpPortLine(port)) },
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
                    imeAction = ImeAction.Go,
                ),
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
