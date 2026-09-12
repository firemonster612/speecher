import AppKit
import SwiftUI

// The desktop-wide dictation shortcut. The binder has always been able to
// rebind it; until now only the setup assistant asked.

/// Catches the next key press anywhere in the app, which is what recording a
/// shortcut is. A local event monitor rather than a first-responder view: the
/// combination being recorded is usually one AppKit would otherwise route to a
/// menu, and a monitor sees it before the menu does.
@MainActor
final class ShortcutRecorder: ObservableObject {
    @Published private(set) var recording = false
    private var monitor: Any?
    /// Modifiers seen going down since the unified capture armed (or since the
    /// last non-modifier key), by keyCode so left and right stay distinct. A
    /// lone one commits on its release; a second joining spoils the release.
    private var heldModifiers: Set<UInt16> = []
    private var modifierChordSpoiled = false
    /// Restores the hotkey registration recording suspended. The bound
    /// combination is consumed system-wide while registered, so the monitor
    /// would never see it — pressing it would start dictation instead.
    private var restoreShortcut: (@MainActor @Sendable () -> Void)?
    /// Escape abandons the recording rather than becoming the shortcut.
    private let escapeKeyCode: UInt16 = 53

    /// Catches the next shortcut of either kind: a non-modifier key pressed
    /// with ⌘, ⌥, ⌃ or ⇧ held goes to `combination`; one pressed bare goes to
    /// `singleKey`, as does a lone modifier — committed when its flag clears,
    /// so long as no other key was pressed while it was down (a modifier-only
    /// chord is not a valid shortcut). `singleKey` says whether it took the
    /// key; one it does not know (a media key) leaves the recorder armed.
    /// Escape abandons.
    func record(suspending model: AppModel,
                combination: @escaping (String, NSEvent.ModifierFlags) -> Void,
                singleKey: @escaping (UInt16) -> Bool) {
        begin(suspending: model)
        heldModifiers = []
        modifierChordSpoiled = false
        monitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .flagsChanged]) {
            [weak self] event in
            guard let self else { return event }
            if event.type == .flagsChanged {
                return handleModifier(event, singleKey)
            }
            // A non-modifier key ends any pending lone-modifier capture: the
            // modifiers' releases from here on pass by unrecorded.
            heldModifiers = []
            modifierChordSpoiled = false
            if event.keyCode == escapeKeyCode {
                stop()
                return nil
            }
            if !event.modifierFlags.intersection([.command, .option, .control, .shift]).isEmpty {
                stop()
                combination(event.charactersIgnoringModifiers ?? "", event.modifierFlags)
                return nil
            }
            if singleKey(event.keyCode) { stop() }
            // Swallowed: the keys being recorded are the ones that would
            // otherwise do something.
            return nil
        }
    }

    /// Modifier flag changes are never swallowed — hiding one from AppKit
    /// would desync its idea of what is held.
    private func handleModifier(_ event: NSEvent, _ singleKey: (UInt16) -> Bool) -> NSEvent? {
        // Caps Lock's flag reports the lock state, not the key, so its release
        // edge is unreadable; commit it on either toggle edge instead — but
        // only alone. Pressed with another modifier held, it joins a chord
        // like any other key: no commit, and the held modifier's release must
        // not commit either.
        if event.keyCode == 57 {
            if heldModifiers.isEmpty && !modifierChordSpoiled {
                if singleKey(event.keyCode) { stop() }
            } else {
                modifierChordSpoiled = true
            }
            return event
        }
        if Self.modifierIsDown(event) {
            if !heldModifiers.isEmpty { modifierChordSpoiled = true }
            heldModifiers.insert(event.keyCode)
            return event
        }
        // A release. Only a modifier we saw go down counts; one already held
        // when recording started passes by.
        guard heldModifiers.remove(event.keyCode) != nil else { return event }
        let lone = heldModifiers.isEmpty && !modifierChordSpoiled
        if heldModifiers.isEmpty { modifierChordSpoiled = false }
        if lone, singleKey(event.keyCode) { stop() }
        return event
    }

    /// Whether this flagsChanged event is the press edge of the modifier its
    /// keyCode names, read from the flag rather than assumed from the edge.
    /// The side-specific NX_DEVICE* bits (IOLLEvent.h), as the binder uses:
    /// the family flag would read releasing Left Option while Right Option is
    /// held as a press of the released key.
    private static func modifierIsDown(_ event: NSEvent) -> Bool {
        let bit: UInt
        switch event.keyCode {
        case 54: bit = 0x0000_0010 // NX_DEVICERCMDKEYMASK
        case 55: bit = 0x0000_0008 // NX_DEVICELCMDKEYMASK
        case 56: bit = 0x0000_0002 // NX_DEVICELSHIFTKEYMASK
        case 60: bit = 0x0000_0004 // NX_DEVICERSHIFTKEYMASK
        case 58: bit = 0x0000_0020 // NX_DEVICELALTKEYMASK
        case 61: bit = 0x0000_0040 // NX_DEVICERALTKEYMASK
        case 59: bit = 0x0000_0001 // NX_DEVICELCTLKEYMASK
        case 62: bit = 0x0000_2000 // NX_DEVICERCTLKEYMASK
        case 57: bit = NSEvent.ModifierFlags.capsLock.rawValue
        case 63: bit = NSEvent.ModifierFlags.function.rawValue
        default: return false
        }
        return event.modifierFlags.rawValue & bit != 0
    }

    private func begin(suspending model: AppModel) {
        stop()
        model.beginShortcutRecording()
        restoreShortcut = { model.endShortcutRecording() }
        recording = true
    }

    func stop() {
        if let monitor {
            NSEvent.removeMonitor(monitor)
        }
        monitor = nil
        recording = false
        restoreShortcut?()
        restoreShortcut = nil
    }

    deinit {
        if let monitor {
            NSEvent.removeMonitor(monitor)
        }
        // Deinitialization can run outside the main actor. Capture the cleanup
        // rather than the dying recorder, and restore on the actor it requires.
        if let restoreShortcut {
            Task { @MainActor in restoreShortcut() }
        }
    }
}

