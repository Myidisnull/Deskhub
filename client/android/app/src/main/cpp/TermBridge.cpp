#include <jni.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "JniEnv.h"
#include "deskhubp/ffi/TerminalFfi.h"

namespace {

constexpr int kSnapshotHeader = 10;
constexpr int kCellFields = 8;

jintArray PackSnapshot(JNIEnv* env, DHTermSession* session, uint32_t scrollOffset) {
    if (session == nullptr) return nullptr;

    DHTermGrid grid{};
    if (!dh_term_grid(session, scrollOffset, nullptr, 0, &grid)) return nullptr;

    const uint32_t cellCount = uint32_t(grid.rows) * uint32_t(grid.cols);
    std::vector<DHTermCell> cells(size_t(cellCount));
    if (cellCount > 0 &&
        !dh_term_grid(session, scrollOffset, cells.data(), cellCount, &grid)) {
        return nullptr;
    }

    const jsize total = kSnapshotHeader + jsize(cellCount) * kCellFields;
    jintArray arr = env->NewIntArray(total);
    if (arr == nullptr) return nullptr;

    jint header[kSnapshotHeader] = {
        jint(grid.rows),
        jint(grid.cols),
        jint(grid.cursorRow),
        jint(grid.cursorCol),
        grid.cursorVisible ? 1 : 0,
        jint(grid.scrollbackRows),
        jint(grid.scrollOffset),
        jint(grid.revision >> 32),
        jint(grid.revision & 0xffffffffu),
        jint(cellCount),
    };
    env->SetIntArrayRegion(arr, 0, kSnapshotHeader, header);

    if (cellCount == 0) return arr;

    std::vector<jint> packed(size_t(cellCount) * size_t(kCellFields));
    for (uint32_t i = 0; i < cellCount; ++i) {
        const DHTermCell& cell = cells[size_t(i)];
        const size_t base = size_t(i) * size_t(kCellFields);
        packed[base + 0] = jint(cell.codepoint);
        packed[base + 1] = jint(cell.fgR);
        packed[base + 2] = jint(cell.fgG);
        packed[base + 3] = jint(cell.fgB);
        packed[base + 4] = jint(cell.bgR);
        packed[base + 5] = jint(cell.bgG);
        packed[base + 6] = jint(cell.bgB);
        packed[base + 7] = jint(cell.attrs);
    }
    env->SetIntArrayRegion(arr, kSnapshotHeader, jsize(packed.size()), packed.data());
    return arr;
}

jstring CopyMessage(JNIEnv* env, DHTermSession* session) {
    if (session == nullptr) return env->NewStringUTF("");
    char buf[512];
    const int n = dh_term_message(session, buf, int(sizeof(buf)));
    if (n <= 0) return env->NewStringUTF("");
    const size_t take = std::min(size_t(n), sizeof(buf) - 1);
    buf[take] = '\0';
    return env->NewStringUTF(buf);
}

}

extern "C" JNIEXPORT jlong JNICALL
Java_com_deskhub_app_NativeTerm_nativeOpen(JNIEnv* env, jobject, jstring addrStr, jstring passStr,
    jint cols, jint rows) {
    const std::string addr = deskhubj::FromJString(env, addrStr);
    const std::string pass = deskhubj::FromJString(env, passStr);
    DHTermCallbacks callbacks{};
    DHTermSession* session =
        dh_term_open(addr.c_str(), pass.c_str(), uint16_t(std::max(1, cols)),
            uint16_t(std::max(1, rows)), &callbacks);
    return reinterpret_cast<jlong>(session);
}

extern "C" JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeTerm_nativeStop(JNIEnv*, jobject, jlong handle) {
    auto* session = reinterpret_cast<DHTermSession*>(handle);
    if (session != nullptr) dh_term_stop(session);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_deskhub_app_NativeTerm_nativeState(JNIEnv*, jobject, jlong handle) {
    return jint(dh_term_state(reinterpret_cast<DHTermSession*>(handle)));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_deskhub_app_NativeTerm_nativeMessage(JNIEnv* env, jobject, jlong handle) {
    return CopyMessage(env, reinterpret_cast<DHTermSession*>(handle));
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_deskhub_app_NativeTerm_nativeSnapshot(JNIEnv* env, jobject, jlong handle,
    jint scrollOffset) {
    return PackSnapshot(env, reinterpret_cast<DHTermSession*>(handle),
        uint32_t(std::max(0, scrollOffset)));
}

extern "C" JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeTerm_nativeSendKey(JNIEnv*, jobject, jlong handle, jint key,
    jint codepoint, jboolean shift, jboolean alt, jboolean ctrl) {
    dh_term_send_key(reinterpret_cast<DHTermSession*>(handle), key, uint32_t(codepoint),
        shift == JNI_TRUE, alt == JNI_TRUE, ctrl == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeTerm_nativeSendText(JNIEnv* env, jobject, jlong handle,
    jstring textStr) {
    const std::string text = deskhubj::FromJString(env, textStr);
    if (!text.empty())
        dh_term_send_text(reinterpret_cast<DHTermSession*>(handle), text.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_deskhub_app_NativeTerm_nativeResize(JNIEnv*, jobject, jlong handle, jint cols, jint rows) {
    dh_term_resize(reinterpret_cast<DHTermSession*>(handle), uint16_t(std::max(1, cols)),
        uint16_t(std::max(1, rows)));
}
