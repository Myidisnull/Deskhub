#include "HostBridge.h"

#include <android/native_window_jni.h>

#include <string>
#include <vector>

#include "JniEnv.h"
#include "capture/ScreenCapture.h"

#include "deskhub/protocol/Wire.h"
#include "deskhubp/ffi/AgentSession.h"
#include "deskhubp/ffi/DiscoveryFfi.h"
#include "deskhubp/media/DisplayEnum.h"

namespace {

constexpr const char* kNativeHostClass = "com/deskhub/app/NativeHost";
constexpr const char* kHostRowClass = "com/deskhub/app/NativeHost$HostRow";
constexpr int kMaxHostRows = 64;

jclass g_nativeHostClass = nullptr;
jmethodID g_projectionReady = nullptr;
jmethodID g_attachSurface = nullptr;
jmethodID g_detachSurface = nullptr;

jstring NewString(JNIEnv* env, const char* text) {
    return env->NewStringUTF(text ? text : "");
}

}

bool RegisterHostBridge(JNIEnv* env) {
    jclass cls = env->FindClass(kNativeHostClass);
    if (!cls) return false;
    g_nativeHostClass = static_cast<jclass>(env->NewGlobalRef(cls));
    g_projectionReady = env->GetStaticMethodID(g_nativeHostClass, "projectionReady", "()Z");
    g_attachSurface =
        env->GetStaticMethodID(g_nativeHostClass, "attachSurface", "(Landroid/view/Surface;II)Z");
    g_detachSurface = env->GetStaticMethodID(g_nativeHostClass, "detachSurface", "()V");
    return g_projectionReady && g_attachSurface && g_detachSurface;
}

namespace deskhubj {

bool HostProjectionReady() {
    if (!g_nativeHostClass) return false;
    AttachedEnv attached;
    if (!attached) return false;
    return attached.env()->CallStaticBooleanMethod(g_nativeHostClass, g_projectionReady) ==
           JNI_TRUE;
}

bool AttachHostSurface(ANativeWindow* window, uint32_t width, uint32_t height) {
    if (!g_nativeHostClass || !window) return false;
    AttachedEnv attached;
    if (!attached) return false;
    JNIEnv* env = attached.env();

    jobject surface = ANativeWindow_toSurface(env, window);
    if (!surface) return false;

    const jboolean ok = env->CallStaticBooleanMethod(g_nativeHostClass, g_attachSurface, surface,
        jint(width), jint(height));
    env->DeleteLocalRef(surface);
    return ok == JNI_TRUE;
}

void DetachHostSurface() {
    if (!g_nativeHostClass) return;
    AttachedEnv attached;
    if (!attached) return;
    attached.env()->CallStaticVoidMethod(g_nativeHostClass, g_detachSurface);
}

}

