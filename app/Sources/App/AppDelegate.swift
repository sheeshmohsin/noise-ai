import AppKit
import SwiftUI

class AppDelegate: NSObject, NSApplicationDelegate {
    private var statusItem: NSStatusItem?
    private var popover: NSPopover?

    // Shared instances
    let audioManager = AudioManager()
    let deviceManager = DeviceManager()

    func applicationDidFinishLaunching(_ notification: Notification) {
        // Set as accessory app (no dock icon)
        NSApp.setActivationPolicy(.accessory)

        // Wire device manager to audio manager
        deviceManager.onDeviceSelectionChanged = { [weak self] deviceID in
            self?.audioManager.switchDevice(to: deviceID)
        }

        // Restore the selected device on the audio manager
        if let deviceID = deviceManager.selectedDeviceID() {
            audioManager.switchDevice(to: deviceID)
        }

        // Restore default mode from preferences
        let savedMode = UserDefaults.standard.string(forKey: "defaultMode") ?? "Balanced"
        if let mode = AudioManager.NoiseMode(rawValue: savedMode) {
            audioManager.setMode(mode)
        }

        // Create status bar item
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)

        if let button = statusItem?.button {
            button.image = NSImage(systemSymbolName: "mic.slash", accessibilityDescription: "NoiseAI")
            button.image?.isTemplate = true
            button.action = #selector(togglePopover)
            button.target = self
        }

        // Create popover with shared managers
        let popover = NSPopover()
        popover.contentSize = NSSize(width: 300, height: 400)
        popover.behavior = .transient
        popover.contentViewController = NSHostingController(
            rootView: MenuBarView(audioManager: audioManager, deviceManager: deviceManager)
        )
        self.popover = popover
    }

    @objc private func togglePopover() {
        guard let popover = popover, let button = statusItem?.button else { return }

        if popover.isShown {
            popover.performClose(nil)
        } else {
            popover.show(relativeTo: button.bounds, of: button, preferredEdge: .minY)
            // Ensure the popover's window becomes key
            popover.contentViewController?.view.window?.makeKey()
        }
    }

    /// Update the menu bar icon based on processing state
    func updateStatusIcon(isActive: Bool) {
        guard let button = statusItem?.button else { return }
        let symbolName = isActive ? "mic.fill" : "mic.slash"
        button.image = NSImage(systemSymbolName: symbolName, accessibilityDescription: "NoiseAI")
        button.image?.isTemplate = true
    }
}
