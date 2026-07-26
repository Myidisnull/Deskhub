// =============================================================================
// StreamActivity.kt — màn hình XEM + ĐIỀU KHIỂN, chrome dựng trên hệ thiết kế
//                     Deskhub (ui/Tokens.kt + ui/Components.kt), đối ứng
//                     StreamView.swift bên iOS.
//
// KHÔNG CÒN HEADER VÀ THANH ĐÁY ĐẶC — CHỈ CÒN HUD NỔI
//   Bản cũ có một dải xám đặc trên cùng cho dòng trạng thái và một dải nữa dưới đáy
//   cho nút; hai dải ấy ăn khoảng 15% chiều cao màn hình của đúng thứ duy nhất người
//   ta mở màn này ra để nhìn. Bản thiết kế bỏ hẳn chúng: hình từ máy kia chiếm TRỌN
//   màn hình, mọi thứ khác nổi lên trên dưới dạng HUD bo pill. Cả màn LUÔN ở bảng
//   màu tối kể cả khi app đang để giao diện sáng — vùng letterbox phải là màu KHÔNG
//   CÓ, không phải một màu nhạt.
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
//   Hỏi 500 ms một lần rẻ hơn nhiều, mà overlay chỉ đổi mỗi giây một lần.
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
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectDragGesturesAfterLongPress
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.isSpecified
import androidx.compose.ui.geometry.isUnspecified
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import com.deskhub.app.ui.AppState
import com.deskhub.app.ui.Chip
import com.deskhub.app.ui.Credentials
import com.deskhub.app.ui.DeskhubTheme
import com.deskhub.app.ui.Ds
import com.deskhub.app.ui.DsButton
import com.deskhub.app.ui.DsButtonSize
import com.deskhub.app.ui.DsButtonVariant
import com.deskhub.app.ui.DsCheckbox
import com.deskhub.app.ui.DsIconButton
import com.deskhub.app.ui.Eyebrow
import com.deskhub.app.ui.HudBar
import com.deskhub.app.ui.HudDivider
import com.deskhub.app.ui.KeyboardIcon
import com.deskhub.app.ui.MonoText
import com.deskhub.app.ui.PasswordField
import com.deskhub.app.ui.PillTone
import com.deskhub.app.ui.Recents
import com.deskhub.app.ui.Sparkline
import com.deskhub.app.ui.Spinner
import com.deskhub.app.ui.StatePill
import com.deskhub.app.ui.StatusDot
import com.deskhub.app.ui.tr
import kotlinx.coroutines.delay
import kotlin.math.roundToInt

class StreamActivity : ComponentActivity() {
    // Thế hệ phiên do nativeStart trả về (0 = không mở được). Tầng native là
    // singleton mà vòng đời hai StreamActivity có thể chồng lấn nhau (kết thúc rồi
    // kết nối lại ngay) — giữ thế hệ để onDestroy trễ của instance này không giết
    // nhầm phiên mà instance mới vừa mở.
    private var session = 0L
    // Phiên này có tự gửi mật khẩu ĐÃ LƯU không — để StreamScreen biết đường xoá nó
    // đi nếu host trả lời "sai mật khẩu" (host đổi mật khẩu chẳng hạn).
    private var usedSavedPassword = false

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
        AppState.init(this)
        Recents.init(this)
        Credentials.init(this)
        // Người xem không chạm màn hình trong lúc xem, nên nếu không giữ cờ này thì
        // máy tự tắt màn hình giữa chừng — kéo theo Surface bị hủy và phiên đứt.
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        val addr = intent.getStringExtra("addr").orEmpty()
        // GĐ10: chìa mật khẩu + token đã lưu cho host này. Cả hai rỗng ở lần đầu —
        // khi đó host đòi mật khẩu sẽ đẩy phiên sang PHASE_NEED_PASSWORD và
        // StreamScreen hiện hộp thoại.
        val saved = Credentials.forAddress(addr)
        usedSavedPassword = saved?.hasPassword == true
        // Không có "source" (vd. chạy thẳng từ adb) -> nguồn 0, như trước.
        session =
            NativeClient.nativeStart(
                addr,
                intent.getIntExtra("source", 0),
                Credentials.clientId,
                Credentials.deviceName,
                saved?.password.orEmpty(),
                saved?.deviceToken,
            )

