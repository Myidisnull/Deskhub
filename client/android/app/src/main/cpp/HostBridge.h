#pragma once
#include <android/native_window.h>
#include <jni.h>

#include <cstdint>

bool RegisterHostBridge(JNIEnv* env);

namespace deskhubj {

bool HostProjectionReady();

bool AttachHostSurface(ANativeWindow* window, uint32_t width, uint32_t height);

void DetachHostSurface();

}
