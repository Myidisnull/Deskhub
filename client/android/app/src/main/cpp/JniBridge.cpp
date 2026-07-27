// =============================================================================
// JniBridge.cpp — mặt tiền JNI của libdeskhub.so, thay cho NativeActivity cũ.
//
// NHIỆM VỤ
//   Ranh giới giữa Kotlin và C++. Cố ý MỎNG: Kotlin lo phần người dùng nhìn thấy
//   (ô nhập IP, nút bấm, chữ trạng thái), C++ lo mạng và giải mã. Ở đây không có
//   logic nào ngoài chuyển đổi kiểu và giữ vòng đời của một ClientLoop.
//
// VỊ TRÍ TRONG KIẾN TRÚC
//   MainActivity/StreamActivity (Kotlin) → NativeClient.kt → **JniBridge.cpp**
//                                                              → ClientLoop → core
//
// QUY TẮC ĐẶT TÊN CỦA JNI — CẠM BẪY LỚN NHẤT Ở FILE NÀY
//   Tên hàm phải là Java_<gói>_<lớp>_<hàm> với dấu chấm đổi thành gạch dưới. Đây
//   là liên kết theo TÊN CHUỖI, thực hiện lúc chạy: đổi tên gói, tên lớp, hoặc tên
//   hàm ở NativeClient.kt mà quên sửa ở đây thì KHÔNG có lỗi biên dịch nào cả —
//   app dịch ra bình thường rồi chết bằng UnsatisfiedLinkError ngay khi chạm vào
//   hàm đó. Sửa một bên là phải sửa bên kia.
//
// VÌ SAO BIẾN TOÀN CỤC THAY VÌ TRẢ CON TRỎ CHO KOTLIN GIỮ
//   App chỉ xem được một nguồn tại một thời điểm (view-only v1), nên một biến tĩnh
//   vừa đủ. Đổi lại ta bỏ được cả một lớp lỗi: Kotlin cầm con trỏ dưới dạng jlong,
//   giữ nó qua một lần xoay màn hình, rồi gọi vào một đối tượng đã bị hủy.
//
// MÔ HÌNH LUỒNG
//   Mọi hàm ở đây gọi từ UI thread, TRỪ nativeListSources — nó chặn tới 3 giây nên
//   phía Kotlin đã bọc trong Dispatchers.IO. Vì chỉ có một thread gọi vào, hai biến
//   toàn cục dưới đây không cần khoá.
//
// LIÊN QUAN: NativeClient.kt (phía Kotlin, phải khớp tên từng chữ), ClientLoop.h,
//            net/SourceQuery.h
// =============================================================================
#include <android/native_window_jni.h>
#include <jni.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "ClientLoop.h"
#include "Log.h"
#include "net/SourceQuery.h"
#include "net/UdpSocket.h"

namespace {


// Phiên đang chạy (null = chưa kết nối) và Surface đang giữ (null = app ở nền).
// Hai thứ này ĐỘC LẬP về thời điểm xuất hiện: Surface có thể sẵn sàng trước khi
// người dùng bấm Connect, hoặc ngược lại. Mọi chỗ dùng chúng đều phải chịu được cả
// hai thứ tự — đó là lý do nativeStart và nativeSetSurface đều kiểm tra cái kia.
std::unique_ptr<ClientLoop> g_client;
ANativeWindow* g_window = nullptr;

// Thế hệ phiên, tăng mỗi lần nativeStart thành công. Cần vì vòng đời Activity CHỒNG
// LẤN nhau: người dùng kết thúc phiên rồi kết nối lại ngay, onCreate của
// StreamActivity MỚI có thể chạy trước onDestroy của cái CŨ (Android không hứa gì về
// thứ tự với activity đang finish). onDestroy cũ mà gọi một nativeStop vô điều kiện
// là nó bóp chết phiên mới toanh — nên mỗi Activity giữ thế hệ của phiên MÌNH tạo và
// chỉ dừng được đúng phiên đó.
uint64_t g_generation = 0;

jstring ToJString(JNIEnv* env, const std::string& s) {
    return env->NewStringUTF(s.c_str());
}

// Đọc jstring rồi thả ngay — mọi hàm dưới đây chỉ cần bản copy.
std::string FromJString(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string out = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return out;
}

} // namespace

