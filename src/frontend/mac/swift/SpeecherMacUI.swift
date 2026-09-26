import AppKit
import SwiftUI
import UserNotifications

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
    private var setupAssistant: SpeecherSetupAssistant?
    private var applicationDelegate: ReopenApplicationDelegate?

    @MainActor
    @objc public init(bridge: SpeecherBridge) {
        let model = AppModel(bridge: bridge)
        self.model = model
        panel = SpeecherDictationPanel(model: model)
        super.init()
        menuBar = SpeecherMenuBarExtra(model: model,
                                      openSettings: { [weak self] in self?.showSettings() })
        panel.openWhatsNew = { [weak self] in
            self?.showSettings()
            self?.model.showWhatsNew()
        }
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
        model.reloadSettingsDraft()
        if settings == nil {
            settings = SpeecherSettingsWindow(model: model)
        }
        settings?.show()
        NSApp.activate(ignoringOtherApps: true)
    }

    /// Screenshot automation, as on Qt and Windows: SPEECHER_GRAB_PAGE names
    /// the pane to show before the grab.
    @MainActor
    @objc public func captureSettings(toPath path: String) -> Bool {
        if let page = ProcessInfo.processInfo.environment["SPEECHER_GRAB_PAGE"]?.lowercased(),
           let pane = model.panes.first(where: { $0.id.lowercased() == page }) {
            model.pane = pane.id
            RunLoop.main.run(until: Date().addingTimeInterval(0.5))
        }
        return settings?.capture(toPath: path) ?? false
    }

    /// A fresh flow every run: the assistant that was closed mid-way starts
    /// over, which is what a course of checks means. The completion runs when
    /// setup finishes without a relaunch.
    @MainActor
    @objc public func showSetupAssistant(completion: @escaping () -> Void) {
        model.reloadSettingsDraft()
        // The device rows the microphone step offers are the deferred ones.
        model.loadDeferredRows()
        if let setupAssistant {
            setupAssistant.update(onFinished: completion)
        } else {
            setupAssistant = SpeecherSetupAssistant(
                model: model,
                onFinished: completion,
                onClosed: { [weak self] in self?.setupAssistant = nil })
        }
        setupAssistant?.show()
        NSApp.activate(ignoringOtherApps: true)
    }

    /// For the front end's teardown: an assistant left open would outlive the
    /// controller its callbacks reach into.
    @MainActor
    @objc public func dismissSetupAssistant() {
        setupAssistant?.close()
    }

    @MainActor
    @objc public func showDictationProblem(_ message: String) {
        panel.show(problem: message)
    }

    @MainActor
    @objc public func dismissDictationPanel() {
        panel.dismiss()
    }

    @MainActor
    @objc public var dictationPanelVisible: Bool { panel.isVisible }

    @MainActor
    @objc public var dictationPanelLevel: Int { panel.level.rawValue }

    @MainActor
    @objc public var dictationPanelPresentedGeneration: UInt64 { panel.presentedGeneration }

    @MainActor
    @objc public var whatsNewOfferVisible: Bool { model.whatsNewPending }

    /// Whether the settings window is on screen, which is what a restart after
    /// an update puts back.
    @MainActor
    @objc public var settingsWindowVisible: Bool { settings?.isVisible ?? false }

    /// News that arrives while no Speecher window is up, such as a Local
    /// Model download finishing after setup closed, goes to Notification
    /// Center. A window on screen already shows it.
    @MainActor
    @objc public func notifyIfNoWindowShown(title: String, message: String) {
        // Notification Center only takes posts from an app bundle; asking
        // from a bare executable (the tests) throws.
        guard settings?.isVisible != true, setupAssistant?.isVisible != true,
              Bundle.main.bundleURL.pathExtension == "app" else { return }
        let center = UNUserNotificationCenter.current()
        // Closing the last window can leave Speecher the active app, where
        // macOS holds notifications back unless the delegate asks for them.
        center.delegate = ForegroundNotifications.shared
        center.requestAuthorization(options: [.alert, .sound]) { granted, error in
            guard granted else {
                NSLog("Speecher: notifications are not allowed%@",
                      error.map { ": \($0.localizedDescription)" } ?? "")
                return
            }
            let content = UNMutableNotificationContent()
            content.title = title
            content.body = message
            center.add(UNNotificationRequest(identifier: UUID().uuidString, content: content, trigger: nil))
        }
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

/// Shows Speecher's notifications while Speecher is the active app. It only
/// posts when no window of its own is up to show the news instead.
private final class ForegroundNotifications: NSObject, UNUserNotificationCenterDelegate {
    static let shared = ForegroundNotifications()

    func userNotificationCenter(_ center: UNUserNotificationCenter,
                                willPresent notification: UNNotification,
                                withCompletionHandler completionHandler:
                                    @escaping (UNNotificationPresentationOptions) -> Void) {
        completionHandler([.banner, .list, .sound])
    }
}
