import Foundation

@Observable
class Preferences {
    @ObservationIgnored
    private let defaults = UserDefaults.standard

    var launchAtLogin: Bool {
        get { defaults.bool(forKey: "launchAtLogin") }
        set { defaults.set(newValue, forKey: "launchAtLogin") }
    }

    var defaultMode: String {
        get { defaults.string(forKey: "defaultMode") ?? "Balanced" }
        set { defaults.set(newValue, forKey: "defaultMode") }
    }
}
