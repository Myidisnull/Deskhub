// =============================================================================
// StreamActivity.kt — màn hình XEM + ĐIỀU KHIỂN.
//
// GIAO DIỆN TRẦN, DÙNG THẲNG MATERIAL 3 (2026-07-27)
//   Chrome của màn này từng dựng trên hệ thiết kế riêng (ui/Tokens.kt +
//   ui/Components.kt): HUD kính bo pill, chip trạng thái, chấm sống, biểu đồ RTT.
//   Cả bộ đó đã xoá theo yêu cầu "trông cơ bản thôi, không cần màu mè" — giờ chỉ còn
//   `Text` và `Button` mặc định của Material 3 trên nền đen.
//
// BỐ CỤC: VIDEO TRÀN MÀN HÌNH + BẢNG ĐIỀU KHIỂN THU/MỞ (2026-07-29)
//   Trước đây là ba hàng xếp dọc — [thanh trên: địa chỉ + số liệu] / [video] /
//   [thanh dưới: phím tắt + nút]. Không có gì che video, nhưng khung hình mất VĨNH
//   VIỄN đúng chiều cao hai thanh, kể cả lúc chỉ ngồi xem.
//
//   Giờ video chiếm TRỌN màn hình và toàn bộ chrome dồn vào một lớp phủ góc dưới-phải:
//     thu  → chỉ còn một nút tròn 48dp mờ. Gần như cả màn là khung hình.
//     mở   → bảng phủ đáy: địa chỉ + dòng số liệu, hàng phím tắt, Keyboard/Display/End,
//            và nút ✕ để thu lại.
//   Đánh đổi đảo chiều so với bản trước: lúc MỞ, bảng đè lên phần đáy khung hình —
//   nhưng phần bị đè chỉ tồn tại đúng lúc người dùng chủ động mở nó ra.
//
//   Trackpad vẫn phủ trọn màn hình, gồm cả phần dưới bảng — nên lớp điều khiển NUỐT
//   trọn sự kiện chạm rơi vào nó (xem consumeTouches), kẻo mỗi lần bấm nút con trỏ
//   lại nhảy một phát.
//
//   Phần CHỨC NĂNG giữ nguyên từng dòng: SurfaceView, tỉ lệ khung/letterbox, trackpad
//   ảo, view hứng phím IME, hàng phím tắt. Chúng không phải trang trí.
//
// PHÓNG TO KHUNG HÌNH (2026-07-30)
//   Host 4K co vào màn 6 inch thì chữ nhỏ như hạt vừng. Chụm hai ngón để phóng 1×..5×
//   kiểu xem ảnh: điểm desktop nằm giữa hai ngón DÍNH luôn ở đó, cả khung nở ra quanh
//   nó. Rê hai ngón để dời sang khu vực khác.
//
//   Khung hình KHÔNG bao giờ TỰ dịch. Bản đầu làm ngược (con trỏ rê tới mép thì khung
//   chạy theo) và dùng thì hỏng: vừa chụm phóng vào một góc, chạm ngón một cái là
//   khung giật đi chỗ khác vì con trỏ đang đứng đâu đó ngoài vùng nhìn. Con trỏ giờ bị
//   kẹp trong PHẦN ĐANG NHÌN THẤY của khung (giao khung video với màn hình), nên nó
//   không bao giờ lẻn ra ngoài rìa để rồi mất dấu.
//
//   MỘT NGÓN CÓ HAI VAI. Phóng lên rồi thì việc hay làm nhất là VUỐT MỘT NGÓN để ngắm
//   chỗ khác — nhưng một ngón đang là trackpad, mà từ chính cú vuốt thì không đoán
//   được ý người dùng. Nên có công tắc "Pan/Pointer" (viên thuốc ở lớp điều khiển, chỉ
//   hiện khi đang phóng): Pan thì một ngón dời khung, Pointer thì một ngón di chuột.
//   Vào trạng thái phóng là tự sang Pan, về 1× là tự về Pointer.
//
// PHÓNG BẰNG BIẾN ĐỔI VIEW, KHÔNG PHẢI BẰNG LAYOUT
//   SurfaceView được layout ĐÚNG MỘT LẦN theo khung 1× (chỉ letterbox); zoom/kéo áp
//   bằng scaleX/scaleY + translationX/Y của chính nó.
//
//   Bản trước đổi thẳng `size` của SurfaceView theo mức phóng và nó SAI ở chỗ tốn kém:
//   đổi size là requestLayout, kéo theo một lượt đo-và-bố-trí cả cây view RỒI cấp phát
//   lại surface — mỗi frame của cử chỉ chụm. Ảnh trễ hẳn sau ngón tay, đúng cái cảm
//   giác "màn hình chạy theo". (Rê hai ngón thì không bị: nó chỉ đổi offset, mà dời chỗ
//   là đường rẻ — cũng chính là đường SurfaceView dùng để bám theo lúc cuộn.)
//   Đổi scale/translation thì không đo lại gì cả: RenderThread đẩy khung mới xuống
//   SurfaceControl, hardware composer lo phần còn lại. Hoạt động từ API 24, mà minSdk
//   của dự án đúng bằng 24.
//
//   Vẫn KHÔNG dùng graphicsLayer của Compose: biến đổi đó nằm trên RenderNode của
//   Compose, trong khi surface được đặt theo vị trí THẬT của View trong cây view.
//
//   Lớp trackpad KHÔNG bị phóng — nó luôn phủ trọn vùng nhìn — và con trỏ lưu toạ độ
//   CHUẨN HOÁ 0..1 theo khung video, nên phóng/kéo không hề đụng tới vị trí trên
//   desktop host. Đổi lại: phóng càng to, một milimet ngón tay càng ít pixel host.
//
// NHIỀU MÀN HÌNH: ĐỔI NGAY TẠI ĐÂY
//   Host chia sẻ TẤT CẢ màn hình, nên máy nhiều monitor gửi về nhiều nguồn. Nút
//   "Display" ở thanh dưới (chỉ hiện khi có >1 nguồn) mở danh sách và đổi tại chỗ —
//   xem StreamActivity.switchSource về lý do đổi = đóng phiên + mở phiên mới.
//
// ĐIỀU QUAN TRỌNG NHẤT (không đổi): KHUNG HÌNH KHÔNG ĐI QUA COMPOSE
//   Compose chỉ lo phần chrome. Pixel của video đi thẳng từ bộ giải mã phần cứng ra
//   màn hình qua hardware composer — SurfaceView chứ không phải TextureView
//   (TextureView đi qua view hierarchy, thêm một lần copy GPU và một frame trễ).
//
// VÒNG ĐỜI SURFACE LÀ PHẦN DỄ SAI NHẤT
//   surfaceCreated  → giao Surface xuống C++.
//   surfaceDestroyed → thu hồi, và lời gọi này CHẶN tới khi bộ giải mã buông ra.
//   Thứ tự đó bắt buộc: hàm surfaceDestroyed trả về là hệ điều hành hủy Surface
//   thật, codec còn vẽ vào đó là lỗi dùng-sau-giải-phóng.
//
// VÌ SAO HỎI TRẠNG THÁI THEO NHỊP THAY VÌ ĐỂ C++ GỌI NGƯỢC LÊN
//   Gọi ngược từ C++ vào JVM đòi gắn thread vào JVM và giữ global ref, mỗi frame.
//   Hỏi 500 ms một lần rẻ hơn nhiều, mà dòng số liệu chỉ đổi mỗi giây một lần.
//
// LIÊN QUAN: MainActivity.kt (nơi mở màn hình này), NativeClient.kt, ClientLoop.h
// =============================================================================
package com.deskhub.app

