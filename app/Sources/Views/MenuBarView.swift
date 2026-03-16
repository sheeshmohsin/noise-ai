import SwiftUI

struct MenuBarView: View {
    @State private var audioManager = AudioManager()
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
                Text(audioManager.isProcessing ? "Active" : "Off")
                    .font(.caption)
                    .foregroundStyle(audioManager.isProcessing ? .green : .secondary)
            }

            Divider()

            // Noise cancellation toggle
            Toggle("Noise Cancellation", isOn: $wantsProcessing)
                .toggleStyle(.switch)
                .onChange(of: wantsProcessing) { _, newValue in
                    if newValue {
                        audioManager.start()
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
                }
            }

            // Device selection placeholder
            VStack(alignment: .leading, spacing: 8) {
                Text("Microphone")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                HStack {
                    Image(systemName: "mic")
                    Text("Built-in Microphone")
                    Spacer()
                    Image(systemName: "chevron.up.chevron.down")
                        .foregroundStyle(.secondary)
                }
                .padding(8)
                .background(.quaternary)
                .clipShape(RoundedRectangle(cornerRadius: 6))
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

#Preview {
    MenuBarView()
}
