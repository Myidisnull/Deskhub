#include <android/native_window_jni.h>
#include <jni.h>

#include <atomic>
#include <string>
#include <vector>

#include "ClientSessionAndroid.h"

#include "deskhub/input/Hotkeys.h"
#include "deskhub/media/ViewFit.h"
#include "deskhub/protocol/Wire.h"
#include "deskhubp/ffi/ClientFfi.h"

static_assert(DHPhaseIdle == 0 && DHPhaseConnecting == 1 && DHPhaseStreaming == 2 &&
              DHPhaseEnded == 3);
static_assert(DHStrClientIpPrompt == 3 && DHStrQueryingSources == 12 &&
              DHStrInvalidAddressHint == 17 && DHStrSessionEnded == 18);
static_assert(int32_t(deskhub::MouseButton::Left) == 1 &&
              int32_t(deskhub::MouseButton::Right) == 2);

namespace {

DHSession* g_session = nullptr;
ANativeWindow* g_window = nullptr;

JavaVM* g_vm = nullptr;
jclass g_nativeClientClass = nullptr;
jmethodID g_onSessionStatus = nullptr;
jmethodID g_onSessionSize = nullptr;
jmethodID g_onSessionEnded = nullptr;
std::atomic<DHSession*> g_callbackSession{nullptr};

template <class Call>
void CallIntoJava(Call&& call) {
    JavaVM* vm = g_vm;
    if (!vm || !g_nativeClientClass) return;
    JNIEnv* env = nullptr;
    const jint state = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    const bool needsAttach = state == JNI_EDETACHED;
    if (needsAttach) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
    } else if (state != JNI_OK) {
        return;
    }
    call(env);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (needsAttach) vm->DetachCurrentThread();
}

void NotifySessionStatus(const char* statusUtf8, void*) {
    const jint phase =
        jint(dh_session_phase(g_callbackSession.load(std::memory_order_acquire)));
    CallIntoJava([statusUtf8, phase](JNIEnv* env) {
        jstring line = env->NewStringUTF(statusUtf8 ? statusUtf8 : "");
        env->CallStaticVoidMethod(g_nativeClientClass, g_onSessionStatus, line, phase);
        env->DeleteLocalRef(line);
    });
}

void NotifySessionSize(uint32_t width, uint32_t height, void*) {
    CallIntoJava([width, height](JNIEnv* env) {
        env->CallStaticVoidMethod(g_nativeClientClass, g_onSessionSize, jint(width),
            jint(height));
    });
}

void NotifySessionClosed(const char* reasonUtf8, void*) {
    CallIntoJava([reasonUtf8](JNIEnv* env) {
        jstring reason = env->NewStringUTF(reasonUtf8 ? reasonUtf8 : "");
        env->CallStaticVoidMethod(g_nativeClientClass, g_onSessionEnded, reason);
        env->DeleteLocalRef(reason);
    });
}

jstring ToJString(JNIEnv* env, const std::string& s) {
    return env->NewStringUTF(s.c_str());
}

std::string FromJString(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string out = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return out;
}

constexpr const char* kSourceClass = "com/deskhub/app/NativeClient$Source";
constexpr const char* kHotkeyClass = "com/deskhub/app/NativeClient$Hotkey";

jfloatArray NewFloatArray2(JNIEnv* env, jfloat a, jfloat b) {
    const jfloat values[2] = {a, b};
    jfloatArray arr = env->NewFloatArray(2);
    if (arr) env->SetFloatArrayRegion(arr, 0, 2, values);
    return arr;
}

void DropWindow() {
    if (!g_window) return;
    dh_session_set_layer(g_session, nullptr);
    ANativeWindow_release(g_window);
    g_window = nullptr;
}

}

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    jclass cls = env->FindClass("com/deskhub/app/NativeClient");
    if (!cls) return JNI_ERR;
    g_nativeClientClass = static_cast<jclass>(env->NewGlobalRef(cls));
    g_onSessionStatus =
        env->GetStaticMethodID(g_nativeClientClass, "onSessionStatus", "(Ljava/lang/String;I)V");
    g_onSessionSize = env->GetStaticMethodID(g_nativeClientClass, "onSessionSize", "(II)V");
    g_onSessionEnded =
        env->GetStaticMethodID(g_nativeClientClass, "onSessionEnded", "(Ljava/lang/String;)V");
    if (!g_onSessionStatus || !g_onSessionSize || !g_onSessionEnded) return JNI_ERR;
    g_vm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeString(JNIEnv* env, jobject, jint id) {
    return env->NewStringUTF(dh_string(DHStringId(id)));
}

