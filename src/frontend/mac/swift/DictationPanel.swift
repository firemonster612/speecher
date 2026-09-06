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

/// Scratch-branch-only E2E seam: every panel callback lands as a JSON line in
/// SPEECHER_E2E_EVIDENCE_DIR/panel-events.jsonl for the harness to assert on.
private enum E2EPanelEvidence {
    static func record(_ event: String, _ value: String = "") {
        guard let path = ProcessInfo.processInfo.environment["SPEECHER_E2E_EVIDENCE_DIR"],
              !path.isEmpty else { return }
        let directory = URL(fileURLWithPath: path, isDirectory: true)
        try? FileManager.default.createDirectory(at: directory,
                                                 withIntermediateDirectories: true)
        let object: [String: Any] = [
            "ts": Int64(Date().timeIntervalSince1970 * 1000),
            "event": event,
            "value": value,
        ]
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
}

private struct DictationPanelGlass: ViewModifier {
    @ViewBuilder
    func body(content: Content) -> some View {
        if #available(macOS 26.0, *) {
            content.glassEffect(in: .capsule)
        } else {
            content
        }
    }
}

@MainActor
final class DictationPanelState: ObservableObject {
    /// After the mic stops the panel walks Transcribing then Refining; the live
    /// speech preview only belongs to `live`, exactly as on the Qt popup.
    enum Phase { case live, transcribing, refining }

    @Published var status = ""
    @Published var preview = ""
    @Published var level: Float = 0
    @Published var phase = Phase.live
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
            if finished {
                // The delivery outcome takes the whole pill, centred as one
                // icon-and-text group (and one VoiceOver element); the
                // transcript and the live level bar left with the live audio.
                Label(state.status, systemImage: symbol)
                    .imageScale(.large)
                    .font(.body)
                    .lineLimit(1)
                    .frame(maxWidth: .infinity)
            } else if state.problem.isEmpty, let waiting = waitingLabel {
                // The provider is finalising or the refiner has not streamed a
                // word yet: a shimmering label where the preview was, and no
                // trailing control — the sweep already says work is under way,
                // and the mic-level Gauge would be a meter over a closed mic.
                Image(systemName: symbol)
                    .imageScale(.large)
                    .accessibilityLabel(phaseLabel)
                ShimmerText(text: waiting)
                    .frame(maxWidth: .infinity, alignment: .leading)
            } else {
                Image(systemName: symbol)
                    .imageScale(.large)
                    .accessibilityLabel(phaseLabel)
                // The words are the point of the panel, so they get the only line
                // of type in it, and a problem takes that line rather than a
                // second one.
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
                } else if state.phase == .refining {
                    // A spinner beside the streamed text, because refinement has
                    // no measurable end, and no label because it appeared when
                    // the work started.
                    ProgressView().controlSize(.small)
                } else {
                    Gauge(value: Double(min(max(state.level, 0), 1))) { EmptyView() }
                        .gaugeStyle(.linearCapacity)
                        .frame(width: 96)
                        .accessibilityLabel("Input level")
                }
            }
        }
        .scenePadding()
        .frame(height: pillHeight)
        .background(.regularMaterial, in: .capsule)
        .modifier(DictationPanelGlass())
    }

    /// One symbol and one label per phase, from the same mapping, so what a
    /// sighted user sees and what VoiceOver reads for it never disagree — the
    /// panel can present before the first status lands, when state.status is
    /// still empty. The session ends a delivery on a free-form outcome from the
    /// delivery back end ("Copied to clipboard"), so an unrecognised non-empty
    /// status is a finished one.
    private var phase: (symbol: String, label: String, finished: Bool) {
        if !state.problem.isEmpty {
            return ("exclamationmark.triangle.fill", "Dictation problem", false)
        }
        switch state.status.lowercased() {
        case "", "preparing", "starting":
            return ("arrow.triangle.2.circlepath", state.status.isEmpty ? "Dictating" : state.status, false)
        case "listening": return ("mic.fill", state.status, false)
        case "stopping": return ("waveform", state.status, false)
        case "refining": return ("sparkles", state.status, false)
        // Set by the OAuth refresh callback in wire(): ongoing work, not an
        // outcome, so it must not present as a finished delivery.
        case "refreshing sign-in…":
            return ("arrow.triangle.2.circlepath", state.status, false)
        default: return ("paperplane.fill", state.status, true)
        }
    }

    private var symbol: String { phase.symbol }

    /// Whether the dictation has ended in an outcome ("Input sent") rather
    /// than a live phase, so the panel shows the receipt and nothing live.
    private var finished: Bool { phase.finished }

    /// The phase in words, for the screen reader that can't see the symbol.
    private var phaseLabel: String { phase.label }

    /// The shimmer's label while there is nothing to show where the preview
    /// goes: the provider is still turning audio into words, or the refiner
    /// has not streamed any yet.
    private var waitingLabel: String? {
        switch state.phase {
        case .transcribing: return "Transcribing…"
        case .refining: return state.preview.isEmpty ? "Refining…" : nil
        case .live: return nil
        }
    }
}