import android.content.Context
import android.os.Build
import android.os.Bundle
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import android.view.inputmethod.InputMethodManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.calculateCentroid
import androidx.compose.foundation.gestures.calculatePan
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectDragGesturesAfterLongPress
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.PointerInputScope
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import kotlinx.coroutines.delay
import kotlin.math.roundToInt

class StreamActivity : ComponentActivity() {
    // Thế hệ phiên do nativeStart trả về (0 = không mở được). Tầng native là
    // singleton mà vòng đời hai StreamActivity có thể chồng lấn nhau (kết thúc rồi
    // kết nối lại ngay) — giữ thế hệ để onDestroy trễ của instance này không giết
    // nhầm phiên mà instance mới vừa mở.
    //
    // Là state của Compose vì đổi màn hình giữa chừng sẽ thay nó bằng một thế hệ mới,
    // và giao diện phải vẽ lại theo (xoá số liệu cũ, poll lại từ đầu).
    private var session by mutableStateOf(0L)

    // Cỡ màn hình máy (pixel), đo MỘT LẦN ở onCreate và dùng lại cho mọi lần
    // nativeStart của activity này — kể cả switchSource. Không phải Compose state:
    // nó không đổi trong vòng đời activity (xoay máy là config change → activity mới,
    // và `maximumWindowMetrics` vốn không phụ thuộc hướng cầm).
    private var screenPx: Pair<Int, Int> = 0 to 0

    // Nguồn đang xem + danh sách host đang chia sẻ (do MainActivity truyền sang, xem
    // openStream). Rỗng hoặc một phần tử = không có gì để đổi, nút Display ẩn.
    private var currentSourceId by mutableIntStateOf(0)
    private var sources: List<NativeClient.Source> = emptyList()
    private var address = ""

    // Giữ ở Activity chứ không tạo trong composable: callback này phải sống đúng
    // bằng vòng đời SurfaceView, không được dựng lại theo mỗi lần recomposition.
    private val holderCallback =
        object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                NativeClient.nativeSetSurface(holder.surface)
            }

            override fun surfaceChanged(
                h: SurfaceHolder,
                f: Int,
                w: Int,
                ht: Int,
            ) {}

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                // Chặn tới khi bộ giải mã buông surface — xem chú thích ở NativeClient.
                // Bản có so danh tính: surfaceDestroyed trễ của activity cũ không
                // được giật cửa sổ mà phiên mới đang vẽ vào.
                NativeClient.nativeReleaseSurface(holder.surface)
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // Người xem không chạm màn hình trong lúc xem, nên nếu không giữ cờ này thì
        // máy tự tắt màn hình giữa chừng — kéo theo Surface bị hủy và phiên đứt.
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        address = intent.getStringExtra("addr").orEmpty()
        // Không có "source" (vd. chạy thẳng từ adb) -> nguồn 0, như trước.
        currentSourceId = intent.getIntExtra("source", 0)
        sources = readSources(intent)
        screenPx = NativeClient.screenSizePx(this)
        session = NativeClient.nativeStart(address, currentSourceId, screenPx.first, screenPx.second)

        setContent {
            MaterialTheme(colorScheme = darkColorScheme()) {
                StreamScreen(
                    address = address,
                    sessionKey = session,
                    sources = sources,
                    currentSourceId = currentSourceId,
                    holderCallback = holderCallback,
                    onSwitchSource = ::switchSource,
                    onDismiss = { finish() },
                )
            }
        }
    }

    // Bốn mảng song song do MainActivity gói lại — xem MainActivity.openStream.
    private fun readSources(intent: android.content.Intent): List<NativeClient.Source> {
        val ids = intent.getIntArrayExtra("srcIds") ?: return emptyList()
        val w = intent.getIntArrayExtra("srcW") ?: return emptyList()
        val h = intent.getIntArrayExtra("srcH") ?: return emptyList()
        val names = intent.getStringArrayExtra("srcNames") ?: return emptyList()
        if (w.size != ids.size || h.size != ids.size || names.size != ids.size) return emptyList()
        return ids.indices.map { NativeClient.Source(ids[it], w[it], h[it], names[it]) }
    }

    /**
     * Đổi sang màn hình khác của CÙNG host, không rời màn xem.
     *
     * Giao thức không có lệnh "đổi nguồn": mỗi cặp (client, nguồn) là một PHIÊN riêng,
     * nên đổi = đóng phiên cũ rồi mở phiên mới với sourceId khác. Đó đúng là những gì
     * nativeStop + nativeStart làm sẵn.
     *
     * SurfaceView KHÔNG bị dựng lại: JniBridge giữ `g_window` độc lập với vòng đời
     * phiên và nativeStart tự gắn lại nó cho phiên mới (xem nativeStart trong
     * JniBridge.cpp). Nhờ vậy đổi màn hình chỉ tốn một lần bắt tay, không nháy đen do
     * huỷ/tạo lại Surface.
     */
    private fun switchSource(sourceId: Int) {
        if (sourceId == currentSourceId) return
        if (session != 0L) NativeClient.nativeStop(session)
        currentSourceId = sourceId
        session = NativeClient.nativeStart(address, sourceId, screenPx.first, screenPx.second)
    }

    override fun onStop() {
        super.onStop()
        // Xuống nền là KẾT THÚC phiên. Không có nhánh này thì surfaceDestroyed chỉ
        // thu hồi cửa sổ (frame bị vứt) còn thread Net vẫn nhận trọn bitrate và host
        // vẫn encode/gửi vô hạn — nhiều Mbps và pin đốt cho một app vô hình. Giao
        // thức chưa có "pause" nên dừng hẳn là lựa chọn đúng; bản iOS cũng chết phiên
        // khi xuống nền (host timeout 5s). Xoay màn hình (config change) thì không.
        if (!isFinishing && !isChangingConfigurations) finish()
    }

    override fun onDestroy() {
        // Chỉ dừng đúng phiên MÌNH tạo — xem chú thích ở `session`.
        if (session != 0L) NativeClient.nativeStop(session)
        super.onDestroy()
    }
}

