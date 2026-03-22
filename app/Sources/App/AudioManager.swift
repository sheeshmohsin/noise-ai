import AVFoundation
import CoreAudio
import AudioToolbox

// Context accessible from the real-time audio callback.
// Pre-allocates all buffers so the callback never allocates memory.
final class CaptureContext {
    var audioUnit: AudioComponentInstance?
    var engineHandle: NoiseEngineHandle?
    var captureBuffer: UnsafeMutablePointer<Float32>?
    let maxFrames: UInt32 = 4096
    let channels: UInt32 = 2
    // Debug: track whether the callback has ever been called
    var callbackFired: Bool = false

    init() {
        captureBuffer = UnsafeMutablePointer<Float32>.allocate(
            capacity: Int(maxFrames * channels))
    }

    deinit {
        captureBuffer?.deallocate()
    }
}

// AUHAL input callback — called on CoreAudio's real-time audio thread.
// Must be real-time safe: no allocations, no locks, no ObjC.
//
// Audio Workgroup / P-core scheduling (Apple Silicon):
// This callback runs on the audio device's I/O thread, which CoreAudio
// automatically places in the device's os_workgroup. Because we call
// noise_engine_process_and_write() synchronously here (no separate
// inference thread), our processing inherits the workgroup membership
// and is scheduled on Performance cores by the OS.
//
// TODO: If processing is ever moved to a separate thread (e.g., for
// async inference), that thread must explicitly join the audio device's
// workgroup via os_workgroup_join() to retain P-core scheduling.
// Retrieve the workgroup from the audio device using
// kAudioDevicePropertyIOThreadOSWorkgroup and pass it to the worker.
private func auhalInputCallback(
    inRefCon: UnsafeMutableRawPointer,
    ioActionFlags: UnsafeMutablePointer<AudioUnitRenderActionFlags>,
    inTimeStamp: UnsafePointer<AudioTimeStamp>,
    inBusNumber: UInt32,
    inNumberFrames: UInt32,
    ioData: UnsafeMutablePointer<AudioBufferList>?
) -> OSStatus {
    let ctx = Unmanaged<CaptureContext>.fromOpaque(inRefCon).takeUnretainedValue()

    // Log once when the callback first fires
    if !ctx.callbackFired {
        ctx.callbackFired = true
        print("[NoiseAI] AUHAL callback firing! frames=\(inNumberFrames), bus=\(inBusNumber)")
    }

    guard let audioUnit = ctx.audioUnit,
          let captureBuffer = ctx.captureBuffer else {
        return noErr
    }

    // Clamp to pre-allocated buffer size
    let framesToRender = min(inNumberFrames, ctx.maxFrames)
    let channels = ctx.channels
    let bytesPerFrame = channels * UInt32(MemoryLayout<Float32>.size)
    let bufferSize = framesToRender * bytesPerFrame

    // Set up the AudioBufferList pointing to our pre-allocated buffer
    var bufferList = AudioBufferList(
        mNumberBuffers: 1,
        mBuffers: AudioBuffer(
            mNumberChannels: channels,
            mDataByteSize: bufferSize,
            mData: captureBuffer
        )
    )

    // Pull audio data from the input device
    let status = AudioUnitRender(
        audioUnit, ioActionFlags, inTimeStamp, 1, framesToRender, &bufferList)
    if status != noErr {
        // Log first render failure
        struct RenderErrorLog {
            static var logged = false
        }
        if !RenderErrorLog.logged {
            RenderErrorLog.logged = true
            print("[NoiseAI] AudioUnitRender failed: \(status)")
        }
        return status
    }

    // Process captured audio through RNNoise and write to shared memory
    if let handle = ctx.engineHandle {
        let result = noise_engine_process_and_write(handle, captureBuffer, framesToRender)
        // Log first write result (once) for diagnostics
        struct WriteLog {
            static var logged = false
        }
        if !WriteLog.logged {
            WriteLog.logged = true
            print("[NoiseAI] First process_and_write: \(framesToRender) frames, result=\(result)")
        }
    }

    return noErr
}

@Observable
class AudioManager {
    var isProcessing = false {
        didSet { onProcessingChanged?(isProcessing) }
    }
    var currentMode: NoiseMode = .balanced
    var engineStatus: EngineStatus = .stopped

    /// Called whenever `isProcessing` changes; set by AppDelegate to update the status icon.
    var onProcessingChanged: ((Bool) -> Void)?

    /// The AudioObjectID of the device currently being captured, or nil if using system default
    private(set) var currentDeviceID: AudioObjectID?

    private var engineHandle: NoiseEngineHandle?
    private var captureContext: CaptureContext?
    // Strong reference to keep the context alive for the callback
    private var retainedContext: Unmanaged<CaptureContext>?

    enum NoiseMode: String, CaseIterable {
        case cpuSaver = "CPU Saver"
        case balanced = "Balanced"
        case maxQuality = "Max Quality"
    }

    enum EngineStatus {
        case stopped, running, error
    }

    init() {
        engineHandle = noise_engine_create(48000, 2, 128)
        loadBundledModel()
    }