/// Dimmed text with a looping highlight sweep, this panel's version of the Qt
/// popup's status shimmer. Driven by TimelineView rather than a repeating
/// SwiftUI animation so every rendered frame carries the sweep's position.
private struct ShimmerText: View {
    let text: String
    private let loop: TimeInterval = 1.5

    var body: some View {
        TimelineView(.animation) { context in
            let progress = context.date.timeIntervalSinceReferenceDate
                .truncatingRemainder(dividingBy: loop) / loop
            Text(text)
                .font(.body)
                .lineLimit(1)
                .foregroundStyle(.secondary)
                .overlay(
                    Text(text)
                        .font(.body)
                        .lineLimit(1)
                        .mask(alignment: .leading) {
                            GeometryReader { geometry in
                                let width = geometry.size.width
                                LinearGradient(stops: [.init(color: .clear, location: 0),
                                                       .init(color: .white, location: 0.5),
                                                       .init(color: .clear, location: 1)],
                                               startPoint: .leading,
                                               endPoint: .trailing)
                                    .frame(width: width / 2)
                                    // From fully off the leading edge to fully
                                    // off the trailing one, then around again.
                                    .offset(x: width * 1.5 * progress - width / 2)
                            }
                        }
                )
        }
    }
}

@MainActor
final class SpeecherDictationPanel {
    private let state = DictationPanelState()
    private let bridge: SpeecherBridge
    private let panel: NSPanel
    private var frozen = false
    private var e2eFrameIndex = 0
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
        installE2ECaptureSeam()
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
        bridge.popupStatusChanged = { [weak self] status in
            guard let self else { return }
            E2EPanelEvidence.record("status", status)
            state.status = status
            // The mic is closed but the provider is still finalising, so the
            // shimmer takes the line and the stale speech preview goes away.
            if status == "Stopping" {
                state.phase = .transcribing
                state.preview = ""
            }
        }
        bridge.popupPreviewChanged = { [weak self] preview in self?.setPreview(preview) }
        bridge.popupFrozenChanged = { [weak self] frozen in
            guard let self else { return }
            self.frozen = frozen
            if !frozen { state.phase = .live }
        }
        bridge.popupRefiningChanged = { [weak self] refining in
            guard let self else { return }
            E2EPanelEvidence.record("refining", refining ? "true" : "false")
            if refining {
                state.phase = .refining
                state.preview = ""
            } else {
                state.phase = .live
            }
        }
        bridge.popupRefinementPreviewChanged = { [weak self] preview in
            guard let self, state.phase == .refining else { return }
            E2EPanelEvidence.record("refinement-preview", preview)
            applyPreview(preview)
        }
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
            self?.state.problem = ""
            self?.show(generation: generation)
        }
        bridge.popupHideRequested = { [weak self] in self?.panel.orderOut(nil) }
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
        state.phase = .live
        present()
    }

    func dismiss() {
        state.problem = ""
        panel.orderOut(nil)
        bridge.stopListening()
    }

    var isVisible: Bool { panel.isVisible }
    var level: NSWindow.Level { panel.level }

    private func setPreview(_ preview: String) {
        guard !frozen, state.phase == .live else { return }
        applyPreview(preview)
    }

    /// The one line of type and the pill's width around it, shared by the live
    /// speech preview and the streamed refinement text.
    private func applyPreview(_ preview: String) {
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

    /// Scratch-branch-only E2E seam: with SPEECHER_E2E_PANEL_CAPTURE_DIR set,
    /// the visible panel's backing store lands there ten times a second as
    /// numbered PNGs, which needs no screen-recording grant on a CI runner.
    private func installE2ECaptureSeam() {
        guard let dir = ProcessInfo.processInfo.environment["SPEECHER_E2E_PANEL_CAPTURE_DIR"],
              !dir.isEmpty else { return }
        try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
        Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            DispatchQueue.main.async { self?.captureE2EFrame(into: dir) }
        }
    }

    private func captureE2EFrame(into dir: String) {
        guard panel.isVisible,
              let view = panel.contentView,
              let bitmap = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { return }
        view.cacheDisplay(in: view.bounds, to: bitmap)
        guard let png = bitmap.representation(using: .png, properties: [:]) else { return }
        e2eFrameIndex += 1
        let path = String(format: "%@/frame-%06d.png", dir, e2eFrameIndex)
        try? png.write(to: URL(fileURLWithPath: path), options: .atomic)
    }

    /// The panel belongs on the display the user is working on, which on a
    /// multi-display Mac is often not the primary one.
    private func present() {
        position()
        panel.orderFrontRegardless()
    }

    private func position() {
        let pointer = NSEvent.mouseLocation
        let screen = NSScreen.screens.first { $0.visibleFrame.contains(pointer) }
            ?? NSScreen.main
            ?? NSScreen.screens.first
        if let area = screen?.visibleFrame {
            let size = panel.frame.size
            panel.setFrameOrigin(NSPoint(x: area.midX - size.width / 2,
                                         y: area.minY + bottomMargin))
        }
    }
}