extern "C" {

JNIEXPORT void JNICALL Java_com_deskhub_app_NativeHost_nativeSetScreenSize(JNIEnv* env, jobject,
    jint width, jint height, jstring name) {
    deskhubp::SetLocalDisplay(uint32_t(width), uint32_t(height),
        deskhubj::FromJString(env, name));
}

JNIEXPORT jboolean JNICALL Java_com_deskhub_app_NativeHost_nativeStart(JNIEnv* env, jobject,
    jint fps, jint bitrateMbps, jint maxDim, jint port, jstring passcode) {
    DHShareSource source{};
    if (dha_list_share_sources(&source, 1) != 1) return JNI_FALSE;

    const std::string code = deskhubj::FromJString(env, passcode);
    return dha_start(&source, 1, uint32_t(fps), uint32_t(bitrateMbps), uint32_t(maxDim),
               uint16_t(port), false, code.c_str())
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_deskhub_app_NativeHost_nativeStop(JNIEnv*, jobject) {
    dha_stop();
}

JNIEXPORT jboolean JNICALL Java_com_deskhub_app_NativeHost_nativeRunning(JNIEnv*, jobject) {
    return dha_running() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_com_deskhub_app_NativeHost_nativeLastError(JNIEnv* env, jobject) {
    return NewString(env, dha_last_error());
}

JNIEXPORT jstring JNICALL Java_com_deskhub_app_NativeHost_nativeLocalAddresses(JNIEnv* env,
    jobject) {
    return NewString(env, dha_local_addresses());
}

JNIEXPORT void JNICALL Java_com_deskhub_app_NativeHost_nativeKickViewer(JNIEnv* env, jobject,
    jint sourceId, jstring viewerAddr) {
    dha_kick_viewer(uint8_t(sourceId), deskhubj::FromJString(env, viewerAddr).c_str());
}

JNIEXPORT void JNICALL Java_com_deskhub_app_NativeHost_nativeProjectionStopped(JNIEnv*, jobject) {
    ScreenCapture::ReportProjectionStopped();
}

JNIEXPORT void JNICALL Java_com_deskhub_app_NativeHost_nativeDisplayResized(JNIEnv*, jobject,
    jint width, jint height) {
    ScreenCapture::ReportDisplaySize(uint32_t(width), uint32_t(height));
}

JNIEXPORT jobjectArray JNICALL Java_com_deskhub_app_NativeHost_nativeHostRows(JNIEnv* env,
    jobject) {
    std::vector<DHHostRow> rows(kMaxHostRows);
    const int count = dha_host_rows(rows.data(), kMaxHostRows);

    jclass cls = env->FindClass(kHostRowClass);
    if (!cls) return nullptr;
    jmethodID ctor = env->GetMethodID(cls, "<init>",
        "(ZIZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
        "Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
        "Ljava/lang/String;)V");
    if (!ctor) return nullptr;

    jobjectArray out = env->NewObjectArray(count, cls, nullptr);
    if (!out) return nullptr;

    for (int i = 0; i < count; ++i) {
        const DHHostRow& row = rows[size_t(i)];
        jstring viewerAddr = NewString(env, row.viewerAddr);
        jstring source = NewString(env, row.source);
        jstring size = NewString(env, row.size);
        jstring viewers = NewString(env, row.viewers);
        jstring client = NewString(env, row.client);
        jstring capture = NewString(env, row.capture);
        jstring send = NewString(env, row.send);
        jstring mbps = NewString(env, row.mbps);
        jstring rtt = NewString(env, row.rtt);

        jobject item = env->NewObject(cls, ctor, row.viewer ? JNI_TRUE : JNI_FALSE,
            jint(row.sourceId), row.online ? JNI_TRUE : JNI_FALSE, viewerAddr, source, size,
            viewers, client, capture, send, mbps, rtt);
        env->SetObjectArrayElement(out, i, item);

        env->DeleteLocalRef(item);
        env->DeleteLocalRef(viewerAddr);
        env->DeleteLocalRef(source);
        env->DeleteLocalRef(size);
        env->DeleteLocalRef(viewers);
        env->DeleteLocalRef(client);
        env->DeleteLocalRef(capture);
        env->DeleteLocalRef(send);
        env->DeleteLocalRef(mbps);
        env->DeleteLocalRef(rtt);
    }
    return out;
}

JNIEXPORT jstring JNICALL Java_com_deskhub_app_NativeHost_nativeSharingStatus(JNIEnv* env, jobject,
    jint port, jstring passcode) {
    char buf[320];
    const std::string code = deskhubj::FromJString(env, passcode);
    dh_sharing_status(uint16_t(port), code.c_str(), false, buf, int(sizeof(buf)));
    return NewString(env, buf);
}

JNIEXPORT jstring JNICALL Java_com_deskhub_app_NativeHost_nativeIdleStatus(JNIEnv* env, jobject,
    jint port) {
    char buf[160];
    dh_idle_host_status(uint16_t(port), buf, int(sizeof(buf)));
    return NewString(env, buf);
}

JNIEXPORT jstring JNICALL Java_com_deskhub_app_NativeHost_nativePasscode(JNIEnv* env, jobject) {
    const DHUiSettings settings = dh_settings_load();
    return NewString(env, settings.passcode);
}

JNIEXPORT void JNICALL Java_com_deskhub_app_NativeHost_nativeSavePasscode(JNIEnv* env, jobject,
    jstring passcode) {
    const DHUiSettings settings = dh_settings_load();
    const std::string code = deskhubj::FromJString(env, passcode);
    dh_settings_save(settings.fps, settings.bitrateMbps, settings.maxDim, settings.port,
        settings.allowInput, settings.clientControl, settings.runInBackground,
        settings.runInBackgroundChoiceMade, settings.hideTrayIcon, settings.shareOnLaunch,
        settings.logMaxFileMb, settings.logCompressAfterDays, settings.logDeleteAfterDays,
        settings.logDir, code.c_str());
}

JNIEXPORT jstring JNICALL Java_com_deskhub_app_NativeHost_nativeBindIp(JNIEnv* env, jobject) {
    char buf[64];
    dh_bind_ip(buf, int(sizeof(buf)));
    return NewString(env, buf);
}

JNIEXPORT void JNICALL Java_com_deskhub_app_NativeHost_nativeSetBindIp(JNIEnv* env, jobject,
    jstring ip) {
    dh_set_bind_ip(deskhubj::FromJString(env, ip).c_str());
}

JNIEXPORT void JNICALL Java_com_deskhub_app_NativeHost_nativeClipOffer(JNIEnv* env, jobject,
    jstring text) {
    dha_clip_offer(deskhubj::FromJString(env, text).c_str());
}

JNIEXPORT jstring JNICALL Java_com_deskhub_app_NativeHost_nativeClipTake(JNIEnv* env, jobject) {
    char buf[deskhub::kMaxClipboardTextBytes + 1];
    dha_clip_take(buf, int(sizeof(buf)));
    return NewString(env, buf);
}

JNIEXPORT jintArray JNICALL Java_com_deskhub_app_NativeHost_nativeShareDefaults(JNIEnv* env,
    jobject) {
    const DHUiSettings stored = dh_settings_load();
    const DHShareDefaults defaults = dha_default_options();
    const jint values[3] = {
        jint(stored.fps ? stored.fps : defaults.fps),
        jint(stored.bitrateMbps ? stored.bitrateMbps : defaults.bitrateMbps),
        jint(stored.maxDim ? stored.maxDim : defaults.maxDim),
    };
    jintArray out = env->NewIntArray(3);
    if (out) env->SetIntArrayRegion(out, 0, 3, values);
    return out;
}
}
