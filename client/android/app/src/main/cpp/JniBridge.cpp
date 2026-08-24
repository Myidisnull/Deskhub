#include <android/native_window_jni.h>
#include <jni.h>

#include <atomic>
#include <string>
#include <vector>

#include "ClientSessionAndroid.h"
#include "HostBridge.h"
#include "JniEnv.h"

#include "deskhub/input/Hotkeys.h"
#include "deskhub/media/ViewFit.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/diag/LogPolicy.h"
#include "deskhubp/diag/LogFile.h"
#include "deskhubp/ffi/ClientFfi.h"
#include "deskhubp/ffi/DiscoveryFfi.h"

static_assert(DHPhaseIdle == 0 && DHPhaseConnecting == 1 && DHPhaseStreaming == 2 &&
              DHPhaseEnded == 3);
static_assert(DHStrClientIpPrompt == 3 && DHStrQueryingSources == 12 &&
              DHStrInvalidAddressHint == 17 && DHStrSessionEnded == 18);
static_assert(int32_t(deskhub::MouseButton::Left) == 1 &&
              int32_t(deskhub::MouseButton::Right) == 2);

namespace {

DHSession* g_session = nullptr;
ANativeWindow* g_window = nullptr;

jclass g_nativeClientClass = nullptr;
jmethodID g_onSessionStatus = nullptr;
jmethodID g_onSessionSize = nullptr;
jmethodID g_onSessionEnded = nullptr;
std::atomic<DHSession*> g_callbackSession{nullptr};

template <class Call>
void CallIntoJava(Call&& call) {
    if (!g_nativeClientClass) return;
    deskhubj::AttachedEnv attached;
    if (!attached) return;
    call(attached.env());
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

using deskhubj::FromJString;

constexpr const char* kSourceClass = "com/deskhub/app/NativeClient$Source";
constexpr const char* kScanHitClass = "com/deskhub/app/NativeClient$ScanHit";
constexpr const char* kRecentDeviceClass = "com/deskhub/app/NativeClient$RecentDevice";
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
    deskhubj::RememberJavaVm(vm);
    if (!RegisterHostBridge(env)) return JNI_ERR;
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
Java_com_deskhub_app_NativeClient_nativeSourceQueryFailed(JNIEnv* env, jobject, jstring addrStr) {
    const std::string addr = FromJString(env, addrStr);
    char buf[320];
    dh_source_query_failed(addr.c_str(), buf, int(sizeof(buf)));
    return env->NewStringUTF(buf);
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeSourceQueryEmpty(JNIEnv* env, jobject, jstring addrStr) {
    const std::string addr = FromJString(env, addrStr);
    char buf[320];
    dh_source_query_empty(addr.c_str(), buf, int(sizeof(buf)));
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
Java_com_deskhub_app_NativeClient_nativeListSources(JNIEnv* env, jobject, jstring addrStr,
    jstring passcodeStr) {
    jclass cls = env->FindClass(kSourceClass);
    if (!cls) return nullptr;
    jmethodID ctor =
        env->GetMethodID(cls, "<init>", "(ILjava/lang/String;Ljava/lang/String;)V");
    if (!ctor) return nullptr;

    const std::string addr = FromJString(env, addrStr);
    const std::string passcode = FromJString(env, passcodeStr);
    DHSourceInfo sources[deskhub::kMaxSources];
    const int count =
        dh_list_sources(addr.c_str(), sources, int(deskhub::kMaxSources), passcode.c_str(),
            nullptr);
    if (count == DH_SOURCE_QUERY_FAILED) return nullptr;

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

JNIEXPORT jint JNICALL
Java_com_deskhub_app_NativeClient_nativeDefaultPort(JNIEnv*, jobject) {
    return jint(dh_default_port());
}

JNIEXPORT jboolean JNICALL
Java_com_deskhub_app_NativeClient_nativeClientControl(JNIEnv*, jobject) {
    return dh_client_control() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeSetClientControl(JNIEnv*, jobject, jboolean on) {
    dh_set_client_control(on == JNI_TRUE);
}

JNIEXPORT jboolean JNICALL
Java_com_deskhub_app_NativeClient_nativeClipboardSync(JNIEnv*, jobject) {
    return dh_clipboard_sync() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeSetClipboardSync(JNIEnv*, jobject, jboolean on) {
    dh_set_clipboard_sync(on == JNI_TRUE);
}

JNIEXPORT jboolean JNICALL
Java_com_deskhub_app_NativeClient_nativeShareAudio(JNIEnv*, jobject) {
    return dh_share_audio() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeSetShareAudio(JNIEnv*, jobject, jboolean on) {
    dh_set_share_audio(on == JNI_TRUE);
}

JNIEXPORT jboolean JNICALL
Java_com_deskhub_app_NativeClient_nativePlayAudio(JNIEnv*, jobject) {
    return dh_play_audio() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeSetPlayAudio(JNIEnv*, jobject, jboolean on) {
    dh_set_play_audio(on == JNI_TRUE);
}

JNIEXPORT jboolean JNICALL
Java_com_deskhub_app_NativeClient_nativeKeepAwake(JNIEnv*, jobject) {
    return dh_keep_awake() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeSetKeepAwake(JNIEnv*, jobject, jboolean on) {
    dh_set_keep_awake(on == JNI_TRUE);
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeLanguage(JNIEnv* env, jobject) {
    char buf[32];
    dh_language(buf, int(sizeof(buf)));
    return env->NewStringUTF(buf);
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeSetLanguage(JNIEnv* env, jobject, jstring codeStr) {
    dh_set_language(FromJString(env, codeStr).c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_deskhub_app_NativeClient_nativeEncryptSession(JNIEnv*, jobject) {
    return dh_encrypt_session() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeSetEncryptSession(JNIEnv*, jobject, jboolean on) {
    dh_set_encrypt_session(on == JNI_TRUE);
}

JNIEXPORT jboolean JNICALL
Java_com_deskhub_app_NativeClient_nativeEscrowSessionKey(JNIEnv*, jobject) {
    return dh_escrow_session_key() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeSetEscrowSessionKey(JNIEnv*, jobject, jboolean on) {
    dh_set_escrow_session_key(on == JNI_TRUE);
}

JNIEXPORT jint JNICALL
Java_com_deskhub_app_NativeClient_nativeSessionKeyLifetime(JNIEnv*, jobject) {
    return jint(dh_session_key_lifetime());
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeSetSessionKeyLifetime(JNIEnv*, jobject, jint lifetime) {
    dh_set_session_key_lifetime(int(lifetime));
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeSessionKeyHex(JNIEnv* env, jobject) {
    char buf[DH_SESSION_KEY_CAP];
    dh_session_key_hex(buf, int(sizeof(buf)));
    return env->NewStringUTF(buf);
}

JNIEXPORT jboolean JNICALL
Java_com_deskhub_app_NativeClient_nativeEnsureSessionKey(JNIEnv*, jobject, jboolean refresh) {
    return dh_ensure_session_key(refresh == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_deskhub_app_NativeClient_nativeIsValidSessionKey(JNIEnv* env, jobject, jstring hexStr) {
    return dh_is_valid_session_key(FromJString(env, hexStr).c_str()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeClipOffer(JNIEnv* env, jobject, jstring textStr) {
    dh_session_clip_offer(g_session, FromJString(env, textStr).c_str());
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeClipTake(JNIEnv* env, jobject) {
    char buf[deskhub::kMaxClipboardTextBytes + 1];
    dh_session_clip_take(g_session, buf, int(sizeof(buf)));
    return env->NewStringUTF(buf);
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeDeviceName(JNIEnv* env, jobject) {
    char buf[128];
    dh_device_name(buf, int(sizeof(buf)));
    return env->NewStringUTF(buf);
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeSetDeviceName(JNIEnv* env, jobject, jstring nameStr) {
    dh_set_device_name(FromJString(env, nameStr).c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_deskhub_app_NativeClient_nativeScanStart(JNIEnv*, jobject, jint port) {
    return dh_scan_start(uint16_t(port)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_deskhub_app_NativeClient_nativeScanRestart(JNIEnv*, jobject, jint port) {
    return dh_scan_restart(uint16_t(port)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_deskhub_app_NativeClient_nativeRescanSeconds(JNIEnv*, jobject) {
    return jint(dh_scan_rescan_secs());
}

JNIEXPORT jint JNICALL
Java_com_deskhub_app_NativeClient_nativeSettingsPort(JNIEnv*, jobject) {
    const DHUiSettings stored = dh_settings_load();
    const bool usable = stored.port >= 1 && stored.port <= 65535;
    return jint(usable ? stored.port : dh_default_port());
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeSetSettingsPort(JNIEnv*, jobject, jint port) {
    const DHUiSettings stored = dh_settings_load();
    dh_settings_save(stored.fps, stored.bitrateMbps, stored.maxDim, uint32_t(port),
        stored.allowInput, stored.clientControl, stored.runInBackground,
        stored.runInBackgroundChoiceMade, stored.hideTrayIcon, stored.shareOnLaunch,
        stored.logMaxFileMb, stored.logCompressAfterDays, stored.logDeleteAfterDays,
        stored.logDir, stored.passcode);
}

JNIEXPORT jint JNICALL
Java_com_deskhub_app_NativeClient_nativeLogMaxFileMb(JNIEnv*, jobject) {
    return jint(dh_settings_load().logMaxFileMb);
}

JNIEXPORT jint JNICALL
Java_com_deskhub_app_NativeClient_nativeLogCompressAfterDays(JNIEnv*, jobject) {
    return jint(dh_settings_load().logCompressAfterDays);
}

JNIEXPORT jint JNICALL
Java_com_deskhub_app_NativeClient_nativeLogDeleteAfterDays(JNIEnv*, jobject) {
    return jint(dh_settings_load().logDeleteAfterDays);
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeSetLogPolicy(JNIEnv*, jobject, jint maxFileMb,
    jint compressAfterDays, jint deleteAfterDays) {
    const DHUiSettings stored = dh_settings_load();
    const deskhub::diag::LogPolicy policy = deskhub::diag::ClampLogPolicy(deskhub::diag::LogPolicy{
        uint32_t(maxFileMb),
        uint32_t(compressAfterDays),
        uint32_t(deleteAfterDays),
    });
    dh_settings_save(stored.fps, stored.bitrateMbps, stored.maxDim, stored.port,
        stored.allowInput, stored.clientControl, stored.runInBackground,
        stored.runInBackgroundChoiceMade, stored.hideTrayIcon, stored.shareOnLaunch,
        policy.maxFileMb, policy.compressAfterDays, policy.deleteAfterDays, stored.logDir,
        stored.passcode);
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeScanCancel(JNIEnv*, jobject) {
    dh_scan_cancel();
}

JNIEXPORT jboolean JNICALL
Java_com_deskhub_app_NativeClient_nativeScanRunning(JNIEnv*, jobject) {
    return dh_scan_state().running ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeScanStatusText(JNIEnv* env, jobject, jint port) {
    char buf[256];
    dh_scan_status_text(uint16_t(port), buf, int(sizeof(buf)));
    return env->NewStringUTF(buf);
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeSetDataDir(JNIEnv* env, jobject, jstring dirStr) {
    deskhubp::SetAppDataDir(FromJString(env, dirStr));
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeVersionLine(JNIEnv* env, jobject) {
    char buf[64];
    dh_version_line(buf, int(sizeof(buf)));
    return env->NewStringUTF(buf);
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeUdpPortLine(JNIEnv* env, jobject, jint port) {
    char buf[64];
    dh_udp_port_line(uint32_t(port), buf, int(sizeof(buf)));
    return env->NewStringUTF(buf);
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeComposeAddress(JNIEnv* env, jobject, jstring hostStr,
    jstring portStr) {
    char buf[128];
    dh_compose_address(FromJString(env, hostStr).c_str(), FromJString(env, portStr).c_str(), buf,
        int(sizeof(buf)));
    return env->NewStringUTF(buf);
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeAddressHost(JNIEnv* env, jobject, jstring addrStr) {
    char buf[128];
    dh_address_host(FromJString(env, addrStr).c_str(), buf, int(sizeof(buf)));
    return env->NewStringUTF(buf);
}

JNIEXPORT jint JNICALL
Java_com_deskhub_app_NativeClient_nativeAddressPort(JNIEnv* env, jobject, jstring addrStr) {
    return jint(dh_address_port(FromJString(env, addrStr).c_str()));
}

JNIEXPORT jobjectArray JNICALL
Java_com_deskhub_app_NativeClient_nativeScanHits(JNIEnv* env, jobject) {
    jclass cls = env->FindClass(kScanHitClass);
    if (!cls) return nullptr;
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(Ljava/lang/String;Ljava/lang/String;)V");
    if (!ctor) return nullptr;

    DHScanHit hits[64];
    const int count = dh_scan_hits(hits, int(sizeof(hits) / sizeof(hits[0])));

    jobjectArray arr = env->NewObjectArray(jsize(count), cls, nullptr);
    for (int i = 0; i < count && arr; ++i) {
        char ping[32];
        dh_ping_text(hits[i].rttMs, ping, int(sizeof(ping)));
        jstring addr = env->NewStringUTF(hits[i].addr);
        jstring pingText = env->NewStringUTF(ping);
        jobject item = env->NewObject(cls, ctor, addr, pingText);
        env->SetObjectArrayElement(arr, jsize(i), item);
        env->DeleteLocalRef(item);
        env->DeleteLocalRef(pingText);
        env->DeleteLocalRef(addr);
    }
    return arr;
}

JNIEXPORT jobjectArray JNICALL
Java_com_deskhub_app_NativeClient_nativeRecentDevices(JNIEnv* env, jobject) {
    jclass cls = env->FindClass(kRecentDeviceClass);
    if (!cls) return nullptr;
    jmethodID ctor = env->GetMethodID(cls, "<init>",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
        "Ljava/lang/String;Z)V");
    if (!ctor) return nullptr;

    DHRecentRow rows[16];
    const int count = dh_recent_rows(rows, int(sizeof(rows) / sizeof(rows[0])));

    jobjectArray arr = env->NewObjectArray(jsize(count), cls, nullptr);
    for (int i = 0; i < count && arr; ++i) {
        jstring addr = env->NewStringUTF(rows[i].addr);
        jstring passcode = env->NewStringUTF(rows[i].passcode);
        jstring status = env->NewStringUTF(rows[i].status);
        jstring ping = env->NewStringUTF(rows[i].ping);
        jstring last = env->NewStringUTF(rows[i].lastConnected);
        jobject item = env->NewObject(cls, ctor, addr, passcode, status, ping, last,
            rows[i].online ? JNI_TRUE : JNI_FALSE);
        env->SetObjectArrayElement(arr, jsize(i), item);
        env->DeleteLocalRef(item);
        env->DeleteLocalRef(last);
        env->DeleteLocalRef(ping);
        env->DeleteLocalRef(status);
        env->DeleteLocalRef(passcode);
        env->DeleteLocalRef(addr);
    }
    return arr;
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeRecentTouch(JNIEnv* env, jobject, jstring addrStr,
    jstring passcodeStr) {
    const std::string addr = FromJString(env, addrStr);
    const std::string passcode = FromJString(env, passcodeStr);
    dh_recent_touch(addr.c_str(), passcode.c_str());
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeRecentTouchEx(JNIEnv* env, jobject, jstring addrStr,
    jstring passcodeStr, jboolean encrypted, jstring sessionKeyStr) {
    const std::string addr = FromJString(env, addrStr);
    const std::string passcode = FromJString(env, passcodeStr);
    const std::string sessionKey = FromJString(env, sessionKeyStr);
    dh_recent_touch_ex(addr.c_str(), passcode.c_str(), encrypted == JNI_TRUE,
        sessionKey.empty() ? nullptr : sessionKey.c_str());
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeWatchRecent(JNIEnv*, jobject) {
    dh_status_watch_recent();
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeStatusRefreshNow(JNIEnv*, jobject) {
    dh_status_refresh_now();
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeRecentNote(JNIEnv* env, jobject) {
    char buf[256];
    dh_recent_note(buf, int(sizeof(buf)));
    return env->NewStringUTF(buf);
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeRecentPasscode(JNIEnv* env, jobject, jstring addrStr) {
    const std::string addr = FromJString(env, addrStr);
    char buf[16];
    dh_recent_passcode(addr.c_str(), buf, int(sizeof(buf)));
    return env->NewStringUTF(buf);
}

JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeClient_nativeRecentSessionKey(JNIEnv* env, jobject, jstring addrStr) {
    char buf[DH_SESSION_KEY_CAP];
    dh_recent_session_key(FromJString(env, addrStr).c_str(), buf, int(sizeof(buf)));
    return env->NewStringUTF(buf);
}

JNIEXPORT jboolean JNICALL
Java_com_deskhub_app_NativeClient_nativeRecentEncrypted(JNIEnv* env, jobject, jstring addrStr) {
    return dh_recent_encrypted(FromJString(env, addrStr).c_str()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeRecentRemove(JNIEnv* env, jobject, jstring addrStr) {
    dh_recent_remove(FromJString(env, addrStr).c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_deskhub_app_NativeClient_nativeIsValidPasscode(JNIEnv* env, jobject,
    jstring passcodeStr) {
    const std::string passcode = FromJString(env, passcodeStr);
    return dh_is_valid_passcode(passcode.c_str()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_deskhub_app_NativeClient_nativePasscodeDigits(JNIEnv*, jobject) {
    return jint(dh_passcode_digits());
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
    jint sourceId, jint screenW, jint screenH, jstring passcodeStr, jstring sessionKeyStr) {
    const std::string addr = FromJString(env, addrStr);
    const std::string passcode = FromJString(env, passcodeStr);
    const std::string sessionKey = FromJString(env, sessionKeyStr);

    dh_session_set_screen_hint(screenW > 0 ? uint32_t(screenW) : 0,
        screenH > 0 ? uint32_t(screenH) : 0);

    dh_session_stop(g_session);
    g_callbackSession.store(nullptr, std::memory_order_release);
    g_session = nullptr;

    DHSessionCallbacks callbacks{};
    callbacks.onStatus = NotifySessionStatus;
    callbacks.onSize = NotifySessionSize;
    callbacks.onClosed = NotifySessionClosed;

    g_session = dh_session_start(addr.c_str(), uint8_t(sourceId), g_window, &callbacks,
        passcode.c_str(), sessionKey.empty() ? nullptr : sessionKey.c_str());
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
