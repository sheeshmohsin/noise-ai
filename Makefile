BUILD_DIR   := build
APP_DIR     := app
DRIVER_NAME := NoiseAI
CMAKE       := cmake
NINJA       := ninja
ONNX_LIB    := third_party/onnxruntime/onnxruntime-osx-arm64-1.24.3/lib

.PHONY: all deps engine driver bridge cmake-build app install uninstall install-driver uninstall-driver clean test format

all: cmake-build app

deps:
	@./scripts/install_deps.sh

cmake-build:
	$(CMAKE) -B $(BUILD_DIR) -G Ninja
	$(CMAKE) --build $(BUILD_DIR)

engine:
	$(CMAKE) -B $(BUILD_DIR) -G Ninja
	$(CMAKE) --build $(BUILD_DIR) --target noiseengine

driver:
	$(CMAKE) -B $(BUILD_DIR) -G Ninja
	$(CMAKE) --build $(BUILD_DIR) --target NoiseAIDriver

bridge:
	$(CMAKE) -B $(BUILD_DIR) -G Ninja
	$(CMAKE) --build $(BUILD_DIR) --target noisebridge

app: cmake-build
	cd $(APP_DIR) && xcodegen generate
	xcodebuild -project $(APP_DIR)/$(DRIVER_NAME).xcodeproj -scheme $(DRIVER_NAME) -configuration Release SYMROOT=$(CURDIR)/$(APP_DIR)/build build
	@echo "Bundling ONNX Runtime dylib..."
	mkdir -p $(APP_DIR)/build/Release/$(DRIVER_NAME).app/Contents/Frameworks
	cp $(ONNX_LIB)/libonnxruntime.1.24.3.dylib $(APP_DIR)/build/Release/$(DRIVER_NAME).app/Contents/Frameworks/
	cd $(APP_DIR)/build/Release/$(DRIVER_NAME).app/Contents/Frameworks && ln -sf libonnxruntime.1.24.3.dylib libonnxruntime.dylib
	codesign --force --sign - $(APP_DIR)/build/Release/$(DRIVER_NAME).app/Contents/Frameworks/libonnxruntime.1.24.3.dylib
	@echo "ONNX Runtime dylib bundled and signed."

install:
	@echo "Installing $(DRIVER_NAME) (requires sudo)..."
	sudo cp -R $(BUILD_DIR)/driver/$(DRIVER_NAME).driver /Library/Audio/Plug-Ins/HAL/
	sudo cp -R $(APP_DIR)/build/Release/$(DRIVER_NAME).app /Applications/
	@echo "Done. You may need to restart coreaudiod."

uninstall:
	@echo "Uninstalling $(DRIVER_NAME) (requires sudo)..."
	sudo rm -rf /Library/Audio/Plug-Ins/HAL/$(DRIVER_NAME).driver
	sudo rm -rf /Applications/$(DRIVER_NAME).app
	@echo "Done."

install-driver:
	@./scripts/install_driver.sh

uninstall-driver:
	@./scripts/uninstall_driver.sh

clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(APP_DIR)/*.xcodeproj

test: cmake-build
	cd $(BUILD_DIR) && ctest --output-on-failure

format:
	@echo "Format target: not yet configured."
