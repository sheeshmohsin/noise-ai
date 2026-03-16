import Foundation

@Observable
class DeviceManager {
    var inputDevices: [AudioDevice] = []
    var selectedInputDevice: AudioDevice?

    struct AudioDevice: Identifiable, Hashable {
        let id: UInt32
        let name: String
        let isDefault: Bool
    }

    func refreshDevices() {
        // TODO: Enumerate CoreAudio devices
        inputDevices = [
            AudioDevice(id: 1, name: "Built-in Microphone", isDefault: true)
        ]
        selectedInputDevice = inputDevices.first
    }
}
