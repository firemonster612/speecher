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
    /// Escape abandons the recording rather than becoming the shortcut.
    private let escapeKeyCode: UInt16 = 53

    func record(_ bind: @escaping (String, NSEvent.ModifierFlags) -> Void) {
        stop()
        recording = true
        monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            guard let self else { return event }
            stop()
            if event.keyCode != escapeKeyCode {
                bind(event.charactersIgnoringModifiers ?? "", event.modifierFlags)
            }
            // Swallowed: the keys being recorded are the ones that would
            // otherwise do something.
            return nil
        }
    }

    func stop() {
        if let monitor {
            NSEvent.removeMonitor(monitor)
        }
        monitor = nil
        recording = false
    }

    deinit {
        if let monitor {
            NSEvent.removeMonitor(monitor)
        }
    }
}

struct DictationPane: View {
    @ObservedObject var model: AppModel
    @StateObject private var recorder = ShortcutRecorder()

    var body: some View {
        Form {
            Section {
                LabeledContent("Status") { Text(statusLabel) }
                LabeledContent("Dictation") {
                    Button(model.listening ? "Stop Dictation" : "Start Dictation") {
                        model.bridge.toggle()
                    }
                }
            } header: {
                Text("Current Session")
            }
            Section {
                LabeledContent {
                    Button(caption) {
                        recorder.record { characters, flags in
                            model.bindShortcut(characters: characters, modifierFlags: flags)
                        }
                    }
                    .disabled(!model.shortcutSupported)
                } label: {
                    Text("Dictation shortcut")
                    Text("Hold it to dictate while it is down, or press and release to "
                         + "start and press again to stop.")
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
        if recorder.recording { return "Type a shortcut…" }
        return model.shortcut.isEmpty ? "Record Shortcut" : model.shortcut
    }

    private var statusLabel: String {
        let status = model.status
        return status.isEmpty ? "Idle" : status.prefix(1).uppercased() + status.dropFirst()
    }

    private var footnote: String {
        if !model.shortcutProblem.isEmpty { return model.shortcutProblem }
        if recorder.recording { return "Press the keys you want, or Escape to keep the current one." }
        return "macOS keeps no desktop-wide shortcut registry, so this binding is Speecher's own."
    }
}
