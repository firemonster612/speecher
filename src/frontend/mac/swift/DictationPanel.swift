import AppKit
import Combine
import SwiftUI

// The floating dictation panel: what Speecher is doing right now, over whatever
// is being dictated into. A non-activating NSPanel, so showing it never takes
// focus away from the app the text is going to.

/// The pill's height, for the window that is created at it and the content that
/// fills it, so that the capsule is the whole window. Left to size itself the
/// content came out between 59 and 61pt depending on which trailing control was
/// showing, which moved the pill's edges as the dictation changed phase.
private let pillHeight: CGFloat = 72
private let minimumPillWidth: CGFloat = 420
private let previewChromeWidth: CGFloat = 190
private let screenEdgeMargin: CGFloat = 80

private enum E2EPanelEvidence {
    private static var directory: URL? {
        guard let path = ProcessInfo.processInfo.environment["SPEECHER_E2E_EVIDENCE_DIR"],
              !path.isEmpty else { return nil }
        return URL(fileURLWithPath: path, isDirectory: true)
    }

    static func record(_ event: String,
                       generation: UInt64 = 0,
                       blockWasNil: Bool = false,
                       extra: [String: Any] = [:]) {
        guard let directory else { return }
        try? FileManager.default.createDirectory(at: directory,
                                                 withIntermediateDirectories: true)
        let timestamp = Int64(Date().timeIntervalSince1970 * 1000)
        var object: [String: Any] = [
            "ts": timestamp,
            "event": event,
            "generation": generation,
            "blockWasNil": blockWasNil,
        ]
        object.merge(extra) { _, new in new }
        guard var data = try? JSONSerialization.data(withJSONObject: object) else { return }
        data.append(0x0a)
        let url = directory.appendingPathComponent("panel-events.jsonl")
        if !FileManager.default.fileExists(atPath: url.path) {
            FileManager.default.createFile(atPath: url.path, contents: nil)
        }
        guard let file = try? FileHandle(forWritingTo: url) else { return }
        defer { try? file.close() }
        try? file.seekToEnd()
        try? file.write(contentsOf: data)
    }

    static func dumpWindows(_ event: String) {
        guard let directory,
              let all = CGWindowListCopyWindowInfo(.optionAll, kCGNullWindowID)
                as? [[String: Any]] else { return }
        let pid = ProcessInfo.processInfo.processIdentifier
        let windows = all.filter {
            ($0[kCGWindowOwnerPID as String] as? NSNumber)?.int32Value == pid
        }.map { info in
            [
                "bounds": info[kCGWindowBounds as String] ?? NSNull(),
                "layer": info[kCGWindowLayer as String] ?? NSNull(),
                "alpha": info[kCGWindowAlpha as String] ?? NSNull(),
                "isOnscreen": info[kCGWindowIsOnscreen as String] ?? false,
                "ownerPID": info[kCGWindowOwnerPID as String] ?? pid,
            ] as [String: Any]
        }
        try? FileManager.default.createDirectory(at: directory,
                                                 withIntermediateDirectories: true)
        let timestamp = Int64(Date().timeIntervalSince1970 * 1000)
        let url = directory.appendingPathComponent("windows-\(event)-\(timestamp).json")
        if let data = try? JSONSerialization.data(withJSONObject: windows,
                                                  options: [.prettyPrinted, .sortedKeys]) {
            try? data.write(to: url)
        }
    }
}

@MainActor
final class DictationPanelState: ObservableObject {
    @Published var status = ""
    @Published var preview = ""
    @Published var level: Float = 0
    @Published var refining = false
    @Published var problem = ""
}

/// One glass pill. This is the one place in this front end that asks for Liquid
/// Glass by hand, because it is the one genuinely floating element: everything
/// else is a stock sidebar, form or popover that already carries it.
struct DictationPanelView: View {
    @ObservedObject var state: DictationPanelState
    let dismiss: () -> Void

