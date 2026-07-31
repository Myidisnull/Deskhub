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

run-android:
	$(GRADLEW) installDebug
	"$(ADB)" shell am start -n $(ANDROID_ACTIVITY)

.PHONY: build-android release-android run-android
