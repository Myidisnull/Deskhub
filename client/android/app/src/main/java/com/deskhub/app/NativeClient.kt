// =============================================================================
// NativeClient.kt — mặt tiền Kotlin của libdeskhub.so.
//
// NHIỆM VỤ
//   Chỗ DUY NHẤT trong phần Kotlin được phép gọi xuống C++. Mọi Activity đi qua
//   đây, không Activity nào tự khai báo external fun của riêng nó.
//
// VỊ TRÍ TRONG KIẾN TRÚC
//   MainActivity / StreamActivity → **NativeClient** → JniBridge.cpp → ClientLoop
//
// RÀNG BUỘC SỐNG CÒN: TÊN PHẢI KHỚP TỪNG CHỮ
//   Tên hàm native ở đây quyết định tên hàm C++ bên JniBridge.cpp
//   (Java_com_deskhub_app_NativeClient_*). Liên kết diễn ra LÚC CHẠY theo chuỗi,
//   nên đổi tên gói, tên object, hay tên hàm mà quên sửa bên C++ thì trình dịch
//   KHÔNG báo gì — app chỉ chết bằng UnsatisfiedLinkError khi chạm tới hàm đó.
//
// VÌ SAO LÀ `object` CHỨ KHÔNG PHẢI CLASS
//   Tầng C++ giữ đúng một ClientLoop toàn cục, nên một thực thể duy nhất bên Kotlin
//   là ánh xạ đúng của thực tế đó. Khối init nạp thư viện .so đúng một lần, lần đầu
//   có ai chạm tới object.
//
// PHÂN TẦNG TRONG CHÍNH FILE NÀY
//   Hàm `external` là raw, gọi thẳng. Riêng nativeListSources được bọc lại thành
//   suspend fun listSources() vì nó chặn 3 giây — không ai được gọi bản raw đó từ
//   main thread. Đó là lý do nó `private` còn các hàm khác thì không.
//
// LIÊN QUAN: JniBridge.cpp (phía C++, phải khớp tên), MainActivity.kt, StreamActivity.kt
// =============================================================================
package com.deskhub.app

import android.view.Surface
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Mặt tiền Kotlin của libdeskhub.so. Tên hàm native phải khớp đúng chữ với
 * JniBridge.cpp (Java_com_deskhub_app_NativeClient_*) — đổi tên gói hoặc tên
 * lớp là phải sửa cả bên C++.
 */
object NativeClient {
    // Trùng ClientLoop::Phase bên C++ — năm giá trị này là một enum bị tách làm đôi
    // qua ranh giới JNI (nativePhase trả jint), nên sửa một bên phải sửa cả bên kia.
    const val PHASE_IDLE = 0
    const val PHASE_CONNECTING = 1
    const val PHASE_STREAMING = 2
    const val PHASE_ENDED = 3

    // Nạp .so một lần, lần đầu có ai chạm tới object này. Phải chạy trước mọi lời
    // gọi external fun, và khối init của object bảo đảm đúng điều đó.
    init {
        System.loadLibrary("deskhub")
    }

    /**
     * Host đang chia sẻ những màn hình nào. CHẶN tới ~3 giây (LIST_SOURCES đi trên UDP,
     * phát lại vài lần) nên phải gọi ngoài main thread — dùng [listSources].
     */
    private external fun nativeListSources(addr: String): Array<String>

    /**
     * `sourceId` lấy từ [listSources]; 0 = nguồn đầu tiên, cũng là thứ host đời cũ
     * (chỉ một nguồn) hiểu được. Trả về THẾ HỆ của phiên vừa tạo (0 = địa chỉ sai
     * cú pháp) — giữ nó lại và đưa cho [nativeStop], để onDestroy trễ của một
     * StreamActivity cũ không giết nhầm phiên mà activity mới vừa mở (vòng đời hai
     * activity chồng lấn nhau, Android không hứa gì về thứ tự).
     */
    external fun nativeStart(
        addr: String,
        sourceId: Int,
    ): Long

    /**
     * Dừng phiên có thế hệ đúng bằng `generation` (giá trị [nativeStart] đã trả);
     * phiên hiện tại là thế hệ khác thì không đụng. 0 = dừng vô điều kiện.
     */
    external fun nativeStop(generation: Long)

    /**
     * Giao/thu hồi Surface. Truyền null CHẶN tới khi bộ giải mã buông surface ra,
     * nên bắt buộc gọi trong surfaceDestroyed() — trả về rồi thì Surface bị hủy
     * thật, codec còn vẽ vào đó là dùng-sau-giải-phóng.
     */
    external fun nativeSetSurface(surface: Surface?)