struct ShortcutPane: View {
    @ObservedObject var model: AppModel
    @StateObject private var recorder = ShortcutRecorder()
    /// A key the recorder caught but could not bind (a media key); shown in
    /// the footer while the recorder stays armed.
    @State private var captureProblem = ""

    var body: some View {
        Form {
            Section {
                LabeledContent {
                    Button(caption) {
                        captureProblem = ""
                        recorder.record(suspending: model, combination: { characters, flags in
                            model.bindShortcut(characters: characters, modifierFlags: flags)
                        }, singleKey: { keyCode in
                            if model.bindSingleKey(macKeyCode: keyCode) { return true }
                            captureProblem = "That key cannot be a dictation key."
                            return false
                        })
                    }
                    .disabled(!model.shortcutSupported)
                } label: {
                    Text("Dictation shortcut")
                    Text("Press a key combination, or a single key such as "
                         + "Right Option or F13.")
                }
                if model.shortcutNeedsAccessibility, !model.accessibilityEnabled {
                    Button("Grant Accessibility Access") { model.requestAccessibility() }
                }
            } header: {
                Text("Shortcut")
            } footer: {
                Text(footnote)
            }
        }
        .formStyle(.grouped)
        .onDisappear { recorder.stop() }
    }

    private var caption: String {
        if recorder.recording { return "Press a key or key combination…" }
        return model.shortcut.isEmpty ? "Set shortcut" : model.shortcut
    }

    private var footnote: String {
        if recorder.recording {
            if !captureProblem.isEmpty {
                return captureProblem + " Try another, or press Escape to keep the current one."
            }
            return "Press the keys you want — a bare modifier like Right Option works — "
                + "or Escape to keep the current one."
        }
        if !model.shortcutProblem.isEmpty { return model.shortcutProblem }
        if !model.shortcutWarning.isEmpty { return model.shortcutWarning }
        return "macOS keeps no desktop-wide shortcut registry, so this binding is Speecher's own."
    }
}
