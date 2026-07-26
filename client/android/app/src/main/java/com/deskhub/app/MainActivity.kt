// =============================================================================
// MainActivity.kt — màn hình đầu tiên: nhập địa chỉ host rồi chọn cửa sổ muốn xem.
//                   Giao diện dựng trên hệ thiết kế Deskhub (ui/Tokens.kt +
//                   ui/Components.kt), đối ứng ConnectView/SourcePickerView bên iOS.
//
// NHIỆM VỤ
//   Dẫn người dùng qua ba bước tới lúc mở được StreamActivity. Nhớ danh sách máy đã
//   kết nối (ui/Recents.kt — tối đa 12, thay cho một địa chỉ duy nhất trước đây) để
//   lần sau chỉ cần một cú chạm thay vì gõ lại IP trên bàn phím ảo.
//
// BA BƯỚC, MÔ HÌNH HOÁ BẰNG sealed interface Step
//   Address  — gõ địa chỉ.
//   Querying — đang hỏi host có những nguồn nào (chặn tới 3 giây, có vòng quay).
//   Picking  — host trả về nhiều nguồn, cho chọn.
//   Dùng sealed interface thay cho vài biến boolean rời rạc: trình dịch bắt buộc
//   `when` phải phủ hết mọi nhánh, nên thêm bước mới sau này không thể quên chỗ nào.
//
// ĐƯỜNG TẮT: BỎ QUA BƯỚC CHỌN
//   Host im lặng (bản trước GĐ6 không biết LIST_SOURCES) hoặc chỉ chia sẻ một cửa
//   sổ → vào thẳng, không bắt chọn giữa một lựa chọn duy nhất. Lỗi thật, nếu có, sẽ
//   do tầng dưới báo lên ở StreamActivity.
//
// "CHỈ XEM" LÀ CỜ TOÀN CỤC PHÍA KOTLIN (NativeClient.viewOnly)
//   Được tick ở màn này, đọc ở StreamActivity. Chặn ở NativeClient — cửa duy nhất
//   xuống C++ — chứ không ở từng chỗ gửi input: một chỗ quên kiểm tra là cả lựa chọn
//   này thành vô nghĩa mà không ai biết.
//
// VÌ SAO CÓ ĐƯỜNG CHẠY THẲNG TỪ adb
//   Truyền extra "addr" là mở luôn StreamActivity, bỏ qua mọi bước. Dùng để test
//   nhanh mà không phải gõ IP trên bàn phím ảo — xem lệnh cụ thể trong onCreate.
//
// LIÊN QUAN: StreamActivity.kt (màn hình tiếp theo), NativeClient.kt (listSources)
// =============================================================================
package com.deskhub.app

import android.content.Context
import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.deskhub.app.ui.AppMark
import com.deskhub.app.ui.AppState
import com.deskhub.app.ui.Credentials
import com.deskhub.app.ui.DeskhubTheme
import com.deskhub.app.ui.Ds
import com.deskhub.app.ui.DsButton
import com.deskhub.app.ui.DsButtonSize
import com.deskhub.app.ui.DsButtonVariant
import com.deskhub.app.ui.DsCheckbox
import com.deskhub.app.ui.HeroField
import com.deskhub.app.ui.LockIcon
import com.deskhub.app.ui.MachineCard
import com.deskhub.app.ui.MonoText
import com.deskhub.app.ui.Panel
import com.deskhub.app.ui.PillTone
import com.deskhub.app.ui.Recents
import com.deskhub.app.ui.ScreenHeader
import com.deskhub.app.ui.SectionHeader
import com.deskhub.app.ui.SourceRow
import com.deskhub.app.ui.Spinner
import com.deskhub.app.ui.StatBlock
import com.deskhub.app.ui.StatusDot
import com.deskhub.app.ui.TopBar
import com.deskhub.app.ui.cobaltGlow
import com.deskhub.app.ui.tr
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        AppState.init(this)
        Recents.init(this)
        Credentials.init(this)
        val prefs = getSharedPreferences("deskhub", Context.MODE_PRIVATE)
        // NativeClient không có Context nên phần lưu "chỉ xem" nằm ở đây.
        NativeClient.viewOnly = prefs.getBoolean("viewOnly", false)

        // Vẫn cho chạy thẳng từ adb để test nhanh (bỏ qua bước chọn nguồn):
        //   am start -n com.deskhub.app/.MainActivity --es addr 10.0.2.2:47777
        intent?.getStringExtra("addr")?.let { addr ->
            intent.removeExtra("addr") // chỉ dùng một lần, quay lại không tự nhảy nữa
            openStream(addr, 0)
        }

        setContent {
            DeskhubTheme(dark = AppState.isDark) {
                Box(
                    modifier =
                        Modifier
                            .fillMaxSize()
                            .background(Ds.colors.bgWindow)
                            .cobaltGlow()
                            .safeDrawingPadding(),
                ) {
                    MainScreen(
                        initialAddress = prefs.getString("addr", "").orEmpty(),
                        onRemember = { addr ->
                            prefs.edit().putString("addr", addr).apply()
                            Recents.remember(addr)
                        },
                        onViewOnlyChange = { on ->
                            NativeClient.viewOnly = on
                            prefs.edit().putBoolean("viewOnly", on).apply()
                        },
                        onOpenStream = ::openStream,
                    )
                }
            }
        }
    }

    private fun openStream(
        addr: String,
        sourceId: Int,
    ) {
        startActivity(
            Intent(this, StreamActivity::class.java)
                .putExtra("addr", addr)
                .putExtra("source", sourceId),
        )
    }
}

