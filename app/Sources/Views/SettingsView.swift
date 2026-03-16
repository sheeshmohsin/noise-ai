import SwiftUI

struct SettingsView: View {
    @AppStorage("autoStart") private var autoStart = true
    @AppStorage("selectedMode") private var selectedMode = "Balanced"

    var body: some View {
        Form {
            Section("General") {
                Toggle("Launch at login", isOn: $autoStart)
            }

            Section("Audio") {
                Picker("Default Mode", selection: $selectedMode) {
                    Text("CPU Saver").tag("CPU Saver")
                    Text("Balanced").tag("Balanced")
                    Text("Max Quality").tag("Max Quality")
                }
            }

            Section("About") {
                LabeledContent("Version", value: "0.1.0")
                LabeledContent("Engine", value: "ONNX Runtime (CPU)")
            }
        }
        .formStyle(.grouped)
        .frame(width: 400, height: 300)
    }
}

#Preview {
    SettingsView()
}