        setContent {
            // Ép TỐI bất kể AppState.isDark — xem ghi chú đầu file.
            DeskhubTheme(dark = true) {
                StreamScreen(
                    address = addr,
                    started = session != 0L,
                    usedSavedPassword = usedSavedPassword,
                    holderCallback = holderCallback,
                    onDismiss = { finish() },
                )
            }
        }
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
// chúng chuyển focus khỏi cửa sổ đang chia sẻ, host sẽ ngừng nhận input (xem
// TargetHasFocus bên InputInjector).
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
    started: Boolean,
    usedSavedPassword: Boolean,
    holderCallback: SurfaceHolder.Callback,
    onDismiss: () -> Unit,
) {
    var phase by remember { mutableIntStateOf(NativeClient.PHASE_IDLE) }
    var statusLine by remember { mutableStateOf("") }
    var endReason by remember { mutableStateOf("") }
    var videoW by remember { mutableIntStateOf(0) }
    var videoH by remember { mutableIntStateOf(0) }
    // Dãy RTT cho biểu đồ ở HUD số liệu — dòng số liệu đổi mỗi giây một lần nên
    // 60 mẫu ≈ 60 giây gần nhất, trùng bản iOS (SessionModel.rttTrace).
    val rttTrace = remember { mutableStateListOf<Double>() }

    // Mật khẩu chờ được lưu — CHỈ ghi xuống sau khi host xác nhận nó đúng (phase
    // sang STREAMING). Lưu ngay lúc nhập thì một lần gõ nhầm với ô "Save" bật sẵn
    // sẽ ghi đè bản đúng, và mọi lần kết nối sau tự gửi proof sai — mỗi lần tiêu
    // một lượt trong hạn mức 3 lần sai trước khi host khoá 5 phút.
    var pendingSavePassword by remember { mutableStateOf<String?>(null) }
    // Proof đang bay là bản ĐÃ LƯU (gửi tự động lúc start) hay bản vừa gõ.
    var savedPasswordInPlay by remember { mutableStateOf(usedSavedPassword) }

    // Hỏi trạng thái từ tầng C++ 500ms/lần. Rẻ hơn nhiều so với để C++ gọi ngược
    // lên JVM mỗi frame, và overlay chỉ đổi mỗi giây một lần nên không cần nhanh hơn.
    LaunchedEffect(started) {
        if (!started) return@LaunchedEffect
        // Dòng số liệu chỉ đổi mỗi giây trong khi poll chạy 500ms — chỉ lấy mẫu RTT
        // khi chuỗi THAY ĐỔI, kẻo mỗi giá trị vào biểu đồ hai lần (bậc thang giả).
        var prevStatus = ""
        while (true) {
            phase = NativeClient.nativePhase()
            statusLine = NativeClient.nativeStatusLine()
            videoW = NativeClient.nativeVideoWidth()
            videoH = NativeClient.nativeVideoHeight()
            if (statusLine.isNotEmpty() && statusLine != prevStatus) {
                prevStatus = statusLine
                parseRtt(statusLine)?.let { rtt ->
                    rttTrace.add(rtt)
                    while (rttTrace.size > 60) rttTrace.removeAt(0)
                }
            }
            // GĐ10: token nhớ thiết bị chỉ về ĐÚNG MỘT LẦN, ngay sau khi đáp đúng mật
            // khẩu. Vét mỗi nhịp poll và cất ngay — bỏ lỡ là lần sau phải gõ lại.
            NativeClient.nativeTakeDeviceToken().let { tok ->
                if (tok.isNotEmpty()) Credentials.saveToken(address, tok)
            }
            // Mật khẩu vừa gõ đã được host chấp nhận → giờ mới đáng lưu.
            if (phase == NativeClient.PHASE_STREAMING) {
                pendingSavePassword?.let { pw ->
                    Credentials.savePassword(address, pw)
                    pendingSavePassword = null
                }
            }
            // Hết phiên thì thoát hẳn coroutine: lý do kết thúc không đổi nữa, hỏi
            // tiếp chỉ tốn pin. LaunchedEffect tự hủy coroutine khi rời màn hình.
            if (phase == NativeClient.PHASE_ENDED) {
                endReason = NativeClient.nativeEndReason()
                pendingSavePassword = null // chưa được xác nhận thì không lưu
                // Bản đã lưu bị host từ chối (họ đổi mật khẩu chẳng hạn) → xoá đi,
                // nếu không mọi lần kết nối sau tự gửi proof sai và chết ngay.
                if (savedPasswordInPlay &&
                    NativeClient.nativeRejectReason() == NativeClient.RejectReason.AUTH_FAILED
                ) {
                    Credentials.forgetPassword(address)
                }
                return@LaunchedEffect
            }
            delay(500)
        }
    }

    val streaming = phase == NativeClient.PHASE_STREAMING

    // Bàn phím ảo: bật/tắt bằng nút bàn phím trên HUD. KeyInputView vô hình giữ focus
    // để IME gửi phím; xem giải thích cơ chế TYPE_NULL trong KeyInputView.kt.
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
        // bàn phím — canh ime inset để trạng thái nút không kẹt ở "đang bật". Phải chờ
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

    // Hướng dẫn cử chỉ trackpad: hiện 6 giây kể từ lúc có hình rồi tự tắt — cử chỉ
    // không tự nói ra được, mà một dòng nằm mãi trên màn hình thì thành rác.
    var hintVisible by remember { mutableStateOf(true) }
    LaunchedEffect(streaming) {
        if (!streaming) return@LaunchedEffect
        hintVisible = true
        delay(6000)
        hintVisible = false
    }

    // Bàn phím ảo ĐÈ lên video chứ không co layout (không imePadding/adjustResize)
    // — người dùng muốn khung hình đứng yên khi mở bàn phím.
    Box(
        modifier =
            Modifier
                .fillMaxSize()
                .background(Color.Black),
    ) {
        // --- Tầng video: SurfaceView + trackpad + view hứng phím ---
        Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center,
        ) {
            if (started) {
                // Modifier.aspectRatio lo luôn việc letterbox theo tỉ lệ video.
                val aspect = if (videoW > 0 && videoH > 0) videoW.toFloat() / videoH else null
                val videoModifier =
                    if (aspect != null) Modifier.aspectRatio(aspect) else Modifier.fillMaxSize()

                Box(modifier = videoModifier) {
                    AndroidView(
                        factory = { ctx ->
                            SurfaceView(ctx).apply { holder.addCallback(holderCallback) }
                        },
                        modifier = Modifier.fillMaxSize(),
                    )
                }

                // Trackpad phủ CẢ Box ngoài — gồm cả vùng đen letterbox: rê tay ở đâu
                // cũng di được chuột (trackpad chạy theo delta). "Chỉ xem" thì KHÔNG
                // dựng lớp này: NativeClient đã chặn ở cửa xuống C++, nhưng để lại một
                // con trỏ di được mà máy kia không nhúc nhích là nói dối người dùng.
                if (streaming && !NativeClient.viewOnly) {
                    TrackpadOverlay(
                        videoAspect = aspect,
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
        }

        // --- Tầng chrome: HUD nổi, né tai thỏ/thanh điều hướng ---
        Column(
            modifier =
                Modifier
                    .fillMaxSize()
                    .safeDrawingPadding()
                    .padding(horizontal = 12.dp, vertical = 8.dp),
        ) {
            StatusHud(
                address = address,
                streaming = streaming,
                statusLine = statusLine,
                rttTrace = rttTrace,
                videoW = videoW,
                videoH = videoH,
            )

            Box(modifier = Modifier.weight(1f))

            BottomHud(
                streaming = streaming,
                keyboardOn = keyboardOn,
                hintVisible = hintVisible,
                onToggleKeyboard = { keyboardOn = !keyboardOn },
                onEnd = onDismiss,
            )
        }

        // --- Lớp phủ trạng thái ---
        if (!started || phase == NativeClient.PHASE_ENDED) {
            EndedOverlay(
                reason = if (!started) "${tr("invalidAddress")}: $address" else endReason,
                onBack = onDismiss,
            )
        } else if (phase == NativeClient.PHASE_NEED_PASSWORD) {
            // GĐ10: host đòi mật khẩu. Phiên VẪN SỐNG phía dưới (tầng C++ tiếp tục
            // phát lại HELLO), nên đây chỉ là một lớp phủ — nhập xong là đi tiếp,
            // không phải kết nối lại từ đầu.
            PasswordOverlay(
                address = address,
                onSubmit = { pw, remember ->
                    // Chưa lưu vội — vòng poll ghi xuống khi host xác nhận (STREAMING).
                    pendingSavePassword = if (remember) pw else null
                    savedPasswordInPlay = false // proof sắp bay là bản vừa gõ
                    NativeClient.nativeSubmitPassword(pw)
                },
                onCancel = onDismiss,
            )
        } else if (!streaming) {
            ConnectingOverlay(address = address)
        }
    }
}

/** HUD trên: máy đang xem + pill trạng thái, và dòng số liệu ở HUD riêng bên dưới. */
@Composable
private fun StatusHud(
    address: String,
    streaming: Boolean,
    statusLine: String,
    rttTrace: List<Double>,
    videoW: Int,
    videoH: Int,
) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        HudBar {
            StatusDot(live = streaming)
            MonoText(
                text = if (videoW > 0) "$address — $videoW×$videoH" else address,
                color = Ds.colors.textPrimary,
                maxLines = 1,
            )
            StatePill(
                text =
                    if (NativeClient.viewOnly) {
                        tr("viewOnly")
                    } else if (streaming) {
                        tr("streaming")
                    } else {
                        tr("connecting")
                    },
                tone = if (streaming) PillTone.LIVE else PillTone.NEUTRAL,
            )
        }
        // Dòng số liệu là HUD RIÊNG: gộp chung hàng trên thì trên màn dọc nó dài quá
        // bề ngang máy. Nó cũng chỉ có nghĩa khi đã có hình.
        if (streaming && statusLine.isNotEmpty()) {
            HudBar {
                MonoText(text = statusLine, color = Ds.colors.textPrimary, maxLines = 1)
                if (rttTrace.size >= 2) {
                    HudDivider()
                    Sparkline(
                        values = rttTrace,
                        modifier = Modifier.size(width = 64.dp, height = 18.dp),
                    )
                }
            }
        }
    }
}

