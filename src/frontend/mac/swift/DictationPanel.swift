import AppKit
import Combine
import SwiftUI

// The floating dictation panel: what Speecher is doing right now, over whatever
// is being dictated into. A non-activating NSPanel, so showing it never takes
// focus away from the app the text is going to.

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
            Image(systemName: state.problem.isEmpty ? "mic.fill" : "exclamationmark.triangle.fill")
                .accessibilityLabel(state.problem.isEmpty ? "Dictating" : "Dictation problem")
            VStack(alignment: .leading) {
                Text(state.problem.isEmpty ? state.status : state.problem)
                if !state.preview.isEmpty {
                    Text(state.preview).lineLimit(1)
                }
            }
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
                    .accessibilityLabel("Input level")
            }
        }
        .scenePadding()
        .glassEffect()
    }
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
        panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 420, height: 68),
                        styleMask: [.borderless, .nonactivatingPanel],
                        backing: .buffered,
                        defer: false)
        panel.level = .statusBar
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = false
        panel.animationBehavior = .none
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