/**
 * Một phím tắt gửi thẳng sang host — bàn phím ảo không có những phím này.
 * `modVk` != 0 -> tổ hợp (giữ phím bổ trợ rồi gõ phím chính): Ctrl+C, Ctrl+V...
 */
private data class Hotkey(
    val label: String,
    val vk: Int,
    val scan: Int,
    val modVk: Int = 0,
    val modScan: Int = 0,
)

// Thêm phím mới = thêm một dòng: mã phím ảo Windows + scancode US (bit8 = cờ E0
// cho phím mở rộng như mũi tên/Del — xem Wire.h). Không đưa Alt+Tab/phím Win vào:
// chúng chuyển focus khỏi cửa sổ đang chia sẻ, host sẽ ngừng nhận input.
private val kHotkeys =
    listOf(
        Hotkey("Esc", 0x1B, 0x01),
        Hotkey("Tab", 0x09, 0x0F),
        Hotkey("Enter", 0x0D, 0x1C),
        Hotkey("↑", 0x26, 0x148),
        Hotkey("↓", 0x28, 0x150),
        Hotkey("←", 0x25, 0x14B),
        Hotkey("→", 0x27, 0x14D),
        Hotkey("Del", 0x2E, 0x153),
        Hotkey("Ctrl+C", 0x43, 0x2E, modVk = 0x11, modScan = 0x1D),
        Hotkey("Ctrl+V", 0x56, 0x2F, modVk = 0x11, modScan = 0x1D),
    )