// Bóc "RTT 4 ms" ra khỏi dòng số liệu mà ClientLoop dựng sẵn, thay vì mở thêm một
// hàm JNI thứ hai chỉ để trả về đúng con số đó. Dòng ấy được dựng ở MỘT chỗ
// (ClientLoop.cpp) và bản iOS/Windows cũng bóc RTT ra khỏi cùng chuỗi đó — mấy
// client đọc cùng một nguồn thì không có cách nào lệch nhau.
private fun parseRtt(line: String): Double? {
    val idx = line.indexOf("RTT ")
    if (idx < 0) return null
    return line
        .drop(idx + 4)
        .takeWhile { it.isDigit() || it == '.' }
        .toDoubleOrNull()
}

/** HUD dưới: hướng dẫn cử chỉ + hàng phím tắt cuộn ngang + cụm bàn phím/Kết thúc. */
@Composable
private fun BottomHud(
    streaming: Boolean,
    keyboardOn: Boolean,
    hintVisible: Boolean,
    onToggleKeyboard: () -> Unit,
    onEnd: () -> Unit,
) {
    val inputEnabled = streaming && !NativeClient.viewOnly

    Column(
        modifier = Modifier.fillMaxWidth(),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        if (hintVisible && inputEnabled) {
            MonoText(text = tr("trackpadHint"), maxLines = 1)
        }

        // Phím tắt là những pill RỜI cuộn ngang — một HUD kính dài gấp đôi màn hình
        // trượt qua lại thì trông như thanh HUD bị hỏng.
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            kHotkeys.forEach { hk ->
                DsButton(
                    text = hk.label,
                    onClick = {
                        if (hk.modVk != 0) {
                            NativeClient.keyChord(hk.modVk, hk.modScan, hk.vk, hk.scan)
                        } else {
                            NativeClient.keyTap(hk.vk, hk.scan)
                        }
                    },
                    variant = DsButtonVariant.SECONDARY,
                    size = DsButtonSize.SM,
                    enabled = inputEnabled,
                    pill = true,
                )
            }
        }

        HudBar {
            DsIconButton(
                onClick = onToggleKeyboard,
                side = 34.dp,
                radius = Ds.radiusPill,
                active = keyboardOn,
                enabled = inputEnabled,
            ) {
                // Icon vẽ tay (ui/Icons.kt) — ký tự "⌨" là chữ nên bé và lệch
                // baseline; 18dp ở đây trùng cỡ symbol keyboard 16pt của bản iOS.
                KeyboardIcon(
                    size = 18.dp,
                    color = if (keyboardOn) Ds.colors.accent else Ds.colors.textPrimary,
                )
            }

            HudDivider()

            // Chip chứ không phải chữ trần — trùng bản iOS: nhãn trạng thái đứng
            // giữa hai nút cần cái viền của chip để không dính vào chúng.
            Chip(
                text =
                    if (NativeClient.viewOnly) {
                        tr("viewOnly")
                    } else if (keyboardOn) {
                        tr("keysOn")
                    } else {
                        tr("keys")
                    },
                active = keyboardOn,
            )

            HudDivider()

            DsButton(
                text = tr("end"),
                onClick = onEnd,
                variant = DsButtonVariant.DANGER,
                size = DsButtonSize.SM,
                pill = true,
            )
        }
    }
}