extern "C" {

// Trả về mảng String, mỗi dòng "id\twidth\theight\tname" — xem NativeClient.kt.
// Mảng RỖNG nghĩa là host không trả lời (bản cũ / mất gói), KHÁC với host trả lời
// rằng nó không chia sẻ gì; Kotlin gộp cả hai thành "cứ thử nguồn 0".
// CHẶN tới ~3s: gọi trên thread nền.
JNIEXPORT jobjectArray JNICALL
Java_com_deskhub_app_NativeClient_nativeListSources(JNIEnv* env, jobject, jstring addrStr) {
    jclass stringClass = env->FindClass("java/lang/String");

    const std::string addr = FromJString(env, addrStr);
    NetAddr server;
    std::vector<deskhub::SourceInfo> sources;
    if (ParseNetAddr(addr, server)) {
        QuerySources(server, sources);
    } else {
        LOGE("[JNI] Invalid host address: \"%s\"", addr.c_str());
    }

    jobjectArray arr = env->NewObjectArray(jsize(sources.size()), stringClass, nullptr);
    for (size_t i = 0; i < sources.size(); ++i) {
        const deskhub::SourceInfo& s = sources[i];
        char head[32];
        std::snprintf(head, sizeof(head), "%u\t%u\t%u\t", s.sourceId, s.width, s.height);
        env->SetObjectArrayElement(arr, jsize(i), ToJString(env, head + s.name));
    }
    return arr;
}

// Trả về THẾ HỆ của phiên vừa tạo (0 = thất bại). Caller giữ nó lại và đưa cho
// nativeStop — xem chú thích ở g_generation.
JNIEXPORT jlong JNICALL
Java_com_deskhub_app_NativeClient_nativeStart(JNIEnv* env, jobject, jstring addrStr,
    jint sourceId) {
    const std::string addr = FromJString(env, addrStr);

    NetAddr server;
    if (!ParseNetAddr(addr, server)) {
        LOGE("[JNI] Invalid host address: \"%s\"", addr.c_str());
        return 0;
    }

    g_client = std::make_unique<ClientLoop>();
    if (!g_client->Start(server, uint8_t(sourceId))) {
        g_client.reset();
        return 0;
    }
    // Surface có thể đã sẵn sàng trước khi bấm Connect (SurfaceView tạo xong trước).
    if (g_window) g_client->SetWindow(g_window);
    return jlong(++g_generation);
}

// Stop() chờ cả hai thread thoát hẳn rồi reset mới hủy đối tượng — nên sau khi hàm
// này trả về, không còn thread nào của phiên cũ chạm vào Surface nữa.
//
// `generation` là giá trị nativeStart đã trả cho caller: chỉ dừng nếu phiên đang
// chạy đúng là phiên caller tạo (0 = dừng vô điều kiện). Thiếu phép so này thì
// onDestroy trễ của một StreamActivity cũ giết nhầm phiên mới — xem g_generation.
JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeStop(JNIEnv*, jobject, jlong generation) {
    if (!g_client) return;
    if (generation != 0 && uint64_t(generation) != g_generation) return;
    g_client->Stop();
    g_client.reset();
}

// Giao hoặc thu hồi Surface. Đây là hàm tinh tế nhất file — nó quản lý vòng đời của
// một tài nguyên mà hai bên cùng nắm giữ.
//
// ANativeWindow_fromSurface TĂNG bộ đếm tham chiếu, nên mỗi lần gọi nó phải có đúng
// một lần ANativeWindow_release đối ứng. Thiếu release là rò; thừa release là hủy
// sớm một cửa sổ mà codec còn đang vẽ vào.
//
// surface = null khi Surface bị hủy. Phải gọi TRƯỚC khi Surface thật sự biến mất
// (tức trong surfaceDestroyed), vì SetWindow chặn tới khi codec buông nó ra.
JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeSetSurface(JNIEnv* env, jobject, jobject surface) {
    if (surface) {
        ANativeWindow* w = ANativeWindow_fromSurface(env, surface);
        if (g_window == w) {
            // Android giao lại ĐÚNG Surface đang giữ (surfaceCreated bắn lại không có
            // destroy xen giữa). fromSurface vừa +1 lần nữa cho cùng cửa sổ — phải
            // trả lại ref thừa đó, nếu không mỗi lần như vậy rò vĩnh viễn một tham
            // chiếu và buffer queue phía sau Surface không bao giờ được giải phóng
            // trọn vẹn. Ta chỉ theo dõi MỘT ref cho g_window.
            if (w) ANativeWindow_release(w);
        } else {
            // Đã giữ một cửa sổ KHÁC: buông nó theo đúng thứ tự an toàn trước khi thay.
            if (g_window) {
                if (g_client) g_client->SetWindow(nullptr);
                ANativeWindow_release(g_window);
            }
            g_window = w;
        }
        if (g_client && g_window) g_client->SetWindow(g_window);
    } else {
        // Thứ tự bắt buộc: bảo ClientLoop buông trước, RỒI mới release. Đảo lại là
        // codec đang render vào một ANativeWindow đã bị hủy.
        if (g_client) g_client->SetWindow(nullptr);
        if (g_window) {
            ANativeWindow_release(g_window);
            g_window = nullptr;
        }
    }
}

// Thu hồi khi MỘT Surface cụ thể bị hủy (surfaceDestroyed). Khác nativeSetSurface(null)
// ở chỗ có so DANH TÍNH: vòng đời hai StreamActivity chồng lấn nhau thì
// surfaceDestroyed trễ của activity cũ không được phép giật cửa sổ mà phiên MỚI đang
// vẽ vào. Chỉ khi surface chết đúng là cái đang giữ mới buông.
JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeReleaseSurface(JNIEnv* env, jobject, jobject surface) {
    if (!surface) return;
    ANativeWindow* w = ANativeWindow_fromSurface(env, surface); // +1 tạm, chỉ để so
    const bool ours = (w == g_window);
    if (w) ANativeWindow_release(w);
    if (!ours) return; // surface của một activity cũ đang đóng — không đụng phiên hiện tại
    if (g_client) g_client->SetWindow(nullptr);
    ANativeWindow_release(g_window);
    g_window = nullptr;
}

// Gõ một phím rời (nhấn + nhả) sang host — nút F9 trên header màn hình xem.
// `vk` là mã phím ảo Windows, `scan` là scancode (xem Wire.h).
JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeKeyTap(JNIEnv*, jobject, jint vk, jint scan) {
    if (g_client) g_client->QueueKeyTap(int32_t(vk), int32_t(scan));
}

// Chuột tuyệt đối từ touch: toạ độ chuẩn hoá 0..65535 trong khung video.
JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeMouseMove(JNIEnv*, jobject, jint nx, jint ny) {
    if (g_client) g_client->QueueMouseMoveAbs(int32_t(nx), int32_t(ny));
}

// Tổ hợp kiểu Ctrl+C: giữ phím bổ trợ, gõ phím chính, nhả theo đúng thứ tự.
JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeKeyChord(JNIEnv*, jobject, jint modVk, jint modScan,
    jint vk, jint scan) {
    if (g_client)
        g_client->QueueKeyChord(int32_t(modVk), int32_t(modScan), int32_t(vk), int32_t(scan));
}

// Chuột tương đối (chế độ khoá chuột cho game FPS): delta thô, absolute = 0.
JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeMouseMoveRel(JNIEnv*, jobject, jint dx, jint dy) {
    if (g_client) g_client->QueueMouseMoveRel(int32_t(dx), int32_t(dy));
}

// Nhấn/nhả nút chuột (1 = trái, 2 = phải) tại vị trí con trỏ hiện hành.
JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeMouseButton(JNIEnv*, jobject, jint button,
    jboolean down) {
    if (g_client) g_client->QueueMouseButton(int32_t(button), down == JNI_TRUE);
}

// Gõ một ký tự từ bàn phím ảo (KeyMap của core quy đổi sang VK, layout US).
JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeCharTap(JNIEnv*, jobject, jint codepoint) {
    if (g_client && codepoint > 0) g_client->QueueCharTap(uint32_t(codepoint));
}

JNIEXPORT jint JNICALL
Java_com_deskhub_app_NativeClient_nativePhase(JNIEnv*, jobject) {
    return g_client ? jint(g_client->phase()) : jint(ClientLoop::Phase::Idle);
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeStatusLine(JNIEnv* env, jobject) {
    return ToJString(env, g_client ? g_client->StatusLine() : std::string());
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeEndReason(JNIEnv* env, jobject) {
    return ToJString(env, g_client ? g_client->EndReason() : std::string());
}

JNIEXPORT jint JNICALL
Java_com_deskhub_app_NativeClient_nativeVideoWidth(JNIEnv*, jobject) {
    return g_client ? jint(g_client->videoWidth()) : 0;
}

JNIEXPORT jint JNICALL
Java_com_deskhub_app_NativeClient_nativeVideoHeight(JNIEnv*, jobject) {
    return g_client ? jint(g_client->videoHeight()) : 0;
}

} // extern "C"