@Composable
private fun StreamScreen(
    address: String,
    sessionKey: Long,
    sources: List<NativeClient.Source>,
    currentSourceId: Int,
    holderCallback: SurfaceHolder.Callback,
    onSwitchSource: (Int) -> Unit,
    onDismiss: () -> Unit,
) {
    val started = sessionKey != 0L
    var phase by remember { mutableIntStateOf(NativeClient.PHASE_IDLE) }
    var statusLine by remember { mutableStateOf("") }
    var endReason by remember { mutableStateOf("") }
    var videoW by remember { mutableIntStateOf(0) }
    var videoH by remember { mutableIntStateOf(0) }

    // Hỏi trạng thái từ tầng C++ 500ms/lần. Rẻ hơn nhiều so với để C++ gọi ngược lên
    // JVM mỗi frame, và dòng số liệu chỉ đổi mỗi giây nên không cần nhanh hơn.
    //
    // Khoá theo sessionKey chứ không theo `started`: đổi màn hình sinh một thế hệ MỚI
    // nhưng `started` vẫn true, nên nếu khoá theo nó thì vòng poll cũ chạy tiếp và số
    // liệu/lý-do-kết-thúc của phiên vừa đóng còn dính lại trên màn hình.
    LaunchedEffect(sessionKey) {
        phase = NativeClient.PHASE_IDLE
        statusLine = ""
        endReason = ""
        videoW = 0
        videoH = 0
        if (!started) return@LaunchedEffect
        while (true) {
            phase = NativeClient.nativePhase()
            statusLine = NativeClient.nativeStatusLine()
            videoW = NativeClient.nativeVideoWidth()
            videoH = NativeClient.nativeVideoHeight()
            // Hết phiên thì thoát hẳn coroutine: lý do kết thúc không đổi nữa, hỏi
            // tiếp chỉ tốn pin. LaunchedEffect tự hủy coroutine khi rời màn hình.
            if (phase == NativeClient.PHASE_ENDED) {
                endReason = NativeClient.nativeEndReason()
                return@LaunchedEffect
            }
            delay(500)
        }
    }

    val streaming = phase == NativeClient.PHASE_STREAMING

    // Bàn phím ảo: bật/tắt bằng nút Keyboard ở thanh dưới. KeyInputView vô hình giữ
    // focus để IME gửi phím; xem giải thích cơ chế TYPE_NULL trong KeyInputView.kt.
    var keyboardOn by remember { mutableStateOf(false) }
    var keyView by remember { mutableStateOf<KeyInputView?>(null) }
    LaunchedEffect(keyboardOn) {
        val v = keyView ?: return@LaunchedEffect
        val imm = v.context.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
        if (!keyboardOn) {
            imm.hideSoftInputFromWindow(v.windowToken, 0)
            return@LaunchedEffect
        }
        v.requestFocus()
        imm.showSoftInput(v, 0)

        // Người dùng có thể hạ bàn phím bằng nút ẩn/Back của chính IME, không qua nút
        // Keyboard — canh ime inset để trạng thái nút không kẹt ở "đang bật". Phải chờ
        // THẤY bàn phím hiện rồi mới canh lúc ẩn, kẻo tắt nhầm khi IME còn đang trượt
        // lên. rootWindowInsets chỉ báo được IME từ API 30; máy cũ hơn giữ hành vi cũ
        // (nút không tự tắt, bấm hai lần để mở lại). Coroutine tự hủy khi keyboardOn
        // đổi hoặc rời màn hình — không rò vòng lặp.
        if (Build.VERSION.SDK_INT < 30) return@LaunchedEffect
        var seen = false
        while (true) {
            val visible =
                v.rootWindowInsets?.isVisible(
                    android.view.WindowInsets.Type
                        .ime(),
                ) == true
            if (visible) {
                seen = true
            } else if (seen) {
                keyboardOn = false
                return@LaunchedEffect
            }
            delay(200)
        }
    }

    // Bảng điều khiển: mặc định THU. Vào phiên là thấy ngay khung hình trọn vẹn,
    // muốn nút thì mở ra.
    var controlsOpen by remember { mutableStateOf(false) }

    // Khung nhìn: 1× = vừa khít màn hình. `pan` tính bằng pixel màn hình và luôn được
    // kẹp trong videoFrame để không bao giờ hở nền đen ở rìa. `viewport` là vùng
    // video thật (đã trừ notch hai bên), đo bằng onSizeChanged bên dưới.
    var zoom by remember { mutableFloatStateOf(1f) }
    var pan by remember { mutableStateOf(Offset.Zero) }
    var viewport by remember { mutableStateOf(IntSize.Zero) }
    val aspect = if (videoW > 0 && videoH > 0) videoW.toFloat() / videoH else null
    val zoomed = zoom > 1.01f

    // MỘT NGÓN CÓ HAI VAI, VÀ PHẢI CÓ CÔNG TẮC.
    //   Phóng lên rồi thì việc hay làm nhất là VUỐT MỘT NGÓN để ngắm chỗ khác — nhưng
    //   một ngón đang là trackpad. Không đoán được ý người dùng từ chính cú vuốt đó,
    //   nên có công tắc: bật thì một ngón dời khung, tắt thì một ngón di chuột.
    //   Vào trạng thái phóng là tự bật (phóng lên vốn để NGẮM); về 1× là tự tắt vì
    //   không còn gì để dời. Giữa chừng người dùng bấm viên thuốc Pan/Pointer để đổi.
    var panMode by remember { mutableStateOf(false) }
    LaunchedEffect(zoomed) { panMode = zoomed }

    // Đổi màn hình = đổi hẳn kích thước desktop; giữ mức phóng của nguồn cũ thì
    // người dùng rơi vào một góc ngẫu nhiên của nguồn mới.
    LaunchedEffect(sessionKey) {
        zoom = 1f
        pan = Offset.Zero
    }

    /**
     * Quy cử chỉ hai ngón về (zoom, pan) tuyệt đối — kiểu xem ảnh.
     *
     * Phóng NEO Ở TÂM HAI NGÓN: điểm desktop đang nằm dưới `centroid` phải ở nguyên
     * chỗ đó, cả khung nở ra quanh nó. Với ánh xạ
     * `màn hình = tâm + pan + (nội dung - tâm) * zoom`, giữ nguyên điểm dưới tay khi
     * zoom nhân thêm `ratio` cho ra:
     * `pan' = (tâm hai ngón - tâm màn hình) * (1 - ratio) + pan * ratio`.
     * `panDelta` (tâm hai ngón dời được bao nhiêu) cộng thêm vào sau, để lúc chụm mà
     * tay trượt đi thì ảnh vẫn dính lấy tay.
     */
    fun applyTransform(
        factor: Float,
        centroid: Offset,
        panDelta: Offset,
    ) {
        if (viewport.width <= 0 || viewport.height <= 0) return
        val newZoom = (zoom * factor).coerceIn(1f, MAX_ZOOM)
        // Hệ số THẬT sự áp dụng — khác `factor` khi zoom vừa chạm trần/sàn.
        val ratio = newZoom / zoom
        val anchored =
            if (ratio == 1f) {
                pan
            } else {
                Offset(
                    (centroid.x - viewport.width / 2f) * (1f - ratio) + pan.x * ratio,
                    (centroid.y - viewport.height / 2f) * (1f - ratio) + pan.y * ratio,
                )
            }
        val next = anchored + panDelta
        zoom = newZoom
        // Lấy lại pan ĐÃ KẸP từ khung mà videoFrame dựng ra, thay vì kẹp lần nữa ở
        // đây: chỉ một chỗ giữ luật "không được hở rìa", không sợ hai chỗ lệch nhau.
        val rect = videoFrame(viewport, aspect, newZoom, next)
        pan = Offset(rect.center.x - viewport.width / 2f, rect.center.y - viewport.height / 2f)
    }

    // pointerInput(Unit) NHỚ block của lần dựng đầu tiên, mà block đó ôm luôn bản
    // applyTransform lúc ấy — bản còn thấy `aspect` = null vì kích thước video chưa
    // về. Gói qua rememberUpdatedState thì mỗi lần gọi mới đọc bản mới nhất.
    val onTransform by rememberUpdatedState(
        newValue = { factor: Float, centroid: Offset, delta: Offset ->
            applyTransform(factor, centroid, delta)
        },
    )

    // VIDEO TRÀN VIỀN, CHROME NẰM TRONG MỘT LỚP PHỦ (2026-07-29)
    //   Không safeDrawingPadding ở ngoài cùng nữa: video phải chạm tới mép màn hình.
    //   Riêng lớp điều khiển mới né thanh hệ thống / tai thỏ / bàn phím ảo — chỗ đó
    //   mới cần ngón tay bấm trúng.
    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .background(Color.Black),
    ) {
        // --- Video phủ trọn màn hình (+ trackpad phủ đúng vùng này) ---
        //
        // Trừ đúng phần notch ở HAI BÊN: nằm ngang thì notch nằm ở cạnh trái/phải và
        // nó che mất pixel THẬT — rìa desktop biến mất, thấy rõ nhất với host siêu
        // rộng (3440×1440 phủ hết bề ngang). Trên/dưới không trừ: ở đó chỉ có thanh
        // điều hướng cử chỉ vẽ ĐÈ lên, không che mất gì.
        //
        // Cử chỉ hai ngón bắt Ở ĐÂY, không phải trong trackpad: bắt ở view CHA rồi
        // nuốt ngay từ pass Initial (cha nhận trước con) là cách chắc chắn nhất để
        // trackpad không thấy cú chạm — nó tự do consume ở pass Main mà không tranh
        // chấp với ai. Không canh alignment: khung video tự đặt mình bằng offset.
        Box(
            modifier =
                Modifier
                    .fillMaxSize()
                    .windowInsetsPadding(
                        WindowInsets.displayCutout.only(WindowInsetsSides.Horizontal),
                    ).onSizeChanged { viewport = it }
                    // Lambda trung chuyển chứ không truyền thẳng `onTransform`: block
                    // của pointerInput chỉ chạy MỘT lần, đọc thẳng là chộp luôn bản
                    // đầu tiên và rememberUpdatedState thành vô nghĩa.
                    .pointerInput(Unit) {
                        detectZoomPan { factor, centroid, delta ->
                            onTransform(factor, centroid, delta)
                        }
                    },
        ) {
            if (started) {
                // `base` = khung ở mức 1× (chỉ letterbox), `rect` = khung THẬT đang hiển
                // thị (đã phóng/kéo). SurfaceView được LAYOUT theo `base` và chỉ layout
                // lại khi xoay máy / đổi nguồn; phần zoom-kéo áp bằng scale + translation
                // của chính View. Xem "PHÓNG BẰNG BIẾN ĐỔI VIEW" ở đầu file.
                val base = videoFrame(viewport, aspect, 1f, Offset.Zero)
                val rect = videoFrame(viewport, aspect, zoom, pan)
                if (!base.isEmpty) {
                    val density = LocalDensity.current
                    Box(
                        modifier =
                            Modifier
                                .offset {
                                    IntOffset(base.left.roundToInt(), base.top.roundToInt())
                                }.size(
                                    with(density) { base.width.toDp() },
                                    with(density) { base.height.toDp() },
                                ),
                    ) {
                        AndroidView(
                            factory = { ctx ->
                                SurfaceView(ctx).apply { holder.addCallback(holderCallback) }
                            },
                            modifier = Modifier.fillMaxSize(),
                            update = { view ->
                                // Pivot ở góc trên-trái: scale không tự dời gốc view, nên
                                // translation chỉ việc đưa gốc đó tới rect.left/top là
                                // xong. Theo định nghĩa base * zoom == kích thước rect,
                                // nên hai khung trùng khít nhau.
                                view.pivotX = 0f
                                view.pivotY = 0f
                                view.scaleX = zoom
                                view.scaleY = zoom
                                view.translationX = rect.left - base.left
                                view.translationY = rect.top - base.top
                            },
                        )
                    }
                }

                // Trackpad phủ trọn màn hình — gồm cả vùng đen letterbox, và KHÔNG bị
                // phóng theo video. Phần nằm dưới lớp điều khiển không nhận chạm: lớp
                // đó nuốt trước (consumeTouches).
                if (streaming) {
                    TrackpadOverlay(
                        videoRect = rect,
                        panMode = panMode,
                        onPanRequest = { delta -> applyTransform(1f, Offset.Zero, delta) },
                        modifier = Modifier.fillMaxSize(),
                    )
                }

                // View hứng phím: 1dp, vô hình, chỉ tồn tại để giữ focus cho IME.
                AndroidView(
                    factory = { ctx ->
                        KeyInputView(ctx)
                            .apply { onChar = { cp -> NativeClient.charTap(cp) } }
                            .also { keyView = it }
                    },
                    modifier = Modifier.size(1.dp),
                )
            }

            if (!started || phase == NativeClient.PHASE_ENDED) {
                EndedOverlay(
                    reason = if (!started) "Could not connect to $address" else endReason,
                    onBack = onDismiss,
                )
            } else if (!streaming) {
                ConnectingOverlay(address = address)
            }
        }

        // --- Lớp điều khiển: nút tròn <-> bảng, góc dưới-phải ---
        //
        // Viên thuốc zoom nằm TRONG lớp này chứ không phải một lớp phủ riêng: nó được
        // consumeTouches che chung, không phải dựng thêm một vùng mù thứ hai.
        Column(
            modifier =
                Modifier
                    .align(Alignment.BottomEnd)
                    // safeDrawing gồm cả IME, nên bảng tự trượt lên trên bàn phím ảo
                    // trong khi khung hình phía sau đứng yên.
                    .safeDrawingPadding()
                    .consumeTouches()
                    .padding(12.dp),
            horizontalAlignment = Alignment.End,
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            if (zoomed) {
                // Công tắc cho cử chỉ MỘT ngón, rồi tới mức phóng — cả hai chỉ hiện
                // khi đang phóng, vì lúc 1× chẳng cái nào nói được gì.
                Pill(text = if (panMode) "Pan" else "Pointer", onClick = { panMode = !panMode })
                Pill(
                    text = "%.1f×".format(zoom),
                    onClick = {
                        zoom = 1f
                        pan = Offset.Zero
                    },
                )
            }
            if (controlsOpen) {
                ControlPanel(
                    address = address,
                    videoW = videoW,
                    videoH = videoH,
                    statusLine = statusLine,
                    streaming = streaming,
                    keyboardOn = keyboardOn,
                    sources = sources,
                    currentSourceId = currentSourceId,
                    onToggleKeyboard = { keyboardOn = !keyboardOn },
                    onSwitchSource = onSwitchSource,
                    onEnd = onDismiss,
                    onCollapse = { controlsOpen = false },
                )
            } else {
                ExpandButton(onClick = { controlsOpen = true })
            }
        }
    }
}