@Composable
private fun ConnectingOverlay(address: String) {
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        val shape = RoundedCornerShape(Ds.radiusXl)
        Column(
            modifier =
                Modifier
                    .background(Ds.colors.surfacePanel, shape)
                    .border(Ds.hairline, Ds.colors.borderHairline, shape)
                    .padding(22.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Spinner(size = 22.dp)
            MonoText(text = "${tr("connecting")} $address", color = Ds.colors.textPrimary)
        }
    }
}

/**
 * GĐ10 — hộp thoại nhập mật khẩu, hiện khi host đòi mà máy này chưa có.
 *
 * Ứng với màn `05 · settings / password` của thiết kế, phần "Password to connect".
 * Mật khẩu KHÔNG đi lên dây: tầng C++ đổi nó thành một proof HMAC theo challenge của
 * host (xem docs/04-protocol.md §7b), nên chuỗi này không rời khỏi máy.
 */
@Composable
private fun PasswordOverlay(
    address: String,
    onSubmit: (String, Boolean) -> Unit,
    onCancel: () -> Unit,
) {
    var password by remember { mutableStateOf("") }
    var savePassword by remember { mutableStateOf(true) }
    var reveal by remember { mutableStateOf(false) }
    // Sai mật khẩu thì tầng C++ kết thúc phiên (PHASE_ENDED) chứ không quay lại đây,
    // nên chỗ này chỉ cần chặn lần gửi rỗng.
    val canSubmit = password.isNotBlank()

    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        val shape = RoundedCornerShape(Ds.radiusXl)
        Column(
            modifier =
                Modifier
                    .padding(24.dp)
                    .background(Ds.colors.surfacePanel, shape)
                    .border(Ds.hairline, Ds.colors.borderHairline, shape)
                    .padding(22.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            Eyebrow(text = tr("securityEyebrow"))
            Text(
                text = tr("connectPassword"),
                fontSize = Ds.textBodyLg,
                fontWeight = FontWeight.SemiBold,
                color = Ds.colors.textPrimary,
            )
            MonoText(text = address, color = Ds.colors.textSecondary)

            PasswordField(
                value = password,
                onValueChange = { password = it },
                placeholder = tr("connectPassword"),
                reveal = reveal,
                onToggleReveal = { reveal = !reveal },
                onGo = { if (canSubmit) onSubmit(password, savePassword) },
            )
            DsCheckbox(
                checked = savePassword,
                onToggle = { savePassword = it },
                label = tr("savePassword"),
            )
            MonoText(text = tr("passwordHintPhone"))

            DsButton(
                text = tr("connect"),
                onClick = { if (canSubmit) onSubmit(password, savePassword) },
                variant = DsButtonVariant.PRIMARY,
                enabled = canSubmit,
                fullWidth = true,
            )
            DsButton(
                text = tr("back"),
                onClick = onCancel,
                variant = DsButtonVariant.SECONDARY,
                fullWidth = true,
            )
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
        val shape = RoundedCornerShape(Ds.radiusXl)
        Column(
            modifier =
                Modifier
                    .padding(24.dp)
                    .background(Ds.colors.surfacePanel, shape)
                    .border(Ds.hairline, Ds.colors.borderHairline, shape)
                    .padding(22.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            Eyebrow(text = tr("sessionEnded"))
            Text(
                text = reason,
                fontSize = Ds.textBodyLg,
                fontWeight = FontWeight.Normal,
                color = Ds.colors.textPrimary,
            )
            DsButton(
                text = tr("back"),
                onClick = onBack,
                variant = DsButtonVariant.PRIMARY,
                fullWidth = true,
            )
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
 * Overlay phủ CẢ vùng hiển thị (gồm vùng đen letterbox), nhưng con trỏ bị kẹp
 * trong KHUNG VIDEO thật — rect tính từ `videoAspect` (aspect-fit, canh giữa) —
 * và toạ độ gửi đi chuẩn hoá 0..65535 theo rect đó qua [sendMouseMove].
 */
@Composable
private fun TrackpadOverlay(
    videoAspect: Float?,
    modifier: Modifier,
) {
    var cursor by remember { mutableStateOf(Offset.Unspecified) }
    // Khung đổi kích thước (xoay màn hình) -> kẹp con trỏ lại trong khung mới.
    var bounds by remember { mutableStateOf(IntSize.Zero) }

    // Khung video thật bên trong overlay: aspect-fit canh giữa — trùng công thức
    // letterbox của Modifier.aspectRatio bên ngoài.
    fun videoRect(): Rect {
        if (bounds.width <= 0 || bounds.height <= 0) return Rect.Zero
        val bw = bounds.width.toFloat()
        val bh = bounds.height.toFloat()
        if (videoAspect == null || videoAspect <= 0f) return Rect(0f, 0f, bw, bh)
        return if (bw / bh > videoAspect) {
            val vw = bh * videoAspect // thừa ngang: video cao hết cỡ, đen hai bên
            Rect((bw - vw) / 2f, 0f, (bw + vw) / 2f, bh)
        } else {
            val vh = bw / videoAspect // thừa dọc: video rộng hết cỡ, đen trên dưới
            Rect(0f, (bh - vh) / 2f, bw, (bh + vh) / 2f)
        }
    }

    fun moveBy(delta: Offset) {
        val rect = videoRect()
        if (rect.width <= 0f || cursor.isUnspecified) return
        cursor =
            Offset(
                (cursor.x + delta.x).coerceIn(rect.left, rect.right),
                (cursor.y + delta.y).coerceIn(rect.top, rect.bottom),
            )
        sendMouseMove(cursor, rect)
    }

    // Host cũng có người dùng thật di chuột được — gửi lại vị trí con trỏ ngay
    // trước mỗi cú click để chắc chắn click rơi đúng chỗ con trỏ đang hiển thị.
    fun clickAt(button: Int) {
        if (cursor.isUnspecified) return
        sendMouseMove(cursor, videoRect())
        NativeClient.mouseButton(button, true)
        NativeClient.mouseButton(button, false)
    }

    Box(
        modifier =
            modifier
                .onSizeChanged { sz ->
                    bounds = sz
                    val rect = videoRect()
                    cursor =
                        if (cursor.isUnspecified) {
                            rect.center
                        } else {
                            Offset(
                                cursor.x.coerceIn(rect.left, rect.right),
                                cursor.y.coerceIn(rect.top, rect.bottom),
                            )
                        }
                }.pointerInput(Unit) {
                    // Có onDoubleTap nên onTap phải chờ hết cửa sổ double-tap
                    // (~300ms) mới nổ — giá phải trả để phân biệt được hai cử chỉ.
                    detectTapGestures(
                        onTap = { clickAt(NativeClient.MOUSE_LEFT) },
                        onDoubleTap = { clickAt(NativeClient.MOUSE_RIGHT) },
                    )
                }.pointerInput(Unit) {
                    // Rê tự do (không giữ nút nào): di con trỏ theo delta.
                    detectDragGestures(
                        onDrag = { change, delta ->
                            change.consume()
                            moveBy(delta)
                        },
                    )
                }.pointerInput(Unit) {
                    // Giữ yên tới ngưỡng long-press RỒI kéo = drag giữ chuột trái.
                    // Không tranh chấp với detectDrag thường: bên đó cần vượt touch
                    // slop trước, bên này cần đứng yên trước — loại trừ lẫn nhau.
                    detectDragGesturesAfterLongPress(
                        onDragStart = {
                            if (cursor.isSpecified) sendMouseMove(cursor, videoRect())
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
        if (cursor.isSpecified) {
            CursorArrow(
                modifier =
                    Modifier.offset { IntOffset(cursor.x.roundToInt(), cursor.y.roundToInt()) },
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

// Chuẩn hoá vị trí con trỏ theo KHUNG VIDEO (không phải cả overlay) rồi gửi.
private fun sendMouseMove(
    pos: Offset,
    rect: Rect,
) {
    if (rect.width <= 0f || rect.height <= 0f) return
    NativeClient.mouseMove(
        (((pos.x - rect.left) / rect.width) * 65535f).roundToInt(),
        (((pos.y - rect.top) / rect.height) * 65535f).roundToInt(),
    )
}