JNIEXPORT jboolean JNICALL
Java_com_deskhub_app_NativeClient_nativeParseAddress(JNIEnv* env, jobject, jstring addrStr) {
    return dh_parse_address(FromJString(env, addrStr).c_str()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeCouldNotConnect(JNIEnv* env, jobject, jstring addrStr) {
    const std::string addr = FromJString(env, addrStr);
    char buf[320];
    dh_could_not_connect(addr.c_str(), buf, int(sizeof(buf)));
    return env->NewStringUTF(buf);
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeConnectingTo(JNIEnv* env, jobject, jstring addrStr) {
    const std::string addr = FromJString(env, addrStr);
    char buf[320];
    dh_connecting_to(addr.c_str(), buf, int(sizeof(buf)));
    return env->NewStringUTF(buf);
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeHostTitle(JNIEnv* env, jobject, jstring addrStr,
    jint width, jint height) {
    const std::string addr = FromJString(env, addrStr);
    char buf[320];
    dh_host_title(addr.c_str(), uint32_t(width < 0 ? 0 : width),
        uint32_t(height < 0 ? 0 : height), buf, int(sizeof(buf)));
    return env->NewStringUTF(buf);
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeZoomLabel(JNIEnv* env, jobject, jfloat zoom) {
    char buf[32];
    dh_zoom_label(zoom, buf, int(sizeof(buf)));
    return env->NewStringUTF(buf);
}

JNIEXPORT jboolean JNICALL
Java_com_deskhub_app_NativeClient_nativeIsZoomed(JNIEnv*, jobject, jfloat zoom) {
    return dh_is_zoomed(zoom) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jobjectArray JNICALL
Java_com_deskhub_app_NativeClient_nativeListSources(JNIEnv* env, jobject, jstring addrStr) {
    jclass cls = env->FindClass(kSourceClass);
    if (!cls) return nullptr;
    jmethodID ctor =
        env->GetMethodID(cls, "<init>", "(ILjava/lang/String;Ljava/lang/String;)V");
    if (!ctor) return nullptr;

    const std::string addr = FromJString(env, addrStr);
    DHSourceInfo sources[deskhub::kMaxSources];
    const int count = dh_list_sources(addr.c_str(), sources, int(deskhub::kMaxSources));

    jobjectArray arr = env->NewObjectArray(jsize(count), cls, nullptr);
    for (int i = 0; i < count && arr; ++i) {
        const DHSourceInfo& s = sources[i];
        jstring displayName = env->NewStringUTF(s.displayName);
        jstring sizeLabel = env->NewStringUTF(s.sizeLabel);
        jobject item = env->NewObject(cls, ctor, jint(s.sourceId), displayName, sizeLabel);
        env->SetObjectArrayElement(arr, jsize(i), item);
        env->DeleteLocalRef(item);
        env->DeleteLocalRef(sizeLabel);
        env->DeleteLocalRef(displayName);
    }
    return arr;
}

JNIEXPORT jobjectArray JNICALL
Java_com_deskhub_app_NativeClient_nativeHotkeys(JNIEnv* env, jobject) {
    jclass cls = env->FindClass(kHotkeyClass);
    if (!cls) return nullptr;
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(Ljava/lang/String;IIII)V");
    if (!ctor) return nullptr;

    DHHotkey table[deskhub::kMaxHotkeys];
    const int count = dh_hotkeys(table, deskhub::kMaxHotkeys);

    jobjectArray arr = env->NewObjectArray(jsize(count), cls, nullptr);
    for (int i = 0; i < count && arr; ++i) {
        const DHHotkey& h = table[i];
        jstring label = env->NewStringUTF(h.label);
        jobject item = env->NewObject(cls, ctor, label, jint(h.vk), jint(h.scan), jint(h.modVk),
            jint(h.modScan));
        env->SetObjectArrayElement(arr, jsize(i), item);
        env->DeleteLocalRef(item);
        env->DeleteLocalRef(label);
    }
    return arr;
}

JNIEXPORT jlong JNICALL
Java_com_deskhub_app_NativeClient_nativeStart(JNIEnv* env, jobject, jstring addrStr,
    jint sourceId, jint screenW, jint screenH) {
    const std::string addr = FromJString(env, addrStr);

    dh_session_set_screen_hint(screenW > 0 ? uint32_t(screenW) : 0,
        screenH > 0 ? uint32_t(screenH) : 0);

    dh_session_stop(g_session);
    g_callbackSession.store(nullptr, std::memory_order_release);
    g_session = nullptr;

    DHSessionCallbacks callbacks{};
    callbacks.onStatus = NotifySessionStatus;
    callbacks.onSize = NotifySessionSize;
    callbacks.onClosed = NotifySessionClosed;

    g_session = dh_session_start(addr.c_str(), uint8_t(sourceId), g_window, &callbacks, nullptr);
    g_callbackSession.store(g_session, std::memory_order_release);
    return jlong(reinterpret_cast<uintptr_t>(g_session));
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeStop(JNIEnv*, jobject, jlong handle) {
    if (!g_session) return;
    if (handle != 0 && reinterpret_cast<uintptr_t>(g_session) != uintptr_t(handle)) return;
    dh_session_stop(g_session);
    g_callbackSession.store(nullptr, std::memory_order_release);
    g_session = nullptr;
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeSetSurface(JNIEnv* env, jobject, jobject surface) {
    if (!surface) {
        DropWindow();
        return;
    }
    ANativeWindow* w = ANativeWindow_fromSurface(env, surface);
    if (g_window == w) {
        if (w) ANativeWindow_release(w);
    } else {
        DropWindow();
        g_window = w;
    }
    if (g_window) dh_session_set_layer(g_session, g_window);
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeReleaseSurface(JNIEnv* env, jobject, jobject surface) {
    if (!surface) return;
    ANativeWindow* w = ANativeWindow_fromSurface(env, surface);
    const bool ours = (w == g_window);
    if (w) ANativeWindow_release(w);
    if (ours) DropWindow();
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeKey(JNIEnv*, jobject, jint vk, jint scan,
    jboolean down) {
    dh_session_key(g_session, int32_t(vk), int32_t(scan), down == JNI_TRUE);
}

JNIEXPORT jint JNICALL
Java_com_deskhub_app_NativeClient_nativeVkScancode(JNIEnv*, jobject, jint vk) {
    return jint(dh_vk_scancode(int32_t(vk)));
}

JNIEXPORT jobject JNICALL
Java_com_deskhub_app_NativeClient_nativeSnapshot(JNIEnv* env, jobject) {
    jclass cls = env->FindClass("com/deskhub/app/NativeClient$Snapshot");
    if (!cls) return nullptr;
    jmethodID ctor =
        env->GetMethodID(cls, "<init>", "(ILjava/lang/String;Ljava/lang/String;II)V");
    if (!ctor) return nullptr;

    DHSessionState state{};
    dh_session_snapshot(g_session, &state);
    jstring statusLine = env->NewStringUTF(state.statusLine);
    jstring endReason = env->NewStringUTF(state.endReason);
    jobject out = env->NewObject(cls, ctor, jint(state.phase), statusLine, endReason,
        jint(state.videoWidth), jint(state.videoHeight));
    env->DeleteLocalRef(endReason);
    env->DeleteLocalRef(statusLine);
    return out;
}

JNIEXPORT jint JNICALL
Java_com_deskhub_app_NativeClient_nativeConnectDecision(JNIEnv* env, jobject, jintArray ids) {
    std::vector<DHSourceInfo> infos;
    if (ids) {
        const jsize n = env->GetArrayLength(ids);
        if (n > 0) {
            infos.resize(size_t(n));
            jint* raw = env->GetIntArrayElements(ids, nullptr);
            for (jsize i = 0; i < n; ++i) infos[size_t(i)].sourceId = uint8_t(raw[i]);
            env->ReleaseIntArrayElements(ids, raw, JNI_ABORT);
        }
    }
    uint8_t sourceId = 0;
    if (dh_connect_decision(infos.data(), int(infos.size()), &sourceId)) return -1;
    return jint(sourceId);
}

JNIEXPORT jint JNICALL
Java_com_deskhub_app_NativeClient_nativeKeyToVk(JNIEnv*, jobject, jint keyCode) {
    int32_t vk = 0, scan = 0;
    if (!dh_native_key_to_vk(int32_t(keyCode), &vk, &scan)) return 0;
    return jint(vk);
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeHotkey(JNIEnv*, jobject, jint vk, jint scan, jint modVk,
    jint modScan) {
    dh_session_hotkey(g_session, int32_t(vk), int32_t(scan), int32_t(modVk), int32_t(modScan));
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeCharTap(JNIEnv*, jobject, jint codepoint) {
    if (codepoint > 0) dh_session_char_tap(g_session, uint32_t(codepoint));
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeReleaseAllInput(JNIEnv*, jobject) {
    dh_session_release_all_input(g_session);
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeMouseMove(JNIEnv*, jobject, jint nx, jint ny) {
    dh_session_mouse_move(g_session, int32_t(nx), int32_t(ny));
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeMouseButton(JNIEnv*, jobject, jint button,
    jboolean down) {
    dh_session_mouse_button(g_session, int32_t(button), down == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeMouseWheel(JNIEnv*, jobject, jint notches) {
    dh_session_mouse_wheel_notches(g_session, int32_t(notches));
}

JNIEXPORT jfloatArray JNICALL
Java_com_deskhub_app_NativeClient_nativeVideoFrame(JNIEnv* env, jobject, jfloat viewportW,
    jfloat viewportH, jfloat aspect, jfloat zoom, jfloat panX, jfloat panY) {
    const deskhub::ViewRect r = deskhub::FitVideoRect(viewportW, viewportH, aspect,
        deskhub::ViewTransform{zoom, panX, panY});
    const jfloat out[4] = {jfloat(r.x), jfloat(r.y), jfloat(r.width), jfloat(r.height)};
    jfloatArray arr = env->NewFloatArray(4);
    if (arr) env->SetFloatArrayRegion(arr, 0, 4, out);
    return arr;
}

JNIEXPORT jint JNICALL
Java_com_deskhub_app_NativeClient_nativeTakeScrollNotches(JNIEnv* env, jobject,
    jfloat dragPoints, jdoubleArray carry) {
    if (!carry || env->GetArrayLength(carry) < 1) return 0;
    jdouble pending = 0;
    env->GetDoubleArrayRegion(carry, 0, 1, &pending);
    const jint notches = jint(dh_take_scroll_notches(dragPoints, &pending));
    env->SetDoubleArrayRegion(carry, 0, 1, &pending);
    return notches;
}

JNIEXPORT jfloatArray JNICALL
Java_com_deskhub_app_NativeClient_nativeCursorClamp(JNIEnv* env, jobject, jfloat cx, jfloat cy,
    jfloat rectX, jfloat rectY, jfloat rectW, jfloat rectH, jfloat viewportW,
    jfloat viewportH) {
    const DHCursor out = dh_cursor_clamp(DHCursor{cx, cy},
        DHViewRect{rectX, rectY, rectW, rectH}, viewportW, viewportH);
    return NewFloatArray2(env, jfloat(out.x), jfloat(out.y));
}

JNIEXPORT jfloatArray JNICALL
Java_com_deskhub_app_NativeClient_nativeCursorMove(JNIEnv* env, jobject, jfloat cx, jfloat cy,
    jfloat dx, jfloat dy, jfloat rectX, jfloat rectY, jfloat rectW, jfloat rectH,
    jfloat viewportW, jfloat viewportH) {
    const DHCursor out = dh_cursor_move(DHCursor{cx, cy}, dx, dy,
        DHViewRect{rectX, rectY, rectW, rectH}, viewportW, viewportH);
    return NewFloatArray2(env, jfloat(out.x), jfloat(out.y));
}

JNIEXPORT jfloatArray JNICALL
Java_com_deskhub_app_NativeClient_nativeCursorPoint(JNIEnv* env, jobject, jfloat cx, jfloat cy,
    jfloat rectX, jfloat rectY, jfloat rectW, jfloat rectH) {
    double px = 0, py = 0;
    if (!dh_cursor_point(DHCursor{cx, cy}, DHViewRect{rectX, rectY, rectW, rectH}, &px, &py))
        return env->NewFloatArray(0);
    return NewFloatArray2(env, jfloat(px), jfloat(py));
}

JNIEXPORT jintArray JNICALL
Java_com_deskhub_app_NativeClient_nativeCursorNormalize(JNIEnv* env, jobject, jfloat cx,
    jfloat cy, jfloat rectX, jfloat rectY, jfloat rectW, jfloat rectH) {
    int32_t nx = 0, ny = 0;
    if (!dh_cursor_normalize(DHCursor{cx, cy}, DHViewRect{rectX, rectY, rectW, rectH}, &nx, &ny))
        return env->NewIntArray(0);
    const jint out[2] = {jint(nx), jint(ny)};
    jintArray arr = env->NewIntArray(2);
    if (arr) env->SetIntArrayRegion(arr, 0, 2, out);
    return arr;
}

JNIEXPORT jfloatArray JNICALL
Java_com_deskhub_app_NativeClient_nativeApplyGesture(JNIEnv* env, jobject, jfloat zoom,
    jfloat panX, jfloat panY, jfloat factor, jfloat centroidX, jfloat centroidY,
    jfloat panDeltaX, jfloat panDeltaY, jfloat viewportW, jfloat viewportH, jfloat aspect) {
    const deskhub::ViewTransform t = deskhub::ApplyGesture(
        deskhub::ViewTransform{zoom, panX, panY}, factor, centroidX, centroidY, panDeltaX,
        panDeltaY, viewportW, viewportH, aspect);
    const jfloat out[3] = {jfloat(t.zoom), jfloat(t.panX), jfloat(t.panY)};
    jfloatArray arr = env->NewFloatArray(3);
    if (arr) env->SetFloatArrayRegion(arr, 0, 3, out);
    return arr;
}
}
