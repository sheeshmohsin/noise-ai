# NoiseAI - Implementation Plan

## Context

Building a Krisp-class real-time noise cancellation desktop tool for macOS (Phase 1), with future Windows and meeting transcription support. The PRD (`prd-noise-ai.md`) provides the full product vision. All architectural decisions below were made through research-backed analysis during our planning session.

---

## Locked Architectural Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| **Core engine language** | C++ (engine) + Swift (macOS UI) | C++ is industry standard for real-time audio; Swift for native macOS UI |
| **ML models** | RNNoise (CPU Saver) + DeepFilterNet2 (Balanced/Max Quality) | Tiered approach covers low-end to high-end hardware |
| **Inference runtime** | ONNX Runtime (CPU-only EP) | Audio models use GRU/RNN — ANE can't accelerate these. CPU is faster for small models. Cross-platform with one model file. |
| **Virtual audio device** | Custom AudioServerPlugIn via libASPL (MIT) | Apple-recommended for virtual devices. BlackHole (GPL) not viable for commercial. libASPL wraps HAL complexity. |
| **Audio capture** | CoreAudio HAL via AUHAL AudioUnit | Precise buffer control (128 frames @ 48kHz = 2.67ms). Real-time thread. Same as Krisp/pro audio apps. |
| **IPC (audio)** | POSIX shared memory + lock-free SPSC ring buffer | ~100ns per transfer. Only real-time-safe option for AudioServerPlugIn IO callbacks. |
| **IPC (control)** | Localhost TCP socket | Proven by roc-vad. Allowed in AudioServerPlugIn sandbox. |
| **macOS UI** | SwiftUI + AppKit hybrid | AppKit for menubar (NSStatusItem). SwiftUI for views. macOS 14+ target. |
| **Build system** | CMake (C++) + Xcode/XcodeGen (Swift) + Makefile orchestration | Each tool used for what it's best at. Makefile gives familiar `make` interface. |
| **AEC** | Deferred to v2 | Significant extra scope. Most conferencing apps have their own AEC. v1 focuses on noise suppression. |

---

## Project Structure

