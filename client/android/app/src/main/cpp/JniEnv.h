#pragma once
#include <jni.h>

#include <string>

namespace deskhubj {

void RememberJavaVm(JavaVM* vm);

class AttachedEnv {
public:
    AttachedEnv();
    ~AttachedEnv();
    AttachedEnv(const AttachedEnv&) = delete;
    AttachedEnv& operator=(const AttachedEnv&) = delete;

    JNIEnv* env() const {
        return env_;
    }

    explicit operator bool() const {
        return env_ != nullptr;
    }

private:
    JNIEnv* env_ = nullptr;
    bool detachOnDestroy_ = false;
};

std::string FromJString(JNIEnv* env, jstring text);

}