    /// Load the DeepFilterNet model from the app bundle's Resources/Models directory.
    private func loadBundledModel() {
        guard let handle = engineHandle else { return }

        if let modelPath = Bundle.main.path(forResource: "deepfilternet", ofType: "onnx", inDirectory: "Models") {
            let result = noise_engine_load_deepfilter_model(handle, modelPath)
            if result != 0 {
                print("[NoiseAI] Loaded DeepFilterNet model from bundle: \(modelPath)")
            } else {
                print("[NoiseAI] Failed to load DeepFilterNet model from bundle: \(modelPath)")
            }
        } else {
            print("[NoiseAI] DeepFilterNet model not found in app bundle (will use search paths)")
        }
    }

    deinit {
        stop()
        if let handle = engineHandle {
            noise_engine_destroy(handle)
            engineHandle = nil
        }
    }

    func start() {
        start(withDeviceID: currentDeviceID)
    }

    func start(withDeviceID deviceID: AudioObjectID?) {
        guard let handle = engineHandle else {
            isProcessing = false
            return
        }

        // Create shared memory ring buffer (producer side)
        let shmResult = noise_shm_create(handle, 48000, 2)
        if shmResult == 0 {
            print("[NoiseAI] Failed to create shared memory buffer")
            engineStatus = .error
            isProcessing = false
            return
        }
        print("[NoiseAI] Shared memory buffer created successfully")

        // Start the engine
        noise_engine_start(handle)

        // Set up AUHAL for microphone capture
        currentDeviceID = deviceID
        setupAudioUnit(deviceID: deviceID)

        isProcessing = true
        engineStatus = .running
    }

    func stop() {
        // Stop and dispose the audio unit
        if let ctx = captureContext, let au = ctx.audioUnit {
            AudioOutputUnitStop(au)
            AudioComponentInstanceDispose(au)
            ctx.audioUnit = nil
        }

        // Release the retained context reference
        retainedContext?.release()
        retainedContext = nil
        captureContext = nil

        if let handle = engineHandle {
            noise_engine_stop(handle)
            noise_shm_destroy(handle)
        }

        isProcessing = false
        engineStatus = .stopped
    }

    /// Switch to a different input device. If currently processing, restarts capture.
    func switchDevice(to deviceID: AudioObjectID?) {
        currentDeviceID = deviceID
        if isProcessing {
            stop()
            start(withDeviceID: deviceID)
        }
    }

    func setMode(_ mode: NoiseMode) {
        currentMode = mode
        guard let handle = engineHandle else { return }
        let modeInt: Int32 = switch mode {
            case .cpuSaver: 0
            case .balanced: 1
            case .maxQuality: 2
        }
        noise_engine_set_mode(handle, modeInt)
    }

    // MARK: - AUHAL Setup

    private func setupAudioUnit(deviceID: AudioObjectID?) {
        // Find the AUHAL audio component
        var desc = AudioComponentDescription(
            componentType: kAudioUnitType_Output,
            componentSubType: kAudioUnitSubType_HALOutput,
            componentManufacturer: kAudioUnitManufacturer_Apple,
            componentFlags: 0,
            componentFlagsMask: 0
        )

        guard let component = AudioComponentFindNext(nil, &desc) else {
            print("[NoiseAI] Failed to find AUHAL component")
            engineStatus = .error
            return
        }

        var au: AudioComponentInstance?
        var status = AudioComponentInstanceNew(component, &au)
        guard status == noErr, let audioUnit = au else {
            print("[NoiseAI] Failed to create AUHAL instance: \(status)")
            engineStatus = .error
            return
        }

        // Enable input on bus 1 (microphone capture)
        var enableInput: UInt32 = 1
        status = AudioUnitSetProperty(audioUnit,
            kAudioOutputUnitProperty_EnableIO,
            kAudioUnitScope_Input,
            1,
            &enableInput,
            UInt32(MemoryLayout<UInt32>.size))
        if status != noErr {
            print("[NoiseAI] Failed to enable input: \(status)")
        }

        // Disable output on bus 0 (we only capture, no playback)
        var disableOutput: UInt32 = 0
        status = AudioUnitSetProperty(audioUnit,
            kAudioOutputUnitProperty_EnableIO,
            kAudioUnitScope_Output,
            0,
            &disableOutput,
            UInt32(MemoryLayout<UInt32>.size))
        if status != noErr {
            print("[NoiseAI] Failed to disable output: \(status)")
        }

        // Determine which device to use
        let inputDeviceID: AudioObjectID
        if let explicit = deviceID {
            inputDeviceID = explicit
        } else {
            inputDeviceID = findPhysicalInputDevice()
        }

        // Set the input device
        var devID = inputDeviceID
        status = AudioUnitSetProperty(audioUnit,
            kAudioOutputUnitProperty_CurrentDevice,
            kAudioUnitScope_Global,
            0,
            &devID,
            UInt32(MemoryLayout<AudioObjectID>.size))
        if status != noErr {
            print("[NoiseAI] Failed to set input device: \(status)")
        }

        // Set the desired stream format on the output scope of bus 1.
        // This is the format we receive captured data in.
        var streamFormat = AudioStreamBasicDescription(
            mSampleRate: 48000,
            mFormatID: kAudioFormatLinearPCM,
            mFormatFlags: kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
            mBytesPerPacket: 8,     // 2 channels * 4 bytes/sample
            mFramesPerPacket: 1,
            mBytesPerFrame: 8,
            mChannelsPerFrame: 2,
            mBitsPerChannel: 32,
            mReserved: 0
        )

        status = AudioUnitSetProperty(audioUnit,
            kAudioUnitProperty_StreamFormat,
            kAudioUnitScope_Output,
            1,  // input element (bus 1)
            &streamFormat,
            UInt32(MemoryLayout<AudioStreamBasicDescription>.size))
        if status != noErr {
            print("[NoiseAI] Failed to set stream format: \(status)")
        }

        // Create the capture context with pre-allocated buffers
        let ctx = CaptureContext()
        ctx.audioUnit = audioUnit
        ctx.engineHandle = engineHandle
        self.captureContext = ctx

        // Retain the context for the C callback
        retainedContext = Unmanaged.passRetained(ctx)

        // Set the input callback
        var callbackStruct = AURenderCallbackStruct(
            inputProc: auhalInputCallback,
            inputProcRefCon: retainedContext!.toOpaque()
        )

        status = AudioUnitSetProperty(audioUnit,
            kAudioOutputUnitProperty_SetInputCallback,
            kAudioUnitScope_Global,
            0,
            &callbackStruct,
            UInt32(MemoryLayout<AURenderCallbackStruct>.size))
        if status != noErr {
            print("[NoiseAI] Failed to set input callback: \(status)")
        }

        // Initialize the audio unit
        status = AudioUnitInitialize(audioUnit)
        if status != noErr {
            print("[NoiseAI] Failed to initialize AUHAL: \(status)")
            engineStatus = .error
            return
        }

        // Start capturing
        status = AudioOutputUnitStart(audioUnit)
        if status != noErr {
            print("[NoiseAI] Failed to start AUHAL: \(status)")
            engineStatus = .error
            return
        }

        print("[NoiseAI] AUHAL capture started successfully")
    }