/** Ba bước của màn hình: gõ địa chỉ -> hỏi host có nguồn nào -> chọn nguồn. */
private sealed interface Step {
    data object Address : Step

    data object Querying : Step

    data class Picking(
        val sources: List<NativeClient.Source>,
    ) : Step
}

@Composable
private fun MainScreen(
    initialAddress: String,
    onRemember: (String) -> Unit,
    onViewOnlyChange: (Boolean) -> Unit,
    onOpenStream: (String, Int) -> Unit,
) {
    var step by remember { mutableStateOf<Step>(Step.Address) }
    var address by remember { mutableStateOf(initialAddress) }
    val scope = rememberCoroutineScope()

    // Đang hỏi hoặc đang chọn: Back quay về ô địa chỉ thay vì thoát app. Coroutine hỏi
    // nguồn vẫn chạy nốt 3 giây của nó (lời gọi JNI đang chặn, không hủy giữa chừng
    // được), nhưng kết quả bị bỏ qua nhờ phép kiểm tra step ở chỗ nhận kết quả.
    BackHandler(enabled = step != Step.Address) { step = Step.Address }

    val connect: (String) -> Unit = { addr ->
        onRemember(addr)
        step = Step.Querying
        scope.launch {
            // listSources là suspend fun, tự chuyển sang Dispatchers.IO — main
            // thread không bị chặn suốt 3 giây (nếu chặn, Android dựng hộp ANR).
            val sources = NativeClient.listSources(addr)
            if (step is Step.Querying) {
                // Rỗng = host im lặng hoặc host đời cũ; một nguồn = không có gì để
                // chọn. Cả hai vào thẳng, để tầng dưới báo lỗi thật nếu có.
                if (sources.size <= 1) {
                    step = Step.Address
                    onOpenStream(addr, sources.firstOrNull()?.id ?: 0)
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
                onViewOnlyChange = onViewOnlyChange,
                onConnect = connect,
            )

        // Bước hỏi giữ nguyên bố cục màn địa chỉ (chỉ đổi dòng phụ + vòng quay trong
        // ô): nhảy sang một màn "đang tìm…" trống trải rồi quay lại là hai lần giật.
        is Step.Querying ->
            AddressScreen(
                address = address,
                onAddressChange = {},
                busy = true,
                onViewOnlyChange = onViewOnlyChange,
                onConnect = {},
            )

        is Step.Picking ->
            SourcePickerScreen(
                address = address,
                sources = s.sources,
                onViewOnlyChange = onViewOnlyChange,
                onPick = { source ->
                    step = Step.Address // quay lại từ StreamActivity là thấy ô địa chỉ
                    onOpenStream(address, source.id)
                },
            )
    }
}

@Composable
private fun AddressScreen(
    address: String,
    onAddressChange: (String) -> Unit,
    busy: Boolean,
    onViewOnlyChange: (Boolean) -> Unit,
    onConnect: (String) -> Unit,
) {
    val trimmed = address.trim()
    val go = { if (trimmed.isNotEmpty() && !busy) onConnect(trimmed) }
    var recents by remember { mutableStateOf(Recents.all) }
    // GĐ10 — mật khẩu/token đã lưu, cho mục "Saved passwords" bên dưới.
    var savedCreds by remember { mutableStateOf(Credentials.all) }

    Column(modifier = Modifier.fillMaxSize()) {
        TopBar()

        Column(
            modifier =
                Modifier
                    .weight(1f)
                    .verticalScroll(rememberScrollState())
                    .padding(horizontal = Ds.screenGutter, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(22.dp),
        ) {
            ScreenHeader(eyebrow = tr("clientMode"), title = tr("connectTitle"))

            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                HeroField(
                    value = address,
                    onValueChange = onAddressChange,
                    placeholder = tr("addressPlaceholder"),
                    onGo = go,
                ) {
                    if (busy) Spinner(size = 18.dp)
                }
                MonoText(text = if (busy) tr("askingSources") else tr("connectHint"))
            }

            // "Gần đây" đứng NGAY DƯỚI ô nhập, trên cả các panel hướng dẫn: lần mở
            // app thứ hai trở đi, thứ người dùng cần là một cú chạm vào cái máy hôm
            // qua vừa dùng — không phải đọc lại hướng dẫn.
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                SectionHeader(label = tr("recentConnections")) {
                    DsButton(
                        text = tr("forgetAll"),
                        onClick = {
                            Recents.forgetAll()
                            recents = Recents.all
                        },
                        variant = DsButtonVariant.GHOST,
                        size = DsButtonSize.SM,
                        enabled = recents.isNotEmpty(),
                    )
                }
                if (recents.isEmpty()) {
                    MonoText(text = tr("nothingRemembered"))
                } else {
                    recents.forEach { machine ->
                        // Máy đã lưu: ta chưa dò lại nên KHÔNG khẳng định nó đang
                        // sống — MachineCard vẽ chấm "không sống", không bịa độ trễ.
                        MachineCard(
                            name = machine.displayName,
                            address = machine.address,
                            link = machine.link,
                            onTap = { onAddressChange(machine.address) },
                        )
                    }
                }
            }

            // GĐ10 — "Saved passwords" của màn `05 · settings / password`. Đặt ở đây
            // thay vì dựng một màn Settings riêng: chỉ có đúng một việc để làm (xoá),
            // và nó thuộc cùng một câu hỏi với danh sách trên ("máy nào tôi đã dùng").
            // Ẩn hẳn khi chưa lưu gì — một mục rỗng chỉ làm màn hình dài thêm.
            if (savedCreds.isNotEmpty()) {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    SectionHeader(label = tr("savedHosts")) {
                        MonoText(text = tr("savedCountFmt").format(savedCreds.size))
                        DsButton(
                            text = tr("forgetAll"),
                            onClick = {
                                Credentials.forgetAll()
                                savedCreds = Credentials.all
                            },
                            variant = DsButtonVariant.GHOST,
                            size = DsButtonSize.SM,
                        )
                    }
                    savedCreds.forEach { cred ->
                        // Dựng tay thay vì dùng SourceRow: SourceRow là dòng CHỌN
                        // nguồn (cả dòng là một nút, có ô tick), còn dòng này chỉ
                        // hiển thị và có một nút riêng ở đuôi. Nhét thêm slot vào
                        // SourceRow sẽ làm hỏng ý nghĩa của nó ở ba chỗ đang dùng.
                        val shape = RoundedCornerShape(Ds.radiusMd)
                        Row(
                            modifier =
                                Modifier
                                    .fillMaxWidth()
                                    .background(Ds.colors.surfaceCard, shape)
                                    .border(Ds.hairline, Ds.colors.borderHairline, shape)
                                    .padding(horizontal = 14.dp, vertical = 10.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(10.dp),
                        ) {
                            LockIcon(size = 18.dp, color = Ds.colors.accent)
                            Column(
                                modifier = Modifier.weight(1f),
                                verticalArrangement = Arrangement.spacedBy(2.dp),
                            ) {
                                MonoText(
                                    text = cred.address,
                                    color = Ds.colors.textPrimary,
                                    maxLines = 1,
                                )
                                // Nói rõ máy này đang được nhớ bằng CÁI GÌ: có token
                                // thì lần sau vào thẳng, chỉ có mật khẩu thì vẫn phải
                                // qua challenge. Hai trạng thái đó khác nhau thật.
                                MonoText(
                                    text =
                                        if (cred.hasToken) {
                                            tr("savePassword")
                                        } else {
                                            tr("connectPassword")
                                        },
                                    maxLines = 1,
                                )
                            }
                            DsButton(
                                text = tr("forget"),
                                onClick = {
                                    Credentials.forget(cred.address)
                                    savedCreds = Credentials.all
                                },
                                variant = DsButtonVariant.SECONDARY,
                                size = DsButtonSize.SM,
                            )
                        }
                    }
                }
            }

            HelpPanels()
        }

        // Thanh đáy: vùng DUY NHẤT ngón cái với tới thoải mái khi cầm một tay — nút
        // chính của màn nằm ở đây, cao 52.
        Box(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .height(Ds.hairline)
                    .background(Ds.colors.borderHairline),
        )
        Column(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .background(Ds.colors.surfaceCard)
                    .padding(horizontal = Ds.screenGutter, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            DsCheckbox(
                checked = NativeClient.viewOnly,
                onToggle = onViewOnlyChange,
                label = tr("viewOnlyOption"),
            )
            DsButton(
                text = tr("connect"),
                onClick = go,
                variant = DsButtonVariant.PRIMARY,
                size = DsButtonSize.LG,
                enabled = trimmed.isNotEmpty() && !busy,
                fullWidth = true,
            )
        }
    }
}

@Composable
private fun HelpPanels() {
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Panel(label = tr("onOtherMachine")) {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    text = tr("openAndShare"),
                    fontSize = Ds.textBody,
                    fontWeight = FontWeight.Medium,
                    color = Ds.colors.textPrimary,
                )
                MonoText(text = tr("shareOrExe"))
            }
        }
        Panel(label = tr("whereAddress")) {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    text = tr("printedOnShare"),
                    fontSize = Ds.textBody,
                    fontWeight = FontWeight.Medium,
                    color = Ds.colors.textPrimary,
                )
                MonoText(text = tr("onePerInterfaceLong"))
            }
        }
        Panel(label = tr("port")) {
            Row(
                verticalAlignment = Alignment.Bottom,
                horizontalArrangement = Arrangement.spacedBy(18.dp),
            ) {
                StatBlock(label = tr("udp"), value = "47777")
                MonoText(text = tr("portChangeable"))
            }
        }
        MonoText(text = tr("emulatorHint"))
    }
}

