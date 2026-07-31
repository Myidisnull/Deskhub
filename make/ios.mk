ifeq ($(UNAME),Darwin)
IOS_PROJ   := client/ios/Deskhub.xcodeproj
IOS_OUT    := $(CURDIR)/out/build/ios
IOS_BUNDLE := com.ios.deskhub

build-ios:
	xcodebuild -project $(IOS_PROJ) -target app -configuration Debug -sdk iphonesimulator SYMROOT=$(IOS_OUT) build

release-ios:
	xcodebuild -project $(IOS_PROJ) -target app -configuration Release -sdk iphonesimulator SYMROOT=$(IOS_OUT) build

run-ios: build-ios
	open -a Simulator
	xcrun simctl bootstatus booted -b
	xcrun simctl install booted out/build/ios/Debug-iphonesimulator/app.app
	xcrun simctl launch booted $(IOS_BUNDLE)
else
build-ios release-ios run-ios:
	@echo "make $@: needs macOS + Xcode"; exit 1
endif

.PHONY: build-ios release-ios run-ios