```
noise-ai/
├── Makefile                          # Top-level: make all, make engine, make driver, make app, make install
├── CMakeLists.txt                    # Top-level CMake: project version, add_subdirectory for C++ targets
├── .gitignore
├── VERSION
├── prd-noise-ai.md                   # Product requirements (existing)
│
├── cmake/                            # CMake modules
│   ├── FindONNXRuntime.cmake         # Locate ONNX Runtime prebuilt libs
│   └── utils.cmake                   # Shared helpers
│
├── engine/                           # C++ audio/ML engine (CROSS-PLATFORM core)
│   ├── CMakeLists.txt                # Builds libnoiseengine.a
│   ├── include/noise/                # Public headers (namespaced)
│   │   ├── engine.h                  # Main engine API (init, process_frame, set_mode, shutdown)
│   │   ├── inference.h               # ONNX model loader + inference
│   │   ├── ring_buffer.h             # Lock-free SPSC ring buffer
│   │   ├── resampler.h               # Sample rate conversion (48kHz <-> 16kHz)
│   │   ├── stft.h                    # STFT/ISTFT for spectral processing
│   │   └── types.h                   # Shared types, enums (NoiseMode, AudioFormat, etc.)
│   ├── src/
│   │   ├── engine.cpp                # Pipeline orchestration
│   │   ├── inference.cpp             # ONNX Runtime session management
│   │   ├── ring_buffer.cpp           # SPSC ring buffer (cache-line aligned atomics)
│   │   ├── resampler.cpp             # Polyphase resampler or libsamplerate wrapper
│   │   ├── stft.cpp                  # FFT via Accelerate.framework (macOS) / FFTW (Windows)
│   │   └── post_process.cpp          # Limiter, de-click, comfort noise
│   └── tests/
│       ├── CMakeLists.txt
│       ├── test_ring_buffer.cpp
│       ├── test_inference.cpp
│       └── test_pipeline.cpp
│
├── driver/                           # macOS AudioServerPlugIn (.driver bundle)
│   ├── CMakeLists.txt                # Builds NoiseAI.driver bundle
│   ├── Info.plist.in                 # Bundle metadata (MachServices, bundle ID)
│   ├── src/
│   │   ├── plugin.cpp                # AudioServerPlugInDriverInterface entry point
│   │   ├── device.cpp/.hpp           # Virtual mic device (via libASPL)
│   │   ├── stream.cpp/.hpp           # Audio stream handling
│   │   └── shm_reader.cpp/.hpp       # Reads processed audio from shared memory ring buffer
│   └── tests/
│       └── CMakeLists.txt
│
├── bridge/                           # C API boundary (C++ engine -> Swift)
│   ├── CMakeLists.txt                # Builds libnoisebridge.a
│   ├── include/
│   │   └── noise_bridge.h            # extern "C" API — the ONLY header Swift sees
│   └── src/
│       └── noise_bridge.cpp          # Wraps C++ engine calls in C functions
│
├── app/                              # macOS SwiftUI + AppKit menubar app
│   ├── project.yml                   # XcodeGen spec (generates .xcodeproj)
│   ├── Sources/
│   │   ├── App/
│   │   │   ├── NoiseApp.swift        # @main entry
│   │   │   ├── AppDelegate.swift     # NSApplicationDelegate, NSStatusItem setup
│   │   │   └── AudioManager.swift    # AUHAL capture + engine bridge
│   │   ├── Views/
│   │   │   ├── MenuBarView.swift     # Dropdown: toggle, device picker, mode selector
│   │   │   ├── SettingsView.swift    # Preferences window
│   │   │   └── SetupWizardView.swift # First-run: mic select, before/after test
│   │   ├── Models/
│   │   │   ├── DeviceManager.swift   # CoreAudio device enumeration, hotplug
│   │   │   ├── EngineState.swift     # @Observable state for UI binding
│   │   │   └── Preferences.swift     # UserDefaults wrapper
│   │   └── Bridge/
│   │       └── NoiseApp-Bridging-Header.h  # #include "noise_bridge.h"
│   ├── Resources/
│   │   ├── Assets.xcassets           # App icon, menubar icons (template images)
│   │   └── Models/                   # ONNX model files bundled here
│   └── Tests/
│       └── ...
│
├── third_party/                      # External dependencies
│   ├── CMakeLists.txt                # FetchContent / ExternalProject for deps
│   ├── onnxruntime/                  # Prebuilt ONNX Runtime (arm64 + x86_64)
│   └── libaspl/                      # libASPL source or prebuilt
│
├── models/                           # Source ONNX model files
│   ├── rnnoise.onnx                  # CPU Saver mode
│   └── deepfilternet2.onnx          # Balanced / Max Quality mode
│
├── installer/
│   └── macos/
│       ├── scripts/
│       │   ├── preinstall.sh         # Check permissions
│       │   └── postinstall.sh        # Restart coreaudiod, register login item
│       └── distribution.xml          # productbuild config
│
├── scripts/
│   ├── install_deps.sh              # brew install cmake ninja xcodegen
│   ├── format.sh                    # clang-format + swiftformat
│   └── export_models.sh            # Convert/download models to ONNX format
│
└── tasks/
    ├── todo.md                      # This file — task tracking
    └── lessons.md                   # Lessons learned
```

---

## Audio Pipeline Architecture

```
Physical Mic (48kHz)
       │
       ▼
┌─────────────────────────────────────────┐
│  macOS App (user-space)                 │
│                                         │
│  AUHAL AudioUnit IO callback            │
│  (real-time thread, 128 frames/2.67ms)  │
│       │                                 │
│       ▼                                 │
│  Lock-free Ring Buffer (capture)        │
│       │                                 │
│       ▼                                 │
│  Inference Thread                       │
│  (joined to audio workgroup for P-core) │
│       │                                 │
│       ├── Downsample 48kHz → 16kHz      │
│       ├── STFT (log magnitude features) │
│       ├── ONNX Runtime inference        │
│       │   ├── RNNoise (CPU Saver)       │
│       │   └── DeepFilterNet2 (Balanced) │
│       ├── Apply ratio mask + ISTFT      │
│       ├── Upsample 16kHz → 48kHz        │
│       └── Post-process (limiter)        │
│       │                                 │
│       ▼                                 │
│  POSIX Shared Memory SPSC Ring Buffer   │
└────────────┬────────────────────────────┘
             │ (~0 latency, lock-free)
             ▼
┌─────────────────────────────────────────┐
│  AudioServerPlugIn (in coreaudiod)      │
│                                         │
│  DoIOOperation() reads from shm         │
│  Serves as "NoiseAI Microphone"         │
└────────────┬────────────────────────────┘
             │
             ▼
   Zoom / Teams / Meet reads clean audio
```

