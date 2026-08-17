ANDROID_NDK_VERSION ?= 26.1.10909125
ANDROID_QUICHE_TARGETS := aarch64-linux-android x86_64-linux-android

ifeq ($(OS),Windows_NT)
GRADLEW := cd client\android && .\gradlew.bat
ANDROID_SDK := $(if $(ANDROID_HOME),$(ANDROID_HOME),$(LOCALAPPDATA)\Android\Sdk)
ADB     := $(ANDROID_SDK)\platform-tools\adb.exe
ANDROID_NDK_ENV := set "ANDROID_NDK_HOME=$(ANDROID_SDK)\ndk\$(ANDROID_NDK_VERSION)" &&
else
GRADLEW := cd client/android && ./gradlew
ANDROID_SDK := $(if $(ANDROID_HOME),$(ANDROID_HOME),$(firstword $(wildcard $(HOME)/Android/Sdk $(HOME)/Library/Android/sdk)))
ADB     := $(if $(ANDROID_SDK),$(ANDROID_SDK)/platform-tools/adb,adb)
ANDROID_NDK_ENV := ANDROID_NDK_HOME=$(ANDROID_SDK)/ndk/$(ANDROID_NDK_VERSION)
endif

ANDROID_ACTIVITY := com.manhpham.deskhub/com.deskhub.app.MainActivity

quiche-android:
	-@$(ANDROID_NDK_ENV) $(QUICHE_FOR) $(ANDROID_QUICHE_TARGETS)

build-android: quiche-android
	$(GRADLEW) assembleDebug

release-android: quiche-android
	$(GRADLEW) assembleRelease

run-android: quiche-android
	$(GRADLEW) installDebug
	"$(ADB)" shell am start -n $(ANDROID_ACTIVITY)

.PHONY: quiche-android build-android release-android run-android
