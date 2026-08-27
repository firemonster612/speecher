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
                // A live transcript overflows from the front: the words the
                // user just said must always be the visible end.
                .truncationMode(state.problem.isEmpty ? .head : .tail)
            Spacer()
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
    private var levelObserver: AnyCancellable?
    /// The panel is 28pt above the bottom of the screen it sits on, which is
    /// where the Qt popup put itself and where the eye expects it.
    private let bottomMargin: CGFloat = 28

    init(model: AppModel) {
        bridge = model.bridge
        // Non-activating is the whole point, and only an NSPanel accepts that
        // style mask. Borderless because the pill is the window: a titlebar
        // over a floating status readout would be chrome nobody asked for.
        panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 420, height: pillHeight),
                        styleMask: [.borderless, .nonactivatingPanel],
                        backing: .buffered,
                        defer: false)
        panel.level = .statusBar
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = true
        panel.animationBehavior = .utilityWindow
        panel.isFloatingPanel = true
        panel.hidesOnDeactivate = false
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .stationary]
        panel.contentView = NSHostingView(rootView: DictationPanelView(state: state) { [bridge] in
            bridge.stopListening()
        })
        wire()
        // The level arrives through the model, which is the one reader of the
        // bridge's audio callback: two readers of one block would mean the
        // second one silently replaced the first.
        levelObserver = model.$level.sink { [weak self] level in self?.state.level = level }
    }

    private func wire() {
        bridge.popupStatusChanged = { [weak self] status in self?.state.status = status }
        bridge.popupPreviewChanged = { [weak self] preview in self?.state.preview = preview }
        bridge.popupRefiningChanged = { [weak self] refining in self?.state.refining = refining }
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
        DispatchQueue.main.async { [bridge] in
            bridge.notePopupPresented(generation: generation)
        }
    }

    func show(problem: String) {
        // The problem takes the one line of type the pill has, so the transcript
        // of the attempt that failed goes with it rather than lingering in the
        // state for the next show to flash.
        state.preview = ""
        state.problem = problem
        present()
    }

    /// The panel belongs on the display the user is working on, which on a
    /// multi-display Mac is often not the primary one.
    private func present() {
        let pointer = NSEvent.mouseLocation
        let screen = NSScreen.screens.first { $0.frame.contains(pointer) } ?? NSScreen.main
        if let area = screen?.visibleFrame {
            let size = panel.frame.size
            panel.setFrameOrigin(NSPoint(x: area.midX - size.width / 2,
                                         y: area.minY + bottomMargin))
        }
        panel.orderFrontRegardless()
    }
}