/**
 * Nuốt trọn sự kiện chạm rơi vào vùng này.
 *
 * Trackpad là view ANH EM nằm dưới, phủ trọn màn hình. Compose dừng hit-test ở con
 * trên cùng có pointerInput, nên chỉ cần lớp điều khiển CÓ một pointerInput là chạm
 * không lọt xuống nữa — không có nó thì khoảng trống giữa các nút vẫn rơi thẳng
 * xuống trackpad và mỗi lần bấm hụt là con trỏ nhảy.
 *
 * Consume ở pass Main (mặc định): nút con nhận sự kiện TRƯỚC cha, nên chúng vẫn bấm
 * bình thường.
 */
private fun Modifier.consumeTouches(): Modifier =
    pointerInput(Unit) {
        awaitPointerEventScope {
            while (true) {
                awaitPointerEvent().changes.forEach { it.consume() }
            }
        }
    }

/**
 * Bắt cử chỉ HAI ngón (chụm + rê) và nuốt luôn, để lớp trackpad bên dưới không thấy.
 *
 * Chỉ nghe ở pass Initial — pass này đi từ cha xuống con, nên consume ở đây là con
 * (trackpad) coi như chưa hề có cú chạm nào. Nghe ở pass Main thì ngược lại: con đã
 * xử lý xong rồi mới tới lượt cha.
 *
 * Một ngón thì không đụng vào: sự kiện đi thẳng xuống trackpad như cũ. Khi đã vào
 * chế độ hai ngón thì nuốt tới lúc NHẤC HẾT tay ra — nhấc một ngón mà thả cho trackpad
 * nhận nốt ngón còn lại thì con trỏ nhảy một phát ở cuối mỗi lần phóng.
 *
 * Zoom và pan đi CÙNG NHAU, không tách vai: đó là điều kiện để ảnh dính lấy tay như
 * lúc xem ảnh — tay vừa chụm vừa trượt thì khung phải vừa nở vừa đi theo.
 */
