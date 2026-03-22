import Foundation
import CoreAudio

@Observable
class DeviceManager {
    var inputDevices: [AudioDevice] = []
    var selectedDeviceUID: String? {
        didSet {
            UserDefaults.standard.set(selectedDeviceUID, forKey: "selectedDeviceUID")
        }
    }

    /// Callback invoked when the selected device changes (set by AudioManager)
    var onDeviceSelectionChanged: ((AudioObjectID?) -> Void)?

    struct AudioDevice: Identifiable, Hashable {
        let id: UInt32          // AudioObjectID
        let uid: String
        let name: String
        let isDefault: Bool
    }

    // Listener block storage to prevent deallocation
    @ObservationIgnored
    private var deviceListListenerBlock: AudioObjectPropertyListenerBlock?
    @ObservationIgnored
    private var defaultDeviceListenerBlock: AudioObjectPropertyListenerBlock?
    @ObservationIgnored
    private let listenerQueue = DispatchQueue(label: "com.noiseai.device-listener")

    init() {
        refreshDevices()
        restoreSelectedDevice()
        installListeners()
    }

    deinit {
        removeListeners()
    }

    // MARK: - Public

    func refreshDevices() {
        let defaultID = getDefaultInputDeviceID()
        let allDeviceIDs = getAllDeviceIDs()

        var devices: [AudioDevice] = []
        for deviceID in allDeviceIDs {
            guard hasInputStreams(deviceID) else { continue }
            let name = getDeviceName(deviceID)
            // Exclude our own virtual device
            if name.contains("NoiseAI") { continue }
            let uid = getDeviceUID(deviceID)
            devices.append(AudioDevice(
                id: deviceID,
                uid: uid,
                name: name,
                isDefault: deviceID == defaultID
            ))
        }

        inputDevices = devices

        // If the currently selected device is no longer available, fall back
        if let selectedUID = selectedDeviceUID,
           !devices.contains(where: { $0.uid == selectedUID }) {
            // Device was unplugged — fall back to default
            let fallback = devices.first(where: { $0.isDefault }) ?? devices.first
            selectedDeviceUID = fallback?.uid
            onDeviceSelectionChanged?(fallback.map { AudioObjectID($0.id) })
        }
    }

    func selectDevice(uid: String) {
        selectedDeviceUID = uid
        if let device = inputDevices.first(where: { $0.uid == uid }) {
            onDeviceSelectionChanged?(AudioObjectID(device.id))
        }
    }

    /// Returns the AudioObjectID for the currently selected device, or nil
    func selectedDeviceID() -> AudioObjectID? {
        guard let uid = selectedDeviceUID,
              let device = inputDevices.first(where: { $0.uid == uid }) else {
            return nil
        }
        return AudioObjectID(device.id)
    }

    // MARK: - Restore from UserDefaults

    private func restoreSelectedDevice() {
        if let savedUID = UserDefaults.standard.string(forKey: "selectedDeviceUID"),
           inputDevices.contains(where: { $0.uid == savedUID }) {
            selectedDeviceUID = savedUID
        } else {
            // Fall back to default input
            let defaultDevice = inputDevices.first(where: { $0.isDefault }) ?? inputDevices.first
            selectedDeviceUID = defaultDevice?.uid
        }
    }

    // MARK: - CoreAudio Listeners

    private func installListeners() {
        // Listen for device list changes (hotplug)
        var devicesAddress = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDevices,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )

        let devicesBlock: AudioObjectPropertyListenerBlock = { [weak self] _, _ in
            DispatchQueue.main.async {
                self?.refreshDevices()
            }
        }
        deviceListListenerBlock = devicesBlock

        AudioObjectAddPropertyListenerBlock(
            AudioObjectID(kAudioObjectSystemObject),
            &devicesAddress,
            listenerQueue,
            devicesBlock
        )

        // Listen for default input device changes
        var defaultAddress = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDefaultInputDevice,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )

        let defaultBlock: AudioObjectPropertyListenerBlock = { [weak self] _, _ in
            DispatchQueue.main.async {
                self?.refreshDevices()
            }
        }
        defaultDeviceListenerBlock = defaultBlock

        AudioObjectAddPropertyListenerBlock(
            AudioObjectID(kAudioObjectSystemObject),
            &defaultAddress,
            listenerQueue,
            defaultBlock
        )
    }

    private func removeListeners() {
        var devicesAddress = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDevices,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )

        if let block = deviceListListenerBlock {
            AudioObjectRemovePropertyListenerBlock(
                AudioObjectID(kAudioObjectSystemObject),
                &devicesAddress,
                listenerQueue,
                block
            )
        }

        var defaultAddress = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDefaultInputDevice,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )

        if let block = defaultDeviceListenerBlock {
            AudioObjectRemovePropertyListenerBlock(
                AudioObjectID(kAudioObjectSystemObject),
                &defaultAddress,
                listenerQueue,
                block
            )
        }
    }

    // MARK: - CoreAudio Queries

    private func getDefaultInputDeviceID() -> AudioObjectID {
        var deviceID = AudioObjectID(0)
        var size = UInt32(MemoryLayout<AudioObjectID>.size)
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDefaultInputDevice,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        AudioObjectGetPropertyData(
            AudioObjectID(kAudioObjectSystemObject),
            &address, 0, nil, &size, &deviceID
        )
        return deviceID
    }

    private func getAllDeviceIDs() -> [AudioObjectID] {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDevices,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )

        var size: UInt32 = 0
        AudioObjectGetPropertyDataSize(
            AudioObjectID(kAudioObjectSystemObject),
            &address, 0, nil, &size
        )

        let count = Int(size) / MemoryLayout<AudioObjectID>.size
        var ids = [AudioObjectID](repeating: 0, count: count)
        AudioObjectGetPropertyData(
            AudioObjectID(kAudioObjectSystemObject),
            &address, 0, nil, &size, &ids
        )
        return ids
    }

    private func hasInputStreams(_ deviceID: AudioObjectID) -> Bool {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyStreams,
            mScope: kAudioObjectPropertyScopeInput,
            mElement: kAudioObjectPropertyElementMain
        )
        var size: UInt32 = 0
        let status = AudioObjectGetPropertyDataSize(deviceID, &address, 0, nil, &size)
        return status == noErr && size > 0
    }

    private func getDeviceName(_ deviceID: AudioObjectID) -> String {
        var name: CFString?
        var size = UInt32(MemoryLayout<CFString?>.size)
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioObjectPropertyName,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        let status = withUnsafeMutablePointer(to: &name) { ptr in
            AudioObjectGetPropertyData(deviceID, &address, 0, nil, &size, ptr)
        }
        if status == noErr, let cfName = name {
            return cfName as String
        }
        return "Unknown Device"
    }

    private func getDeviceUID(_ deviceID: AudioObjectID) -> String {
        var uid: CFString?
        var size = UInt32(MemoryLayout<CFString?>.size)
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyDeviceUID,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        let status = withUnsafeMutablePointer(to: &uid) { ptr in
            AudioObjectGetPropertyData(deviceID, &address, 0, nil, &size, ptr)
        }
        if status == noErr, let cfUID = uid {
            return cfUID as String
        }
        return ""
    }
}
