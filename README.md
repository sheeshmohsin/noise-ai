# NoiseAI

Real-time noise cancellation desktop tool for macOS. Works system-wide as a virtual audio device — select "NoiseAI Microphone" in any app (Zoom, Teams, Meet, etc.) to get clean, noise-free audio.

## Features

- **Virtual Microphone**: Appears as "NoiseAI Microphone" in all apps
- **On-device ML**: All processing happens locally — no audio leaves your machine
- **Low latency**: ~7-16ms algorithmic latency (imperceptible in calls)
- **Multiple modes**: CPU Saver (RNNoise) / Balanced / Max Quality (DeepFilterNet2)
- **Menubar app**: Minimal UI with quick toggle, device picker, and mode selector

## Architecture

```
Physical Mic → AUHAL capture → ML inference (ONNX Runtime) → Shared Memory → Virtual Mic → Apps
```

- **Audio Engine**: C++17, CoreAudio AUHAL, lock-free SPSC ring buffers
- **Virtual Device**: AudioServerPlugIn via libASPL
- **ML Runtime**: ONNX Runtime (CPU), RNNoise + DeepFilterNet2 models
- **App**: Swift/SwiftUI + AppKit menubar app (macOS 14+)
- **IPC**: POSIX shared memory for audio, localhost TCP for control

## Requirements

- macOS 14 (Sonoma) or later
- Apple Silicon or Intel Mac

## Building

### Prerequisites

```bash
brew install cmake ninja xcodegen
```

### Build

```bash
make all          # Build everything (engine, driver, app)
make cmake-build  # Build C++ components only
make app          # Build Swift app only
make test         # Run tests
```

### Install

```bash
make install-driver   # Install virtual audio driver (requires sudo)
make uninstall-driver # Remove virtual audio driver
```

## Usage

1. Install the driver: `make install-driver`
2. Launch NoiseAI from the menubar
3. Toggle noise cancellation ON
4. In your call app, select "NoiseAI Microphone" as input device

## License

MIT