private suspend fun PointerInputScope.detectZoomPan(onTransform: (Float, Offset, Offset) -> Unit) {
    awaitEachGesture {
        awaitFirstDown(requireUnconsumed = false, pass = PointerEventPass.Initial)
        var multiTouch = false
        do {
            val event = awaitPointerEvent(PointerEventPass.Initial)
            if (event.changes.count { it.pressed } >= 2) {
                multiTouch = true
                val zoom = event.calculateZoom()
                val pan = event.calculatePan()
                if (zoom != 1f || pan != Offset.Zero) {
                    onTransform(zoom, event.calculateCentroid(useCurrent = true), pan)
                }
            }
            if (multiTouch) event.changes.forEach { it.consume() }
        } while (event.changes.any { it.pressed })
    }
}

/** Hộp thoại đổi màn hình — chỉ mở được khi host chia sẻ từ hai nguồn trở lên. */
@Composable
private fun DisplayPickerDialog(
    sources: List<NativeClient.Source>,
    currentSourceId: Int,
    onPick: (Int) -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Display") },
        text = {
            Column {
                sources.forEach { source ->
                    Row(
                        modifier =
                            Modifier
                                .fillMaxWidth()
                                .clickable {
                                    onPick(source.id)
                                    onDismiss()
                                }.padding(vertical = 8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        RadioButton(
                            selected = source.id == currentSourceId,
                            onClick = {
                                onPick(source.id)
                                onDismiss()
                            },
                        )
                        Column {
                            Text(source.name.ifBlank { "Source %d".format(source.id) })
                            Text(
                                text = "${source.width}×${source.height}",
                                style = MaterialTheme.typography.bodySmall,
                            )
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}

/** Viên thuốc mờ trên khung hình — dùng cho công tắc Pan/Pointer và mức phóng. */
@Composable
private fun Pill(
    text: String,
    onClick: () -> Unit,
) {
    Box(
        modifier =
            Modifier
                .clip(CircleShape)
                .background(Color.Black.copy(alpha = 0.45f))
                .border(1.dp, Color.White.copy(alpha = 0.25f), CircleShape)
                .clickable(onClick = onClick)
                .padding(horizontal = 10.dp, vertical = 6.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(text = text, color = Color.White, style = MaterialTheme.typography.labelMedium)
    }
}

/**
 * Trạng thái THU: một nút tròn mờ ở góc dưới-phải — thứ DUY NHẤT nằm trên khung hình
 * khi người dùng chỉ ngồi xem.
 */
@Composable
private fun ExpandButton(onClick: () -> Unit) {
    Box(
        modifier =
            Modifier
                .size(48.dp)
                .clip(CircleShape)
                .background(Color.Black.copy(alpha = 0.45f))
                .border(1.dp, Color.White.copy(alpha = 0.25f), CircleShape)
                .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(text = "☰", color = Color.White)
    }
}

/**
 * Trạng thái MỞ: mọi thứ từng nằm ở hai thanh — địa chỉ + dòng số liệu, hàng phím tắt
 * cuộn ngang, Keyboard/Display/End — gộp vào một bảng phủ đáy, kèm nút ✕ để thu lại.
 */
@Composable
private fun ControlPanel(
    address: String,
    videoW: Int,
    videoH: Int,
    statusLine: String,
    streaming: Boolean,
    keyboardOn: Boolean,
    sources: List<NativeClient.Source>,
    currentSourceId: Int,
    onToggleKeyboard: () -> Unit,
    onSwitchSource: (Int) -> Unit,
    onEnd: () -> Unit,
    onCollapse: () -> Unit,
) {
    var pickerOpen by remember { mutableStateOf(false) }

    if (pickerOpen) {
        DisplayPickerDialog(
            sources = sources,
            currentSourceId = currentSourceId,
            onPick = onSwitchSource,
            onDismiss = { pickerOpen = false },
        )
    }

    Column(
        modifier =
            Modifier
                .fillMaxWidth()
                // Nền đen mờ chứ không trong suốt: chữ trắng phải đọc được trên MỌI
                // khung hình chạy phía sau.
                //
                // background(color, shape) chứ KHÔNG clip(shape) rồi background(color):
                // clip cắt cụt phần thò ra của con — khi chỗ trống theo chiều dọc chỉ
                // vừa đúng nội dung (máy nằm ngang), hàng nút cuối bị xén mất đáy.
                // Bo góc bằng shape của background thì không đụng tới con.
                .background(Color.Black.copy(alpha = 0.75f), RoundedCornerShape(16.dp))
                .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = if (videoW > 0) "$address — $videoW×$videoH" else address,
                    style = MaterialTheme.typography.bodySmall,
                    color = Color.White,
                    maxLines = 1,
                )
                if (streaming && statusLine.isNotEmpty()) {
                    Text(
                        text = statusLine,
                        style = MaterialTheme.typography.bodySmall,
                        color = Color.White.copy(alpha = 0.8f),
                        maxLines = 1,
                    )
                }
            }
            Box(
                modifier =
                    Modifier
                        .size(32.dp)
                        .clip(CircleShape)
                        .clickable(onClick = onCollapse),
                contentAlignment = Alignment.Center,
            ) {
                Text(text = "✕", color = Color.White)
            }
        }

        // Phím tắt cuộn ngang: hàng này dài hơn bề ngang máy.
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            kHotkeys.forEach { hk ->
                OutlinedButton(
                    onClick = {
                        if (hk.modVk != 0) {
                            NativeClient.keyChord(hk.modVk, hk.modScan, hk.vk, hk.scan)
                        } else {
                            NativeClient.keyTap(hk.vk, hk.scan)
                        }
                    },
                    enabled = streaming,
                    contentPadding = PaddingValues(horizontal = 12.dp, vertical = 4.dp),
                ) { Text(hk.label) }
            }
        }

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            OutlinedButton(onClick = onToggleKeyboard, enabled = streaming) {
                Text(if (keyboardOn) "Hide keyboard" else "Keyboard")
            }
            // Chỉ hiện khi có cái để đổi: host một màn hình thì nút này là một câu hỏi
            // không có câu trả lời.
            if (sources.size > 1) {
                OutlinedButton(onClick = { pickerOpen = true }) { Text("Display") }
            }
            Box(modifier = Modifier.weight(1f))
            Button(onClick = onEnd) { Text("End") }
        }
    }
}

@Composable
private fun ConnectingOverlay(address: String) {
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            CircularProgressIndicator()
            Text(text = "Connecting to $address…", color = Color.White)
        }
    }
}

