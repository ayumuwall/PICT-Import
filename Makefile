CXX = xcrun clang++
SDK ?= .vendor/photoshop-sdk
SDK_INCLUDE = -I"$(SDK)/pluginsdk/photoshopapi/photoshop" -I"$(SDK)/pluginsdk/photoshopapi/pica_sp"
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -Wno-deprecated-declarations -Isrc
FRAMEWORKS = -framework CoreGraphics -framework CoreText -framework CoreFoundation -framework ImageIO
ARCHS ?= -arch x86_64 -arch arm64
BUNDLE = dist/PICT Import.plugin

.PHONY: all plugin installer test test-installer clean
all: plugin

plugin: src/pict.cpp src/pict.hpp src/plugin.cpp resources/Info.plist resources/PiPLs.json LICENSE
	@test -f "$(SDK)/pluginsdk/photoshopapi/photoshop/PIFormat.h" || (echo 'Adobe SDKがありません。python3 scripts/fetch_sdk.py を実行してください。' && exit 1)
	mkdir -p "$(BUNDLE)/Contents/MacOS" "$(BUNDLE)/Contents/Resources"
	$(CXX) $(CXXFLAGS) $(SDK_INCLUDE) $(ARCHS) -mmacosx-version-min=12.0 -O2 -fvisibility=hidden -bundle src/pict.cpp src/plugin.cpp $(FRAMEWORKS) -o "$(BUNDLE)/Contents/MacOS/PICTImport"
	cp resources/Info.plist "$(BUNDLE)/Contents/Info.plist"
	cp resources/PiPLs.json "$(BUNDLE)/Contents/Resources/PiPLs.json"
	cp LICENSE "$(BUNDLE)/Contents/Resources/LICENSE.txt"
	printf '8BIF8BIM' > "$(BUNDLE)/Contents/PkgInfo"
	python3 scripts/verify_plugin_metadata.py "$(BUNDLE)"
	codesign --force --sign - "$(BUNDLE)"

installer: plugin
	python3 scripts/build_installer.py

test-installer: installer
	python3 scripts/verify_installer.py

build/pict-tests: src/pict.cpp src/pict.hpp tests/pict_tests.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -g -O1 -fsanitize=address,undefined src/pict.cpp tests/pict_tests.cpp $(FRAMEWORKS) -o "$@"

build/plugin-tests: tests/plugin_tests.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(SDK_INCLUDE) tests/plugin_tests.cpp -framework CoreFoundation -o "$@"

test: build/pict-tests plugin build/plugin-tests
	python3 tests/test_plugin_metadata.py
	./build/pict-tests
	./build/plugin-tests

clean:
	rm -rf build dist
