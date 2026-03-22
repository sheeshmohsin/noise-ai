import SwiftUI
import ServiceManagement

struct SettingsView: View {
    @AppStorage("defaultMode") private var defaultMode = "Balanced"
    @State private var launchAtLogin: Bool = {
        if #available(macOS 14.0, *) {
            return SMAppService.mainApp.status == .enabled
        }
        return false
    }()

    var body: some View {
        Form {
            Section("General") {
                Toggle("Launch at login", isOn: $launchAtLogin)
                    .onChange(of: launchAtLogin) { _, newValue in
                        setLaunchAtLogin(newValue)
                    }
            }

            Section("Audio") {
                Picker("Default Mode", selection: $defaultMode) {
                    ForEach(AudioManager.NoiseMode.allCases, id: \.rawValue) { mode in
                        Text(mode.rawValue).tag(mode.rawValue)
                    }
                }
            }

            Section("About") {
                LabeledContent("Version", value: appVersion)
                LabeledContent("Build", value: appBuild)
                LabeledContent("Engine", value: "ONNX Runtime (CPU)")
            }
        }
        .formStyle(.grouped)
        .frame(width: 400, height: 300)
    }

    // MARK: - Launch at Login

    private func setLaunchAtLogin(_ enabled: Bool) {
        do {
            if enabled {
                try SMAppService.mainApp.register()
            } else {
                try SMAppService.mainApp.unregister()
            }
        } catch {
            print("[NoiseAI] Failed to \(enabled ? "register" : "unregister") login item: \(error)")
            // Revert toggle on failure
            launchAtLogin = !enabled
        }
    }

    // MARK: - App Info

    private var appVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0.1.0"
    }

    private var appBuild: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "1"
    }
}

#Preview {
    SettingsView()
}