    /**
     * Thu hồi khi MỘT Surface cụ thể bị hủy — gọi trong surfaceDestroyed() với chính
     * surface đang chết. Khác [nativeSetSurface] (null) ở chỗ tầng C++ so DANH TÍNH:
     * surfaceDestroyed trễ của một StreamActivity cũ sẽ không giật mất cửa sổ mà
     * phiên mới đang vẽ vào. Cũng CHẶN tới khi bộ giải mã buông surface ra.
     */
    external fun nativeReleaseSurface(surface: Surface)

    // Nút chuột theo deskhub::MouseButton (Wire.h).
    const val MOUSE_LEFT = 1
    const val MOUSE_RIGHT = 2

    /*
     * KHÔNG còn cờ "chỉ xem" (bỏ 2026-07-27): chuột/bàn phím luôn được gửi. Các hàm
     * `external` vẫn để private và UI vẫn gọi qua wrapper — giữ đúng một cửa xuống
     * JNI thì sau này thêm luật gì cũng chỉ phải sửa một chỗ.
     */

    /**
     * Gõ một phím rời (nhấn + nhả ngay) sang host — phím đặc biệt của thanh phím tắt
     * (Esc, mũi tên...); phím chữ đi đường [charTap]. `vk` là mã phím ảo Windows,
     * `scan` là scancode (bit8 = cờ E0). Chỉ có tác dụng khi phiên đang STREAMING.
     */
    private external fun nativeKeyTap(
        vk: Int,
        scan: Int,
    )

    /** Tổ hợp kiểu Ctrl+C: giữ phím bổ trợ, gõ phím chính, nhả theo đúng thứ tự. */
    private external fun nativeKeyChord(
        modVk: Int,
        modScan: Int,
        vk: Int,
        scan: Int,
    )

    /** Chuột tuyệt đối từ touch: toạ độ chuẩn hoá 0..65535 trong khung video. */
    private external fun nativeMouseMove(
        nx: Int,
        ny: Int,
    )

    /**
     * Chuột tương đối — chế độ khoá chuột cho game FPS (nút Lock): delta thô,
     * game tự áp sensitivity. Chưa có UI gọi; giữ lại cho GĐ sau.
     */
    private external fun nativeMouseMoveRel(
        dx: Int,
        dy: Int,
    )

    /** Nhấn/nhả nút chuột ([MOUSE_LEFT]/[MOUSE_RIGHT]) tại vị trí con trỏ hiện hành. */
    private external fun nativeMouseButton(
        button: Int,
        down: Boolean,
    )

    /** Gõ một ký tự từ bàn phím ảo (core quy đổi sang VK theo layout US). */
    private external fun nativeCharTap(codepoint: Int)

    fun keyTap(
        vk: Int,
        scan: Int,
    ) {
        nativeKeyTap(vk, scan)
    }

    fun keyChord(
        modVk: Int,
        modScan: Int,
        vk: Int,
        scan: Int,
    ) {
        nativeKeyChord(modVk, modScan, vk, scan)
    }

    fun mouseMove(
        nx: Int,
        ny: Int,
    ) {
        nativeMouseMove(nx, ny)
    }

    fun mouseMoveRel(
        dx: Int,
        dy: Int,
    ) {
        nativeMouseMoveRel(dx, dy)
    }

    fun mouseButton(
        button: Int,
        down: Boolean,
    ) {
        nativeMouseButton(button, down)
    }

    fun charTap(codepoint: Int) {
        nativeCharTap(codepoint)
    }

    external fun nativePhase(): Int

    external fun nativeStatusLine(): String

    external fun nativeEndReason(): String

    external fun nativeVideoWidth(): Int

    external fun nativeVideoHeight(): Int

    /** Một màn hình host đang chia sẻ. `name` chỉ để hiển thị. */
    data class Source(
        val id: Int,
        val width: Int,
        val height: Int,
        val name: String,
    )

    /**
     * Bọc [nativeListSources] và giải mã dạng "id\twidth\theight\tname" mà JNI trả về
     * (chuỗi phẳng thay vì dựng object Kotlin từ C++: bên đó chỉ cần NewStringUTF, khỏi
     * phải tra FindClass/GetMethodID cho một kiểu chỉ dùng đúng ở đây).
     *
     * Danh sách rỗng = host im lặng HOẶC không chia sẻ gì; caller cứ thử nguồn 0 và
     * để tầng dưới báo lỗi thật.
     */
    suspend fun listSources(addr: String): List<Source> =
        withContext(Dispatchers.IO) {
            nativeListSources(addr).mapNotNull { line ->
                // limit = 4: tên nguồn có thể chứa tab, không được cắt tiếp.
                val f = line.split('\t', limit = 4)
                if (f.size < 4) return@mapNotNull null
                val id = f[0].toIntOrNull() ?: return@mapNotNull null
                Source(id, f[1].toIntOrNull() ?: 0, f[2].toIntOrNull() ?: 0, f[3])
            }
        }
}