@Composable
private fun EndedOverlay(
    reason: String,
    onBack: () -> Unit,
) {
    // Chạm vào đâu cũng quay lại được — nhưng vẫn có nút thật cho người không đoán ra.
    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .clickable(onClick = onBack),
        contentAlignment = Alignment.Center,
    ) {
        Column(
            modifier = Modifier.padding(24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                text = "Session ended",
                style = MaterialTheme.typography.titleMedium,
                color = Color.White,
            )
            Text(text = reason, color = Color.White)
            TextButton(onClick = onBack) { Text("Back") }
        }
    }
}

/**
 * Trackpad ảo phủ lên khung video, kiểu bàn di chuột laptop: con trỏ LUÔN hiện,
 * ngón tay rê ở đâu cũng được — con trỏ dịch theo DELTA chứ không nhảy tới điểm
 * chạm (ngón tay không che mất chỗ cần bấm, và bấm được chính xác từng pixel).
 *
 *   Rê ngón       = di con trỏ.
 *   Tap 1 lần     = click trái TẠI CON TRỎ.
 *   Tap 2 lần     = click phải tại con trỏ.
 *   Giữ rồi kéo   = giữ chuột trái và rê (kéo cửa sổ, bôi đen), nhấc tay là nhả.
 *
 * (Cử chỉ hai ngón — phóng/kéo khung hình — không vào tới đây: view cha nuốt trước.
 * Overlay này KHÔNG bao giờ làm khung hình dịch chuyển; xem đầu file.)
 *
 * Overlay phủ CẢ vùng hiển thị (gồm vùng đen letterbox) và KHÔNG bị phóng theo video.
 * Con trỏ lưu toạ độ CHUẨN HOÁ 0..1 theo [videoRect] chứ không phải pixel màn hình:
 * khung có phóng/kéo thế nào thì vị trí trên desktop host vẫn y nguyên, và toạ độ
 * 0..65535 gửi đi chỉ còn là một phép nhân.
 *
 * @param videoRect khung video đang hiển thị, toạ độ overlay (xem [videoFrame]).
 * @param panMode một ngón DỜI KHUNG thay vì di chuột. Lúc bật, MỌI cử chỉ một ngón đều
 *   im — kể cả tap: đang vuốt để ngắm mà lỡ click xuống host thì tệ hơn nhiều so với
 *   việc phải chạm công tắc một cái.
 * @param onPanRequest xin dời khung một đoạn (chỉ gọi khi [panMode]).
 */
