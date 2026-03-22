import SwiftUI

struct MenuBarView: View {
    @Bindable var audioManager: AudioManager
    var deviceManager: DeviceManager
    @State private var wantsProcessing = false

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            // Header
            HStack {
                Image(systemName: "waveform.circle.fill")
                    .font(.title2)
                    .foregroundStyle(.blue)
                Text("NoiseAI")
                    .font(.headline)
                Spacer()
                HStack(spacing: 4) {
                    Circle()
                        .fill(audioManager.isProcessing ? .green : .gray)
                        .frame(width: 8, height: 8)
                    Text(audioManager.isProcessing ? "Active" : "Off")
                        .font(.caption)
                        .foregroundStyle(audioManager.isProcessing ? .green : .secondary)
                }
            }

            Divider()

            // Noise cancellation toggle
            Toggle("Noise Cancellation", isOn: $wantsProcessing)
                .toggleStyle(.switch)
                .onChange(of: wantsProcessing) { _, newValue in
                    if newValue {
                        // Start with the selected device
                        let deviceID = deviceManager.selectedDeviceID()
                        audioManager.start(withDeviceID: deviceID)
                    } else {
                        audioManager.stop()
                    }
                }

            // Mode picker
            VStack(alignment: .leading, spacing: 8) {
                Text("Mode")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                Picker("Mode", selection: $audioManager.currentMode) {
                    ForEach(AudioManager.NoiseMode.allCases, id: \.self) { mode in
                        Text(mode.rawValue).tag(mode)
                    }
                }
                .pickerStyle(.segmented)
                .onChange(of: audioManager.currentMode) { _, newValue in
                    audioManager.setMode(newValue)
                    // Persist as default mode
                    UserDefaults.standard.set(newValue.rawValue, forKey: "defaultMode")
                }
            }

            // Device selection
            VStack(alignment: .leading, spacing: 8) {
                Text("Microphone")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)

                if deviceManager.inputDevices.isEmpty {
                    HStack {
                        Image(systemName: "mic.slash")
                            .foregroundStyle(.secondary)
                        Text("No input devices found")
                            .foregroundStyle(.secondary)
                    }
                    .padding(8)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(.quaternary)
                    .clipShape(RoundedRectangle(cornerRadius: 6))
                } else {
                    Picker("Microphone", selection: Binding(
                        get: { deviceManager.selectedDeviceUID ?? "" },
                        set: { newUID in
                            deviceManager.selectDevice(uid: newUID)
                        }
                    )) {
                        ForEach(deviceManager.inputDevices) { device in
                            HStack {
                                Text(device.name)
                                if device.isDefault {
                                    Text("(Default)")
                                        .foregroundStyle(.secondary)
                                }
                            }
                            .tag(device.uid)
                        }
                    }
                    .labelsHidden()
                }
            }

            Divider()

            // Footer
            HStack {
                Button("Settings...") {
                    NSApp.sendAction(Selector(("showSettingsWindow:")), to: nil, from: nil)
                }
                .buttonStyle(.plain)
                .foregroundStyle(.blue)

                Spacer()

                Button("Quit") {
                    NSApplication.shared.terminate(nil)
                }
                .buttonStyle(.plain)
                .foregroundStyle(.secondary)
            }
        }
        .padding(16)
        .frame(width: 300)
    }
}
