import AppKit
import SwiftUI

/// The whole macOS front end as Objective-C++ sees it: a menu bar item, a
/// settings window and a dictation panel over one model.
///
/// Everything below this is Swift-only, which is why this is the single @objc
/// class in the target.
@objc public final class SpeecherMacUI: NSObject {
    private let model: AppModel
    private let panel: SpeecherDictationPanel
    private var menuBar: SpeecherMenuBarExtra!
    private var settings: SpeecherSettingsWindow?

    @MainActor
    @objc public init(bridge: SpeecherBridge) {
        let model = AppModel(bridge: bridge)
        self.model = model
        panel = SpeecherDictationPanel(model: model)
        super.init()
        menuBar = SpeecherMenuBarExtra(model: model,
                                      openSettings: { [weak self] in self?.showSettings() })
        installSettingsMenuItem()
    }

    /// Made on first use: a run that only ever dictates never pays for the
    /// settings window, and the menu bar item is the front door.
    @MainActor
    @objc public func showSettings() {
        if settings == nil {
            settings = SpeecherSettingsWindow(model: model)
        }
        settings?.show()
        NSApp.activate(ignoringOtherApps: true)
    }

    @MainActor
    @objc public func captureSettings(toPath path: String) -> Bool {
        settings?.capture(toPath: path) ?? false
    }

    @MainActor
    @objc public func showDictationProblem(_ message: String) {
        panel.show(problem: message)
    }

    /// Brings Speecher forward so whatever it just put on screen can be seen.
    @MainActor
    @objc public func activate() {
        NSApp.activate(ignoringOtherApps: true)
    }

    /// ⌘, in the application menu, where every Mac app puts its settings. A
    /// SwiftUI `Settings` scene would wire this up for free, but scenes only
    /// exist inside a SwiftUI `App`, and Qt owns this process's NSApplication.
    @MainActor
    private func installSettingsMenuItem() {
        guard let appMenu = NSApp.mainMenu?.item(at: 0)?.submenu else { return }
        let comma = ","
        // Qt reserves a hidden Preferences item in the app menu; using it rather
        // than adding a second one is what keeps ⌘, unambiguous.
        if let reserved = appMenu.items.first(where: { $0.keyEquivalent == comma }) {
            reserved.title = "Settings…"
            reserved.action = #selector(showSettings)
            reserved.target = self
            reserved.isHidden = false
            reserved.isEnabled = true
            return
        }
        let item = NSMenuItem(title: "Settings…", action: #selector(showSettings), keyEquivalent: comma)
        item.target = self
        // After About, which is where the item sits in every other Mac app.
        let index = min(1, appMenu.items.count)
        appMenu.insertItem(item, at: index)
        appMenu.insertItem(.separator(), at: index + 1)
    }
}
