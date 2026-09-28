import AppKit
import Combine
import SwiftUI

// Speecher's front door: a mic in the menu bar, and behind it a panel that says
// what dictation is doing, starts and stops it, offers the last transcript back,
// and gets to the settings. Dictation status lives here rather than on a
// settings pane, because a settings window is not something you keep open.

/// The panel behind the menu bar item. A popover, so it takes the system's own
/// glass and dismissal behaviour; scenePadding is the sanctioned way to inset a
/// pane's content without naming a number.
struct MenuBarPanel: View {
    @ObservedObject var model: AppModel
    let openSettings: () -> Void

    var body: some View {
        VStack(alignment: .leading) {
            Label(model.status, systemImage: model.listening ? "mic.fill" : "mic")
                .font(.headline)
            if model.listening {
                // A level meter is not progress towards anything, so it is a
                // gauge rather than a progress view, and its label is hidden
                // because the mic beside it already says what it measures.
                Gauge(value: Double(min(max(model.level, 0), 1))) { EmptyView() }
                    .gaugeStyle(.linearCapacity)
                    .accessibilityLabel("Input level")
            }
            // Labelled and enabled by what toggle() would actually do: it
            // cancels a refinement, and does nothing during stopping/delivering.
            Button(model.toggleLabel) {
                model.bridge.toggle()
            }
            .buttonStyle(.borderedProminent)
            .disabled(!model.toggleEnabled)
            if model.accessibilitySupported && !model.accessibilityEnabled {
                Divider()
                accessibilityNotice
            }
            Divider()
            if model.transcript.isEmpty {
                Text(model.bridge.noTranscriptYetText)
            } else {
                Text(model.transcript)
                    .foregroundStyle(.secondary)
                    .lineLimit(3)
                Button(model.bridge.copyTranscriptCaption, systemImage: "doc.on.doc") { model.copyTranscript() }
            }
            Divider()
            LabeledContent("Shortcut") {
                Text(model.shortcut.isEmpty ? "None" : model.shortcut)
            }
            Button(model.bridge.traySettingsCaption) { openSettings() }
            Button(model.bridge.trayQuitCaption) { model.bridge.quit() }
        }
        // Every button spans the panel, which is what Apple's own example for
        // this modifier is for: buttons in a narrow context.
        .buttonSizing(.flexible)
        .scenePadding()
        // The panel's width, which a popover has to be told: the transcript
        // would otherwise make the panel as wide as whatever was dictated.
        .frame(maxWidth: 300, alignment: .leading)
    }

    /// Where a permission the app is missing belongs: beside the button that
    /// would be affected by it, rather than in a settings pane nobody has open
    /// or an alert nobody asked for. macOS grants are permanent once given, so
    /// "off" is the only state worth saying anything about.
    @ViewBuilder private var accessibilityNotice: some View {
        Label("Without Accessibility, dictation only reaches the clipboard.",
              systemImage: "exclamationmark.triangle")
            // A sentence in a Label truncates to one line unless it is told it
            // may grow downwards.
            .fixedSize(horizontal: false, vertical: true)
        Button("Open Privacy & Security…") { model.requestAccessibility() }
        if !model.accessibilityProblem.isEmpty {
            Text(model.accessibilityProblem)
        }
    }
}

/// The status item and its popover.
@MainActor
final class SpeecherMenuBarExtra: NSObject {
    private static let fallbackContentSize = NSSize(width: 300, height: 320)
    private static let idleIconSize = NSSize(width: 18, height: 18)

    private let model: AppModel
    private let item: NSStatusItem
    private let popover = NSPopover()
    private var stateObserver: AnyCancellable?

    init(model: AppModel, openSettings: @escaping () -> Void) {
        self.model = model
        item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        super.init()
        popover.behavior = .transient
        // No background of ours: a popover carries the system's material, and
        // adding one over it is what the Liquid Glass audit asks you to remove.
        let hostingController = NSHostingController(
            rootView: MenuBarPanel(model: model, openSettings: { [weak self] in
                self?.popover.performClose(nil)
                openSettings()
            }))
        hostingController.sizingOptions = [.preferredContentSize]
        popover.contentViewController = hostingController
        item.button?.action = #selector(togglePanel)
        item.button?.target = self
        symbol(listening: model.listening)
        // The item has to say what dictation is doing even while every window
        // is shut, which is the whole reason it exists.
        stateObserver = model.$listening.sink { [weak self] listening in
            self?.symbol(listening: listening)
        }
    }

    private func symbol(listening: Bool) {
        guard let button = item.button else { return }
        if listening {
            button.image = NSImage(systemSymbolName: "mic.fill",
                                   accessibilityDescription: "Speecher is listening")
        } else {
            // A status item with no image is invisible, so a bundle resource
            // that fails to load must fall back to a symbol, never to nothing.
            let image = Bundle.main.image(forResource: "speecher-menubar")
                ?? NSImage(systemSymbolName: "mic", accessibilityDescription: "Speecher")
            image?.size = Self.idleIconSize
            button.image = image
            button.image?.accessibilityDescription = "Speecher"
        }
        button.image?.isTemplate = true
    }

    @objc private func togglePanel() {
        guard let button = item.button else { return }
        if popover.isShown {
            popover.performClose(nil)
        } else {
            if popover.contentSize.width <= 0 || popover.contentSize.height <= 0 {
                popover.contentSize = Self.fallbackContentSize
            }
            popover.show(relativeTo: button.bounds, of: button, preferredEdge: .minY)
        }
    }
}
