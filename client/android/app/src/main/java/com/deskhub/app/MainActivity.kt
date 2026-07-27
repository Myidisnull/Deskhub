// =============================================================================
// MainActivity.kt — màn hình đầu tiên: nhập IP host rồi chọn màn hình muốn xem.
//
// GIAO DIỆN TRẦN (2026-07-27)
//   Material 3 dựng sẵn, không hệ thiết kế riêng, không chữ hướng dẫn, không danh
//   sách máy gần đây. Màn này còn đúng hai thứ: một ô nhập và một nút.
//
// BA BƯỚC, MÔ HÌNH HOÁ BẰNG sealed interface Step
//   Address  — gõ địa chỉ.
//   Querying — đang hỏi host có những nguồn nào (chặn tới 3 giây).
//   Picking  — host trả về nhiều nguồn, cho chọn.
//   Dùng sealed interface thay cho vài biến boolean rời rạc: trình dịch bắt buộc
//   `when` phải phủ hết mọi nhánh, nên thêm bước mới sau này không thể quên chỗ nào.
//
// ĐƯỜNG TẮT: BỎ QUA BƯỚC CHỌN
//   Host im lặng (bản trước GĐ6 không biết LIST_SOURCES) hoặc chỉ chia sẻ một màn
//   hình → vào thẳng. Lỗi thật, nếu có, sẽ do tầng dưới báo lên ở StreamActivity.
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

        // Vẫn cho chạy thẳng từ adb để test nhanh (bỏ qua bước chọn nguồn):
        //   am start -n com.deskhub.app/.MainActivity --es addr 10.0.2.2
        // CHỈ ở bản debug: activity này exported (launcher), nên trên bản phát hành
        // bất kỳ app nào cũng có thể ném extra "addr" vào và lặng lẽ kích hoạt một
        // kết nối tới địa chỉ do nó chọn.
        val debuggable = (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0
        if (debuggable) {
            intent?.getStringExtra("addr")?.let { addr ->
                intent.removeExtra("addr") // chỉ dùng một lần, quay lại không tự nhảy nữa
                openStream(addr, 0)
            }
        }

        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                Surface(modifier = Modifier.fillMaxSize()) {
                    Column(modifier = Modifier.safeDrawingPadding()) {
                        MainScreen(
                            // Địa chỉ lần trước điền sẵn vào ô — không có giao diện
                            // nào cho việc này, chỉ là giá trị khởi tạo.
                            initialAddress = prefs.getString("addr", "").orEmpty(),
                            onRemember = { addr -> prefs.edit().putString("addr", addr).apply() },
                            onOpenStream = ::openStream,
                        )
                    }
                }
            }
        }
    }

    // Danh sách nguồn đi kèm sang StreamActivity để màn xem tự đổi màn hình được mà
    // không phải quay ra hỏi lại host (mất 3 giây). Truyền dạng bốn mảng song song —
    // NativeClient.Source không Parcelable, và bọc nó lại chỉ để đi qua một Intent thì
    // đắt hơn là chép bốn trường.
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
                .putExtra("srcNames", sources.map { it.name }.toTypedArray()),
        )
    }
}

/** Ba bước của màn hình: gõ địa chỉ -> hỏi host có nguồn nào -> chọn nguồn. */
private sealed interface Step {
    data object Address : Step

    // Mang `seq` để phân biệt CÁC LƯỢT hỏi với nhau: lời gọi JNI chặn 3 giây và
    // không hủy được, nên Back rồi Connect tới máy khác là có HAI coroutine cùng
    // bay. Nếu chỗ nhận kết quả chỉ hỏi "đang ở bước Querying à?" thì cả hai đều
    // lọt — lượt CŨ mở stream tới máy cũ đè lên lượt mới. So bằng đúng thực thể
    // Querying của mình thì chỉ lượt mới nhất được đi tiếp.
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

    // Đang hỏi hoặc đang chọn: Back quay về ô địa chỉ thay vì thoát app. Coroutine hỏi
    // nguồn vẫn chạy nốt 3 giây của nó (lời gọi JNI đang chặn, không hủy giữa chừng
    // được), nhưng kết quả bị bỏ qua nhờ phép kiểm tra step ở chỗ nhận kết quả.
    BackHandler(enabled = step != Step.Address) { step = Step.Address }

    val connect: (String) -> Unit = { addr ->
        onRemember(addr)
        val mine = Step.Querying(++querySeq)
        step = mine
        scope.launch {
            // listSources là suspend fun, tự chuyển sang Dispatchers.IO — main
            // thread không bị chặn suốt 3 giây (nếu chặn, Android dựng hộp ANR).
            val sources = NativeClient.listSources(addr)
            // Chỉ nhận kết quả nếu ĐÚNG lượt hỏi này còn là lượt hiện hành — Back
            // rồi Connect máy khác đã thay `step` bằng một Querying seq mới, và lượt
            // cũ về tới đây phải bị bỏ, kẻo nó mở stream tới máy cũ (xem Step.Querying).
            if (step == mine) {
                // Rỗng = host im lặng hoặc host đời cũ; một nguồn = không có gì để
                // chọn. Cả hai vào thẳng, để tầng dưới báo lỗi thật nếu có.
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
                    step = Step.Address // quay lại từ StreamActivity là thấy ô địa chỉ
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

/**
 * Danh sách màn hình host đang chia sẻ. Chọn kiểu radio — nativeStart nhận MỘT
 * sourceId, chọn cái mới là bỏ cái cũ.
 */
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
                        // Host cắt tên ở 64 byte; tên rỗng thì hiện "Source N".
                        Text(
                            text = source.name.ifBlank { "Source %d".format(source.id) },
                            style = MaterialTheme.typography.bodyLarge,
                        )
                        Text(
                            text = "${source.width}×${source.height}",
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
