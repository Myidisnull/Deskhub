# make/android.mk — APK Android: build-android / release-android / run-android.
#
# Gradle tự dựng CẢ libdeskhub.so (NDK + core) lẫn APK — không đi qua cây CMake desktop.
# Build chỉ cần SDK, không cần máy/emulator, nên chạy được trên cả 3 OS.
# release-android ra app-release-unsigned.apk: dự án CHƯA khai signingConfig, khi phát
# hành thật thì ký bằng apksigner hoặc thêm signingConfig vào gradle.

ifeq ($(OS),Windows_NT)
GRADLEW := cd client\android && .\gradlew.bat
ADB     := $(if $(ANDROID_HOME),$(ANDROID_HOME)\platform-tools\adb.exe,$(LOCALAPPDATA)\Android\Sdk\platform-tools\adb.exe)
else
GRADLEW := cd client/android && ./gradlew
ADB     := $(if $(ANDROID_HOME),$(ANDROID_HOME)/platform-tools/adb,adb)
endif

ANDROID_ACTIVITY := com.manhpham.deskhub/com.deskhub.app.MainActivity

build-android:
	$(GRADLEW) assembleDebug

release-android:
	$(GRADLEW) assembleRelease

# Cài qua adb rồi mở app. Cần máy thật/emulator đang hiện trong `adb devices`.
run-android:
	$(GRADLEW) installDebug
	"$(ADB)" shell am start -n $(ANDROID_ACTIVITY)

.PHONY: build-android release-android run-android