    var body: some View {
        HStack {
            Image(systemName: symbol)
                .imageScale(.large)
                .accessibilityLabel(phaseLabel)
            // The words are the point of the panel, so they get the only line of
            // type in it, and a problem takes that line rather than a second one.
            Text(state.problem.isEmpty ? state.preview : state.problem)
                .font(.body)
                .lineLimit(1)
                .multilineTextAlignment(.leading)
                .frame(maxWidth: .infinity, alignment: .leading)
                .layoutPriority(1)
                // A live transcript overflows from the front: the words the
                // user just said must always be the visible end.
                .truncationMode(state.problem.isEmpty ? .head : .tail)
            if !state.problem.isEmpty {
                Button("Dismiss", action: dismiss)
            } else if state.refining {
                // A spinner, because refinement has no measurable end, and no
                // label because it appeared when the work started.
                ProgressView().controlSize(.small)
            } else {
                Gauge(value: Double(min(max(state.level, 0), 1))) { EmptyView() }
                    .gaugeStyle(.linearCapacity)
                    .frame(width: 96)
                    .accessibilityLabel("Input level")
            }
        }
        .scenePadding()
        .frame(height: pillHeight)
        .background(.regularMaterial, in: .capsule)
        .glassEffect(in: .capsule)
    }

    /// One symbol and one label per phase, from the same mapping, so what a
    /// sighted user sees and what VoiceOver reads for it never disagree — the
    /// panel can present before the first status lands, when state.status is
    /// still empty. The session ends a delivery on a free-form outcome from the
    /// delivery back end ("Copied to clipboard"), so an unrecognised non-empty
    /// status is a finished one.
    private var phase: (symbol: String, label: String) {
        if !state.problem.isEmpty {
            return ("exclamationmark.triangle.fill", "Dictation problem")
        }
        switch state.status.lowercased() {
        case "", "preparing", "starting":
            return ("arrow.triangle.2.circlepath", state.status.isEmpty ? "Dictating" : state.status)
        case "listening": return ("mic.fill", state.status)
        case "stopping": return ("waveform", state.status)
        case "refining": return ("sparkles", state.status)
        default: return ("paperplane.fill", state.status)
        }
    }

    private var symbol: String { phase.symbol }

    /// The phase in words, for the screen reader that can't see the symbol.
    private var phaseLabel: String { phase.label }
}

@MainActor
final class SpeecherDictationPanel {
    private let state = DictationPanelState()
    private let bridge: SpeecherBridge
    private let panel: NSPanel
    private var frozen = false
    private(set) var presentedGeneration: UInt64 = 0
    private var levelObserver: AnyCancellable?
    private var screenObserver: AnyCancellable?
    /// The panel is 28pt above the bottom of the screen it sits on, which is
    /// where the Qt popup put itself and where the eye expects it.
    private let bottomMargin: CGFloat = 28

