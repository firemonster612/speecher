import AppKit
import SwiftUI

@MainActor
private final class ReopenApplicationDelegate: NSObject, NSApplicationDelegate {
    private let forwardingDelegate: NSApplicationDelegate?
    private let reopen: () -> Void

    init(forwardingTo delegate: NSApplicationDelegate?, reopen: @escaping () -> Void) {
        forwardingDelegate = delegate
        self.reopen = reopen
    }

    func applicationShouldHandleReopen(_ sender: NSApplication,
                                       hasVisibleWindows flag: Bool) -> Bool {
        if !flag {
            reopen()
        }
        // This method's own implementation of the selector is what makes
        // forwardingTarget(for:) never see it, so a delegate Qt installed here
        // needs an explicit call to still hear reopen events.
        return forwardingDelegate?.applicationShouldHandleReopen?(sender, hasVisibleWindows: flag) ?? true
    }

    override func responds(to selector: Selector!) -> Bool {
        super.responds(to: selector)
            || forwardingDelegate?.responds(to: selector) == true
    }

    override func forwardingTarget(for selector: Selector!) -> Any? {
        guard forwardingDelegate?.responds(to: selector) == true else {
            return super.forwardingTarget(for: selector)
        }
        return forwardingDelegate
    }

    /// Hands NSApp.delegate back to whatever this proxy was installed over.
    /// Only if it's still the installed delegate: a later relaunch's proxy may
    /// already have replaced it by the time this one deallocates.
    func restoreIfInstalled() {
        if NSApp.delegate === self {
            NSApp.delegate = forwardingDelegate
        }
    }
}

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
    private var applicationDelegate: ReopenApplicationDelegate?

    @MainActor
    @objc public init(bridge: SpeecherBridge) {
        let model = AppModel(bridge: bridge)
        self.model = model
        panel = SpeecherDictationPanel(model: model)
        super.init()
        menuBar = SpeecherMenuBarExtra(model: model,
                                      openSettings: { [weak self] in self?.showSettings() })
        applicationDelegate = ReopenApplicationDelegate(forwardingTo: NSApp.delegate) {
            [weak self] in self?.showSettings()
        }
        NSApp.delegate = applicationDelegate
        installSettingsMenuItem()
    }

    // NSApplication.delegate is unsafe-unretained, and this object holds the
    // only strong reference to the proxy installed over it. Without this,
    // every setup relaunch and app shutdown leaves NSApp.delegate dangling
    // during Qt's Cocoa teardown.
    deinit {
        MainActor.assumeIsolated {
            applicationDelegate?.restoreIfInstalled()
        }
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