/**
 * Danh sách cửa sổ host đang chia sẻ, ô tick hành xử như radio — dh_start nhận MỘT
 * sourceId, chọn cái mới là bỏ cái cũ. Nút "Bắt đầu xem" ở thanh đáy chốt lựa chọn.
 */
@Composable
private fun SourcePickerScreen(
    address: String,
    sources: List<NativeClient.Source>,
    onViewOnlyChange: (Boolean) -> Unit,
    onPick: (NativeClient.Source) -> Unit,
) {
    var pickedId by remember { mutableStateOf(sources.first().id) }

    Column(modifier = Modifier.fillMaxSize()) {
        TopBar {
            AppMark()
        }

        Column(
            modifier =
                Modifier
                    .weight(1f)
                    .verticalScroll(rememberScrollState())
                    .padding(horizontal = Ds.screenGutter, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(22.dp),
        ) {
            ScreenHeader(eyebrow = tr("clientMode"), title = tr("pickTitle"), aside = address)

            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                SectionHeader(label = tr("sharedByHost")) {
                    StatusDot(live = sources.isNotEmpty())
                    MonoText(
                        text = tr("publishedCountFmt").format(sources.size, 1),
                        maxLines = 1,
                    )
                }

                sources.forEach { source ->
                    val selected = source.id == pickedId
                    SourceRow(
                        // Host cắt tên ở 64 byte và có cửa sổ không có tiêu đề.
                        name = source.name.ifBlank { tr("unnamedSourceFmt").format(source.id) },
                        detail = "${source.width}×${source.height}",
                        selected = selected,
                        state = if (selected) tr("streaming") else tr("idle"),
                        tone = if (selected) PillTone.LIVE else PillTone.NEUTRAL,
                        onSelect = { pickedId = source.id },
                    )
                }

                MonoText(text = tr("pickHint"))
            }
        }

        Box(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .height(Ds.hairline)
                    .background(Ds.colors.borderHairline),
        )
        Column(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .background(Ds.colors.surfaceCard)
                    .padding(horizontal = Ds.screenGutter, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            DsCheckbox(
                checked = !NativeClient.viewOnly,
                onToggle = { onViewOnlyChange(!it) },
                label = tr("allowInput"),
            )
            DsButton(
                text = tr("startViewing"),
                onClick = { sources.firstOrNull { it.id == pickedId }?.let(onPick) },
                variant = DsButtonVariant.PRIMARY,
                size = DsButtonSize.LG,
                fullWidth = true,
            )
        }
    }
}