    init(model: AppModel) {
        bridge = model.bridge
        // Non-activating is the whole point, and only an NSPanel accepts that
        // style mask. Borderless because the pill is the window: a titlebar
        // over a floating status readout would be chrome nobody asked for.
        panel = NSPanel(contentRect: NSRect(x: 0, y: 0,
                                            width: minimumPillWidth,
                                            height: pillHeight),
                        styleMask: [.borderless, .nonactivatingPanel],
                        backing: .buffered,
                        defer: false)
        panel.level = .statusBar
        panel.isReleasedWhenClosed = false
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = true
        panel.animationBehavior = .utilityWindow
        panel.hidesOnDeactivate = false
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .stationary]
        panel.contentView = NSHostingView(rootView: DictationPanelView(state: state) { [weak self] in
            self?.dismiss()
        })
        wire()
        // The level arrives through the model, which is the one reader of the
        // bridge's audio callback: two readers of one block would mean the
        // second one silently replaced the first.
        levelObserver = model.$level.sink { [weak self] level in self?.state.level = level }
        screenObserver = NotificationCenter.default
            .publisher(for: NSApplication.didChangeScreenParametersNotification)
            .sink { [weak self] _ in
                guard self?.panel.isVisible == true else { return }
                self?.position()
            }
    }

    private func wire() {
        E2EPanelEvidence.record("swift-wire")
        bridge.popupStatusChanged = { [weak self] status in self?.state.status = status }
        bridge.popupPreviewChanged = { [weak self] preview in self?.setPreview(preview) }
        bridge.popupFrozenChanged = { [weak self] frozen in self?.frozen = frozen }
        bridge.popupRefiningChanged = { [weak self] refining in self?.state.refining = refining }
        bridge.popupOAuthRefreshRequested = { [weak self] in
            self?.state.status = "Refreshing sign-in…"
            self?.state.preview = "Refreshing sign-in…"
        }
        bridge.popupListeningIndicatorRequested = { [weak self] in
            self?.state.status = "Listening"
        }
        bridge.popupErrorRequested = { [weak self] message in
            self?.show(problem: message)
        }
        bridge.popupShowRequested = { [weak self] generation in
            E2EPanelEvidence.record("show", generation: generation)
            self?.state.problem = ""
            self?.show(generation: generation)
            E2EPanelEvidence.dumpWindows("show")
        }
        bridge.popupHideRequested = { [weak self] in
            E2EPanelEvidence.record("hide")
            self?.panel.orderOut(nil)
            E2EPanelEvidence.dumpWindows("hide")
        }
    }

    func show(generation: UInt64) {
        present()
        // The session waits out a 50ms fallback otherwise; telling it the panel
        // is up lets the microphone open as soon as the frame is on screen.
        DispatchQueue.main.async { [weak self, bridge] in
            guard let self else { return }
            presentedGeneration = generation
            bridge.notePopupPresented(generation: generation)
        }
    }

    func show(problem: String) {
        // The problem takes the one line of type the pill has, so the transcript
        // of the attempt that failed goes with it rather than lingering in the
        // state for the next show to flash.
        state.preview = ""
        state.problem = problem
        E2EPanelEvidence.record("error")
        present()
        E2EPanelEvidence.dumpWindows("error")
    }

    func dismiss() {
        state.problem = ""
        panel.orderOut(nil)
        bridge.stopListening()
    }

    var isVisible: Bool { panel.isVisible }
    var level: NSWindow.Level { panel.level }

    private func setPreview(_ preview: String) {
        guard !frozen else { return }
        state.preview = preview
        let font = NSFont.systemFont(ofSize: NSFont.systemFontSize)
        let textWidth = (preview as NSString).size(withAttributes: [.font: font]).width
        let availableWidth = (panel.screen ?? NSScreen.main)?.visibleFrame.width
            ?? minimumPillWidth + screenEdgeMargin
        let maximumWidth = max(minimumPillWidth, availableWidth - screenEdgeMargin)
        let width = min(max(minimumPillWidth, textWidth + previewChromeWidth), maximumWidth)
        guard abs(panel.frame.width - width) >= 1 else { return }

        var frame = panel.frame
        frame.origin.x -= (width - frame.width) / 2
        frame.size.width = width
        panel.setFrame(frame, display: true)
    }

    /// The panel belongs on the display the user is working on, which on a
    /// multi-display Mac is often not the primary one.
    private func present() {
        position()
        panel.orderFrontRegardless()
        E2EPanelEvidence.record("presented", extra: [
            "activationPolicy": NSApp.activationPolicy().rawValue,
            "isVisible": panel.isVisible,
            "level": panel.level.rawValue,
            "panelFrame": NSStringFromRect(panel.frame),
        ])
    }

    private func position() {
        let pointer = NSEvent.mouseLocation
        let screen = NSScreen.screens.first { $0.visibleFrame.contains(pointer) }
            ?? NSScreen.main
            ?? NSScreen.screens.first
        E2EPanelEvidence.record("position", extra: [
            "pointer": NSStringFromPoint(pointer),
            "chosenScreen": screen.map { NSStringFromRect($0.visibleFrame) } ?? "",
            "screens": NSScreen.screens.map { NSStringFromRect($0.visibleFrame) },
        ])
        if let area = screen?.visibleFrame {
            let size = panel.frame.size
            panel.setFrameOrigin(NSPoint(x: area.midX - size.width / 2,
                                         y: area.minY + bottomMargin))
        }
    }
}