    // MARK: - Device Selection

    /// Find a physical input device, avoiding our own NoiseAI virtual mic.
    private func findPhysicalInputDevice() -> AudioObjectID {
        // Get the default input device first
        var defaultDeviceID = AudioObjectID(0)
        var propertySize = UInt32(MemoryLayout<AudioObjectID>.size)
        var propertyAddress = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDefaultInputDevice,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )

        AudioObjectGetPropertyData(
            AudioObjectID(kAudioObjectSystemObject),
            &propertyAddress,
            0, nil,
            &propertySize,
            &defaultDeviceID
        )

        // Check if the default device is our NoiseAI virtual mic
        let defaultName = getDeviceName(defaultDeviceID)
        if !defaultName.contains("NoiseAI") {
            return defaultDeviceID
        }

        print("[NoiseAI] Default input is our virtual mic, looking for physical mic...")

        // Enumerate all audio devices and find the first non-NoiseAI input
        var devicesAddress = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDevices,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )

        var devicesSize: UInt32 = 0
        AudioObjectGetPropertyDataSize(
            AudioObjectID(kAudioObjectSystemObject),
            &devicesAddress,
            0, nil,
            &devicesSize
        )

        let deviceCount = Int(devicesSize) / MemoryLayout<AudioObjectID>.size
        var deviceIDs = [AudioObjectID](repeating: 0, count: deviceCount)
        AudioObjectGetPropertyData(
            AudioObjectID(kAudioObjectSystemObject),
            &devicesAddress,
            0, nil,
            &devicesSize,
            &deviceIDs
        )

        for deviceID in deviceIDs {
            // Check if this device has input channels
            var inputStreamAddress = AudioObjectPropertyAddress(
                mSelector: kAudioDevicePropertyStreams,
                mScope: kAudioObjectPropertyScopeInput,
                mElement: kAudioObjectPropertyElementMain
            )

            var streamSize: UInt32 = 0
            let streamStatus = AudioObjectGetPropertyDataSize(
                deviceID, &inputStreamAddress, 0, nil, &streamSize)
            if streamStatus != noErr || streamSize == 0 {
                continue  // No input streams
            }

            // Skip our virtual device
            let name = getDeviceName(deviceID)
            if name.contains("NoiseAI") {
                continue
            }

            print("[NoiseAI] Using physical input device: \(name)")
            return deviceID
        }

        // Fallback: return default device anyway
        print("[NoiseAI] No alternative input device found, using default")
        return defaultDeviceID
    }

    private func getDeviceName(_ deviceID: AudioObjectID) -> String {
        var name: CFString?
        var propertySize = UInt32(MemoryLayout<CFString?>.size)
        var nameAddress = AudioObjectPropertyAddress(
            mSelector: kAudioObjectPropertyName,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )

        let status = withUnsafeMutablePointer(to: &name) { ptr in
            AudioObjectGetPropertyData(
                deviceID, &nameAddress, 0, nil, &propertySize, ptr)
        }
        if status == noErr, let cfName = name {
            return cfName as String
        }
        return ""
    }
}
