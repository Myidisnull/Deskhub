#include "JniEnv.h"

namespace deskhubj {
namespace {

JavaVM* g_vm = nullptr;

}

void RememberJavaVm(JavaVM* vm) {
    g_vm = vm;
}

AttachedEnv::AttachedEnv() {
    if (!g_vm) return;
    JNIEnv* env = nullptr;
    const jint state = g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (state == JNI_OK) {
        env_ = env;
        return;
    }
    if (state != JNI_EDETACHED) return;
    if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
    env_ = env;
    detachOnDestroy_ = true;
}

AttachedEnv::~AttachedEnv() {
    if (!env_) return;
    if (env_->ExceptionCheck()) env_->ExceptionClear();
    if (detachOnDestroy_ && g_vm) g_vm->DetachCurrentThread();
}

std::string FromJString(JNIEnv* env, jstring text) {
    if (!env || !text) return {};
    const char* chars = env->GetStringUTFChars(text, nullptr);
    std::string out = chars ? chars : "";
    if (chars) env->ReleaseStringUTFChars(text, chars);
    return out;
}

}