**Estimated latency budget:**

| Component | Latency |
|-----------|---------|
| AUHAL capture buffer (128 frames @ 48kHz) | 2.67 ms |
| Downsample + ONNX inference + upsample | 2-10 ms |
| Shared memory transfer | ~0 ms |
| Virtual device output buffer | 2.67 ms |
| **Total algorithmic latency** | **~7-16 ms** (well under 20ms target) |

---

## Implementation Milestones

### Milestone 1: Project Scaffolding & Build System
**Goal:** Empty project compiles end-to-end with `make all`

- [x] Create directory structure
- [x] Set up top-level `Makefile` with targets: `deps`, `engine`, `driver`, `bridge`, `app`, `all`, `clean`
- [x] Set up top-level `CMakeLists.txt` with `add_subdirectory` for engine, driver, bridge
- [x] Set up `engine/CMakeLists.txt` — builds empty `libnoiseengine.a`
- [x] Set up `driver/CMakeLists.txt` — builds empty `.driver` bundle (stub, libASPL in Milestone 2)
- [x] Set up `bridge/CMakeLists.txt` — builds empty `libnoisebridge.a` with stub C API
- [x] Set up `app/project.yml` (XcodeGen) — generates Xcode project, links bridge lib
- [x] Create minimal Swift app that shows a menubar icon (NSStatusItem)
- [x] Verify: `make all` produces `.app` + `.driver` bundles

### Milestone 2: Virtual Audio Device (Driver)
**Goal:** "NoiseAI Microphone" appears in System Settings → Sound

- [x] Implement AudioServerPlugIn using libASPL: device, stream, volume control
- [x] Configure `Info.plist` with bundle ID, `AudioServerPlugIn_MachServices`
- [x] Implement sine wave test output (to verify the driver works before ML integration)
- [x] Create install script: copies `.driver` to `/Library/Audio/Plug-Ins/HAL/`, restarts `coreaudiod`
- [x] Verify: "NoiseAI Microphone" selectable in Zoom/Teams and produces test tone

### Milestone 3: Audio Capture + Passthrough
**Goal:** Capture from physical mic, pass through to virtual mic (no processing)

- [x] Implement AUHAL capture in Swift `AudioManager` — capture from selected physical mic
- [x] Implement lock-free SPSC ring buffer in `engine/` (cache-line aligned atomics)
- [x] Set up POSIX shared memory segment (`shm_open`/`mmap`) between app and driver
- [x] Driver reads from shared memory in `DoIOOperation()`
- [x] Implement C bridge API: `noise_engine_create()`, `noise_engine_start()`, `noise_engine_stop()`
- [x] Wire up: physical mic → AUHAL → ring buffer → shared memory → virtual mic
- [x] Verify: speak into physical mic, select "NoiseAI Microphone" in Zoom — voice passes through clearly

### Milestone 4: ML Inference Pipeline
**Goal:** Noise cancellation works in real-time

- [ ] Integrate ONNX Runtime (prebuilt, CPU EP, static link)
- [ ] Implement model loader (`inference.cpp`) — load `.onnx` files, create session
- [ ] Implement resampler (48kHz → 16kHz → 48kHz) using Accelerate.framework vDSP
- [ ] Implement STFT/ISTFT using Accelerate.framework vDSP FFT
- [ ] Integrate RNNoise ONNX model — frame processing, ratio mask application
- [ ] Integrate DeepFilterNet2 ONNX model
- [ ] Implement mode switching: CPU Saver (RNNoise) / Balanced (DeepFilterNet2) / Max Quality (DeepFilterNet2 + post-processing)
- [ ] Add processing thread joined to audio workgroup (`os_workgroup_join`)
- [ ] Implement overload handling: detect if inference takes too long, fallback to passthrough
- [ ] Verify: background noise is removed, voice sounds natural, latency is imperceptible