@Composable
private fun TrackpadOverlay(
    videoRect: Rect,
    panMode: Boolean,
    onPanRequest: (Offset) -> Unit,
    modifier: Modifier,
) {
    // Vị trí con trỏ, CHUẨN HOÁ 0..1 trong khung video.
    var cursor by remember { mutableStateOf(Offset(0.5f, 0.5f)) }
    var bounds by remember { mutableStateOf(IntSize.Zero) }

    // pointerInput(...) nhớ block của lần dựng đầu; đọc tham số qua State để các cử
    // chỉ luôn thấy khung/callback MỚI NHẤT thay vì bản chụp lúc mới vào phiên.
    // (`panMode` thì ngược lại — nó là KHOÁ của pointerInput, đổi là dựng lại handler.)
    val rect by rememberUpdatedState(videoRect)
    val requestPan by rememberUpdatedState(onPanRequest)

    fun screenPos(): Offset = Offset(rect.left + cursor.x * rect.width, rect.top + cursor.y * rect.height)

    // Con trỏ đã chuẩn hoá sẵn — chỉ việc trải ra thang 0..65535 mà InputInjector bên
    // host mong đợi.
    fun sendMove() {
        NativeClient.mouseMove(
            (cursor.x * 65535f).roundToInt(),
            (cursor.y * 65535f).roundToInt(),
        )
    }

    /**
     * Kẹp vị trí (chuẩn hoá) vào PHẦN KHUNG ĐANG NHÌN THẤY — giao của khung video với
     * màn hình. Lúc 1× khung nằm gọn trong màn hình nên đây đúng là kẹp về 0..1 như cũ;
     * phóng to thì nó chặn con trỏ lẻn ra ngoài rìa màn hình rồi mất dấu.
     */
    fun clampToVisible(pos: Offset): Offset {
        if (rect.width <= 0f || rect.height <= 0f) return pos
        if (bounds.width <= 0 || bounds.height <= 0) return pos
        val screen = Rect(0f, 0f, bounds.width.toFloat(), bounds.height.toFloat())
        val visible = rect.intersect(screen)
        if (visible.width <= 0f || visible.height <= 0f) return pos
        return Offset(
            pos.x.coerceIn(
                (visible.left - rect.left) / rect.width,
                (visible.right - rect.left) / rect.width,
            ),
            pos.y.coerceIn(
                (visible.top - rect.top) / rect.height,
                (visible.bottom - rect.top) / rect.height,
            ),
        )
    }

    // Khung hoặc màn hình đổi (phóng, kéo, xoay máy): vị trí trên desktop của con trỏ
    // KHÔNG đổi, chỉ kẹp lại cho nằm trong phần đang nhìn thấy. Không gửi gì cả — mọi
    // cú click đều tự gửi lại vị trí ngay trước khi bấm.
    LaunchedEffect(videoRect, bounds) { cursor = clampToVisible(cursor) }

    fun moveBy(delta: Offset) {
        if (rect.width <= 0f || rect.height <= 0f) return
        cursor =
            clampToVisible(
                Offset(cursor.x + delta.x / rect.width, cursor.y + delta.y / rect.height),
            )
        sendMove()
    }

    // Host cũng có người dùng thật di chuột được — gửi lại vị trí con trỏ ngay
    // trước mỗi cú click để chắc chắn click rơi đúng chỗ con trỏ đang hiển thị.
    fun clickAt(button: Int) {
        sendMove()
        NativeClient.mouseButton(button, true)
        NativeClient.mouseButton(button, false)
    }

    Box(
        modifier =
            modifier
                .onSizeChanged { sz -> bounds = sz }
                // Khoá theo panMode: đổi công tắc là Compose dựng lại đúng bộ handler
                // của vai mới, không phải kiểm tra cờ trong từng callback.
                .pointerInput(panMode) {
                    if (panMode) return@pointerInput
                    // Có onDoubleTap nên onTap phải chờ hết cửa sổ double-tap
                    // (~300ms) mới nổ — giá phải trả để phân biệt được hai cử chỉ.
                    detectTapGestures(
                        onTap = { clickAt(NativeClient.MOUSE_LEFT) },
                        onDoubleTap = { clickAt(NativeClient.MOUSE_RIGHT) },
                    )
                }.pointerInput(panMode) {
                    // Rê một ngón: dời khung hay di con trỏ, tuỳ vai. Cùng một delta,
                    // khác chỗ đổ.
                    detectDragGestures(
                        onDrag = { change, delta ->
                            change.consume()
                            if (panMode) requestPan(delta) else moveBy(delta)
                        },
                    )
                }.pointerInput(panMode) {
                    if (panMode) return@pointerInput
                    // Giữ yên tới ngưỡng long-press RỒI kéo = drag giữ chuột trái.
                    // Không tranh chấp với detectDrag thường: bên đó cần vượt touch
                    // slop trước, bên này cần đứng yên trước — loại trừ lẫn nhau.
                    detectDragGesturesAfterLongPress(
                        onDragStart = {
                            sendMove()
                            NativeClient.mouseButton(NativeClient.MOUSE_LEFT, true)
                        },
                        onDrag = { change, delta ->
                            change.consume()
                            moveBy(delta)
                        },
                        onDragEnd = {
                            NativeClient.mouseButton(NativeClient.MOUSE_LEFT, false)
                        },
                        onDragCancel = {
                            NativeClient.mouseButton(NativeClient.MOUSE_LEFT, false)
                        },
                    )
                },
    ) {
        if (!rect.isEmpty) {
            // Đọc vị trí TRONG lambda của offset: cả cursor lẫn rect đều là state, đọc
            // ở đây thì mỗi lần dịch con trỏ chỉ chạy lại bước layout chứ không phải
            // cả recomposition.
            CursorArrow(
                modifier =
                    Modifier.offset {
                        val p = screenPos()
                        IntOffset(p.x.roundToInt(), p.y.roundToInt())
                    },
            )
        }
    }
}

/** Mũi tên con trỏ — vẽ tay bằng Path, trắng viền đen để nổi trên mọi nền video. */
@Composable
private fun CursorArrow(modifier: Modifier) {
    Canvas(modifier = modifier.size(18.dp)) {
        val w = size.width
        val h = size.height
        val p =
            Path().apply {
                moveTo(0f, 0f)
                lineTo(0f, h * 0.80f)
                lineTo(w * 0.22f, h * 0.62f)
                lineTo(w * 0.40f, h * 0.98f)
                lineTo(w * 0.55f, h * 0.90f)
                lineTo(w * 0.37f, h * 0.55f)
                lineTo(w * 0.63f, h * 0.55f)
                close()
            }
        drawPath(p, Color.White)
        drawPath(p, Color.Black, style = Stroke(width = 1.dp.toPx()))
    }
}

/**
 * Mức phóng tối đa. 5× đủ đọc chữ nhỏ trên host 4K mà vẫn còn trỏ được: mỗi pixel
 * màn hình lúc đó chỉ còn ứng với 1/5 pixel host. Đối ứng kMaxZoom bên iOS.
 */
private const val MAX_ZOOM = 5f

/**
 * Khung video HIỂN THỊ bên trong vùng nhìn `viewport` — nguồn sự thật DUY NHẤT cho cả
 * chỗ đặt SurfaceView lẫn toạ độ con trỏ. Đối ứng videoFrame bên iOS.
 *
 * Không phóng: rect aspect-fit canh giữa, đúng thứ Modifier.aspectRatio từng dựng.
 * Có phóng: nhân kích thước lên `zoom` (vẫn quanh tâm vùng nhìn) rồi dịch `pan` đã kẹp.
 * `aspect` null = chưa biết kích thước video -> phủ trọn vùng nhìn, như trước.
 */
private fun videoFrame(
    viewport: IntSize,
    aspect: Float?,
    zoom: Float,
    pan: Offset,
): Rect {
    if (viewport.width <= 0 || viewport.height <= 0) return Rect.Zero
    val vw = viewport.width.toFloat()
    val vh = viewport.height.toFloat()
    var baseW = vw
    var baseH = vh
    if (aspect != null && aspect > 0f) {
        baseH = vw / aspect
        if (baseH > vh) {
            baseH = vh
            baseW = vh * aspect
        }
    }
    val w = baseW * zoom
    val h = baseH * zoom
    // Kẹp pan: chiều nào khung lớn hơn vùng nhìn thì kéo được tối đa nửa phần thừa
    // (kéo quá là hở nền đen ở rìa); chiều nào nhỏ hơn thì đứng yên giữa.
    val maxX = ((w - vw) / 2f).coerceAtLeast(0f)
    val maxY = ((h - vh) / 2f).coerceAtLeast(0f)
    val left = (vw - w) / 2f + pan.x.coerceIn(-maxX, maxX)
    val top = (vh - h) / 2f + pan.y.coerceIn(-maxY, maxY)
    return Rect(left, top, left + w, top + h)
}
