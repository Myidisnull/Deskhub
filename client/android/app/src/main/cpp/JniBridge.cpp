#include <android/native_window_jni.h>
#include <jni.h>

#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "ClientLoop.h"

#include "deskhub/input/Hotkeys.h"
#include "deskhub/media/ViewFit.h"
#include "deskhub/protocol/Wire.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/ffi/ClientFfi.h"
#include "deskhubp/net/UdpSocket.h"

namespace {

std::unique_ptr<ClientLoop> g_client;
ANativeWindow* g_window = nullptr;

uint64_t g_generation = 0;

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

}

extern "C" {

JNIEXPORT jobjectArray JNICALL
Java_com_deskhub_app_NativeClient_nativeListSources(JNIEnv* env, jobject, jstring addrStr) {
    jclass stringClass = env->FindClass("java/lang/String");

    const std::string addr = FromJString(env, addrStr);
    DHSourceInfo sources[deskhub::kMaxSources];
    const int count = dh_list_sources(addr.c_str(), sources, int(deskhub::kMaxSources));

    jobjectArray arr = env->NewObjectArray(jsize(count), stringClass, nullptr);
    for (int i = 0; i < count; ++i) {
        const DHSourceInfo& s = sources[i];
        char row[320];
        std::snprintf(row, sizeof(row), "%u\t%u\t%u\t%s", s.sourceId, s.width, s.height, s.name);
        env->SetObjectArrayElement(arr, jsize(i), ToJString(env, row));
    }
    return arr;
}

JNIEXPORT jobjectArray JNICALL
Java_com_deskhub_app_NativeClient_nativeHotkeys(JNIEnv* env, jobject) {
    jclass stringClass = env->FindClass("java/lang/String");

    const std::span<const deskhub::Hotkey> table = deskhub::TouchHotkeys();
    jobjectArray arr = env->NewObjectArray(jsize(table.size()), stringClass, nullptr);
    for (size_t i = 0; i < table.size(); ++i) {
        const deskhub::Hotkey& h = table[i];
        char row[96];
        std::snprintf(row, sizeof(row), "%s\t%d\t%d\t%d\t%d", h.label, h.vk, h.scan, h.modVk,
            h.modScan);
        env->SetObjectArrayElement(arr, jsize(i), ToJString(env, row));
    }
    return arr;
}

JNIEXPORT jlong JNICALL
Java_com_deskhub_app_NativeClient_nativeStart(JNIEnv* env, jobject, jstring addrStr,
    jint sourceId, jint screenW, jint screenH) {
    const std::string addr = FromJString(env, addrStr);

    NetAddr server;
    if (!ParseNetAddr(addr, server)) {
        LOGE("[JNI] Invalid host address: \"%s\"", addr.c_str());
        return 0;
    }

    const uint32_t sw = screenW > 0 ? uint32_t(screenW) : 0;
    const uint32_t sh = screenH > 0 ? uint32_t(screenH) : 0;

    g_client = std::make_unique<ClientLoop>();
    if (!g_client->Start(server, uint8_t(sourceId), sw, sh)) {
        g_client.reset();
        return 0;
    }
    if (g_window) g_client->SetWindow(g_window);
    return jlong(++g_generation);
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeStop(JNIEnv*, jobject, jlong generation) {
    if (!g_client) return;
    if (generation != 0 && uint64_t(generation) != g_generation) return;
    g_client->Stop();
    g_client.reset();
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeSetSurface(JNIEnv* env, jobject, jobject surface) {
    if (surface) {
        ANativeWindow* w = ANativeWindow_fromSurface(env, surface);
        if (g_window == w) {
            if (w) ANativeWindow_release(w);
        } else {
            if (g_window) {
                if (g_client) g_client->SetWindow(nullptr);
                ANativeWindow_release(g_window);
            }
            g_window = w;
        }
        if (g_client && g_window) g_client->SetWindow(g_window);
    } else {
        if (g_client) g_client->SetWindow(nullptr);
        if (g_window) {
            ANativeWindow_release(g_window);
            g_window = nullptr;
        }
    }
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeReleaseSurface(JNIEnv* env, jobject, jobject surface) {
    if (!surface) return;
    ANativeWindow* w = ANativeWindow_fromSurface(env, surface);
    const bool ours = (w == g_window);
    if (w) ANativeWindow_release(w);
    if (!ours) return;
    if (g_client) g_client->SetWindow(nullptr);
    ANativeWindow_release(g_window);
    g_window = nullptr;
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeKeyTap(JNIEnv*, jobject, jint vk, jint scan) {
    if (g_client) g_client->QueueKeyTap(int32_t(vk), int32_t(scan));
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeMouseMove(JNIEnv*, jobject, jint nx, jint ny) {
    if (g_client) g_client->QueueMouseMoveAbs(int32_t(nx), int32_t(ny));
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeKeyChord(JNIEnv*, jobject, jint modVk, jint modScan,
    jint vk, jint scan) {
    if (g_client)
        g_client->QueueKeyChord(int32_t(modVk), int32_t(modScan), int32_t(vk), int32_t(scan));
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeMouseMoveRel(JNIEnv*, jobject, jint dx, jint dy) {
    if (g_client) g_client->QueueMouseMoveRel(int32_t(dx), int32_t(dy));
}

JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeClient_nativeMouseButton(JNIEnv*, jobject, jint button,
    jboolean down) {
    if (g_client) g_client->QueueMouseButton(int32_t(button), down == JNI_TRUE);
}

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