### Milestone 5: macOS App UI
**Goal:** Polished menubar app with full user controls

- [ ] AppKit: `NSStatusItem` with template image (mic icon, changes state for on/off)
- [ ] SwiftUI `MenuBarView`: NC toggle, device picker (mic selection), mode selector
- [ ] Device enumeration using CoreAudio `AudioObjectAddPropertyListenerBlock`
- [ ] Device hotplug handling (headset plug/unplug → auto-switch)
- [ ] SwiftUI `SettingsView`: preferences window (mode, auto-start, update checks)
- [ ] SwiftUI `SetupWizardView`: first-run flow (select mic, before/after demo)
- [ ] `NSApplication.ActivationPolicy.accessory` — hide from Dock
- [ ] Login item via `SMAppService` — launch at login
- [ ] Mic permission request with `NSMicrophoneUsageDescription`
- [ ] Verify: full user flow works end-to-end from first install

### Milestone 6: Packaging & Distribution
**Goal:** Single `.pkg` installer that non-technical users can run

- [ ] Code signing with Developer ID (app + driver)
- [ ] Notarization via `notarytool`
- [ ] `pkgbuild` for driver component (installs to `/Library/Audio/Plug-Ins/HAL/`)
- [ ] `pkgbuild` for app component (installs to `/Applications/`)
- [ ] `productbuild` to combine into single `.pkg`
- [ ] Post-install script: restart `coreaudiod`, register login item
- [ ] Uninstaller script
- [ ] Verify: fresh macOS install → download `.pkg` → install → works in Zoom

### Milestone 7: Testing & Polish
**Goal:** Production-ready quality

- [ ] Unit tests: ring buffer, resampler, STFT, inference pipeline
- [ ] Integration test: end-to-end audio loopback latency measurement
- [ ] Stress test: CPU contention (run during video call + screen share)
- [ ] Device edge cases: hot-swap, exclusive mode, sleep/wake
- [ ] Measure actual CPU usage per mode (target: single-digit %)
- [ ] Measure actual latency per mode (target: <20ms)
- [ ] Memory leak testing (long-running sessions)
- [ ] Crash reporting integration

---

## Key Dependencies

| Dependency | Version | License | Purpose |
|-----------|---------|---------|---------|
| libASPL | latest | MIT | AudioServerPlugIn C++17 wrapper |
| ONNX Runtime | 1.17+ | MIT | ML inference (CPU EP) |
| RNNoise | latest | BSD-3-Clause | Lightweight noise suppression model |
| DeepFilterNet2 | latest | MIT / Apache-2.0 | Full-band noise suppression model |
| XcodeGen | 2.38+ | MIT | Xcode project generation |
| CMake | 3.26+ | BSD-3-Clause | C++ build system |
| Ninja | latest | Apache-2.0 | Fast CMake backend |

---

## Open Items for v2+

- **AEC (Echo Cancellation):** WebRTC AEC3 (BSD, standalone extractions available). Needed for speakerphone scenarios.
- **Virtual Speaker:** Inbound noise cleaning (process audio before it reaches physical speakers)
- **Windows support:** Same C++ engine via CMake. WDM virtual audio driver (SysVAD-based). WinUI 3 for UI.
- **Meeting transcription:** Phase 2 feature per project scope.
- **Voice isolation:** Remove other human voices (background conversation), not just noise.

---

## Verification Plan

After each milestone:
1. **Build:** `make all` succeeds with zero warnings
2. **Unit tests:** `make test` passes
3. **Manual test:** Verify the specific milestone deliverable (documented above)
4. **Latency check:** Measure with audio loopback (from Milestone 4 onward)
5. **CPU check:** Monitor Activity Monitor during a test call (from Milestone 4 onward)
