BUILD_DIR   := build
APP_DIR     := app
DRIVER_NAME := NoiseAI
CMAKE       := cmake
NINJA       := ninja
ONNX_LIB    := third_party/onnxruntime/onnxruntime-osx-arm64-1.24.3/lib
VERSION     := $(shell cat VERSION)
STAGING_DIR := $(BUILD_DIR)/staging

.PHONY: all deps engine driver bridge cmake-build app install uninstall install-driver uninstall-driver clean test format package

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
	@echo "Copying DeepFilterNet model into app resources..."
	mkdir -p $(APP_DIR)/Resources/Models
	cp models/deepfilternet.onnx $(APP_DIR)/Resources/Models/deepfilternet.onnx
	cd $(APP_DIR) && xcodegen generate
	xcodebuild -project $(APP_DIR)/$(DRIVER_NAME).xcodeproj -scheme $(DRIVER_NAME) -configuration Release SYMROOT=$(CURDIR)/$(APP_DIR)/build clean build
	@echo "Bundling ONNX Runtime dylib..."
	mkdir -p $(APP_DIR)/build/Release/$(DRIVER_NAME).app/Contents/Frameworks
	cp $(ONNX_LIB)/libonnxruntime.1.24.3.dylib $(APP_DIR)/build/Release/$(DRIVER_NAME).app/Contents/Frameworks/
	cd $(APP_DIR)/build/Release/$(DRIVER_NAME).app/Contents/Frameworks && ln -sf libonnxruntime.1.24.3.dylib libonnxruntime.dylib
	codesign --force --sign - $(APP_DIR)/build/Release/$(DRIVER_NAME).app/Contents/Frameworks/libonnxruntime.1.24.3.dylib
	@echo "ONNX Runtime dylib bundled and signed."
	@echo "Verifying model is in app bundle..."
	@test -f $(APP_DIR)/build/Release/$(DRIVER_NAME).app/Contents/Resources/Models/deepfilternet.onnx \
		&& echo "  Model found in app bundle." \
		|| (echo "  WARNING: Model NOT found in app bundle — check project.yml resources config." && exit 1)

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
	rm -rf $(APP_DIR)/Resources/Models/deepfilternet.onnx

test: cmake-build
	cd $(BUILD_DIR) && ctest --output-on-failure

format:
	@echo "Format target: not yet configured."

# ---------------------------------------------------------------------------
# Packaging: build a .pkg installer for macOS
# ---------------------------------------------------------------------------
package: all
	@echo "=== Building NoiseAI Installer Package (v$(VERSION)) ==="
	@# Clean previous staging
	rm -rf $(STAGING_DIR)
	@# --- Stage the application ---
	mkdir -p $(STAGING_DIR)/app
	cp -R $(APP_DIR)/build/Release/$(DRIVER_NAME).app $(STAGING_DIR)/app/
	@# Verify bundled assets
	@echo "Verifying staged app bundle..."
	@test -f $(STAGING_DIR)/app/$(DRIVER_NAME).app/Contents/Frameworks/libonnxruntime.1.24.3.dylib \
		|| (echo "ERROR: ONNX Runtime dylib missing from staged app" && exit 1)
	@test -f $(STAGING_DIR)/app/$(DRIVER_NAME).app/Contents/Resources/Models/deepfilternet.onnx \
		|| (echo "ERROR: DeepFilterNet model missing from staged app" && exit 1)
	@# --- Stage the driver ---
	mkdir -p $(STAGING_DIR)/driver
	cp -R $(BUILD_DIR)/driver/$(DRIVER_NAME).driver $(STAGING_DIR)/driver/
	@# --- Build component packages ---
	@echo "Building component packages..."
	pkgbuild --root $(STAGING_DIR)/app \
		--install-location /Applications \
		--identifier com.noiseai.app \
		--version $(VERSION) \
		$(BUILD_DIR)/NoiseAI-app.pkg
	pkgbuild --root $(STAGING_DIR)/driver \
		--install-location /Library/Audio/Plug-Ins/HAL \
		--identifier com.noiseai.driver \
		--version $(VERSION) \
		--scripts installer/macos/scripts \
		$(BUILD_DIR)/NoiseAI-driver.pkg
	@# --- Combine into a single product archive ---
	@echo "Building product installer..."
	productbuild --distribution installer/macos/distribution.xml \
		--package-path $(BUILD_DIR) \
		--resources installer/macos/resources \
		$(BUILD_DIR)/NoiseAI-Installer.pkg
	@# --- Report ---
	@echo ""
	@echo "=== Installer built successfully ==="
	@ls -lh $(BUILD_DIR)/NoiseAI-Installer.pkg
	@echo ""
	@echo "Install with:  open $(BUILD_DIR)/NoiseAI-Installer.pkg"
	@echo "Uninstall:     sudo ./scripts/uninstall.sh"
