import AppKit
import Combine
import SwiftUI

// The floating dictation panel: what Speecher is doing right now, over whatever
// is being dictated into. A non-activating NSPanel, so showing it never takes
// focus away from the app the text is going to.

/// The popup's measurements, which the Linux and Windows popups share.
private typealias PopupGeometry = SpeecherPopupGeometry
/// The error capsule's corners, which it keeps as a wrapped problem makes it
/// taller; the pill, shorter than twice this, is a stadium.
private let capsuleCornerRadius: CGFloat = 24
/// The error capsule's shortest, its own on every platform.
private let problemMinimumHeight: CGFloat = 48
/// The dots' own width, which the strip shrinks to beside the buttons.
private let waveformInkWidth = CGFloat(PopupGeometry.barCount) * PopupGeometry.barWidth
    + CGFloat(PopupGeometry.barCount - 1) * PopupGeometry.barGap
/// The widest text bar: the longest preview line and the margins beside it.
private let maximumPreviewBarWidth = PopupGeometry.maxPreviewWidth + 2 * PopupGeometry.previewSideMargin
private let screenEdgeMargin: CGFloat = 80
/// The preview line, the status and the receipt: the system font scaled as
/// on every platform. Measured and drawn with the same font.
private var popupTextFont: NSFont {
    NSFont.systemFont(ofSize: NSFont.systemFontSize * PopupGeometry.previewFontScale)
}
private var popupFont: Font { Font(popupTextFont as CTFont) }
/// The update and what's-new banners stacked above the pill.
private let bannerHeight: CGFloat = 36
private let bannerSpacing: CGFloat = 8

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

/// The preview bar and its compact status strip share one outline.
private struct PanelContour: Shape {
    var shoulder: CGFloat
    var lobeWidth: CGFloat

    func path(in rect: CGRect) -> Path {
        let cap = shoulder / 2
        let half = lobeWidth / 2
        let left = rect.midX - half
        let right = rect.midX + half
        let lobeHeight = rect.height - shoulder
        var fillet = min(PopupGeometry.fillet, left - rect.minX - cap)
        var radius = min(PopupGeometry.lobeRadius, half)
        if fillet + radius > lobeHeight {
            let scale = lobeHeight / (fillet + radius)
            fillet *= scale
            radius *= scale
        }
        guard shoulder > 0, lobeHeight > 0, fillet >= 4 else {
            let radius = min(capsuleCornerRadius, rect.height / 2)
            return Path(roundedRect: rect, cornerSize: CGSize(width: radius, height: radius))
        }
        let top = rect.minY
        let shelf = top + shoulder
        var path = Path()
        path.move(to: CGPoint(x: rect.minX + cap, y: top))
        path.addLine(to: CGPoint(x: rect.maxX - cap, y: top))
        path.addArc(center: CGPoint(x: rect.maxX - cap, y: top + cap), radius: cap,
                    startAngle: .degrees(-90), endAngle: .degrees(90), clockwise: false)
        path.addLine(to: CGPoint(x: right + fillet, y: shelf))
        path.addArc(center: CGPoint(x: right + fillet, y: shelf + fillet), radius: fillet,
                    startAngle: .degrees(-90), endAngle: .degrees(-180), clockwise: true)
        path.addLine(to: CGPoint(x: right, y: rect.maxY - radius))
        path.addArc(center: CGPoint(x: right - radius, y: rect.maxY - radius), radius: radius,
                    startAngle: .degrees(0), endAngle: .degrees(90), clockwise: false)
        path.addLine(to: CGPoint(x: left + radius, y: rect.maxY))
        path.addArc(center: CGPoint(x: left + radius, y: rect.maxY - radius), radius: radius,
                    startAngle: .degrees(90), endAngle: .degrees(180), clockwise: false)
        path.addLine(to: CGPoint(x: left, y: shelf + fillet))
        path.addArc(center: CGPoint(x: left - fillet, y: shelf + fillet), radius: fillet,
                    startAngle: .degrees(0), endAngle: .degrees(-90), clockwise: true)
        path.addLine(to: CGPoint(x: rect.minX + cap, y: shelf))
        path.addArc(center: CGPoint(x: rect.minX + cap, y: top + cap), radius: cap,
                    startAngle: .degrees(90), endAngle: .degrees(270), clockwise: false)
        path.closeSubpath()
        return path
    }
}

private struct DictationPanelBackground: View {
    let shape: PanelContour
    let cornerRadius: CGFloat

    @ViewBuilder
    var body: some View {
        // Liquid Glass cannot render the concave preview contour reliably.
        // Only the background branches, so waveform state survives preview changes.
        if #available(macOS 26.0, *), shape.shoulder == 0 {
            RoundedRectangle(cornerRadius: cornerRadius).fill(.regularMaterial)
                .glassEffect(in: .rect(cornerRadius: cornerRadius))
        } else {
            shape.fill(.regularMaterial)
        }
    }
}

@MainActor
final class DictationPanelState: ObservableObject {
    /// After the mic stops the panel walks Transcribing then Refining; the live
    /// speech preview only belongs to `live`, exactly as on the Qt popup.
    enum Phase { case live, transcribing, refining }

    @Published var status = ""
    /// The session state the status was reported with, which is what the
    /// panel decides on rather than the status's words.
    @Published var sessionState = SpeecherDictationState.idle
    @Published var preview = "" {
        didSet { if preview != oldValue { trimPreview() } }
    }
    @Published var level: Float = 0
    @Published var phase = Phase.live
    @Published var frozen = false
    @Published var pillWidth: CGFloat = PopupGeometry.pillHeight {
        didSet { if pillWidth != oldValue { trimPreview() } }
    }
    /// The preview as the line shows it: its newest words, the oldest cut
    /// from the front when they do not fit. Trimmed once per preview or
    /// width, not on every render.
    @Published private(set) var shownPreview = ""
    /// Words were cut from the preview's front.
    @Published private(set) var previewCut = false
    /// The buttons either side of the waveform (speecher::sessionControls).
    @Published var pauseVisible = false
    @Published var pauseEnabled = false
    @Published var paused = false
    @Published var cancelVisible = false
    /// A spinner in pause's place while transcribing and refining.
    @Published var busyVisible = false
    /// Test seam: where the processing row's spinner, status and cancel
    /// button were laid out, in the window's coordinates; empty while not
    /// showing.
    var busyFrame = CGRect.zero
    var statusFrame = CGRect.zero
    var cancelFrame = CGRect.zero
    var outcomeFixFrame = CGRect.zero
    var waveformFloor: Float = 0
    @Published var problem = ""
    /// What the problem offers to fix it; nil when it offers nothing.
    @Published var problemFix: SpeecherErrorAction?
    /// When a problem appeared and when it dismisses itself, for its countdown.
    @Published var problemCountdown = Date.distantPast...Date.distantPast
    /// The share of the countdown left while the pointer holds it; nil while
    /// it runs.
    @Published var problemPausedAt: Double?
    /// The problem's height once wrapped at the shared width.
    @Published var problemHeight: CGFloat = problemMinimumHeight
    /// How the delivery ended, once it has; nil while the session is live.
    @Published var outcome: SpeecherPopupOutcome? {
        didSet { if outcome == nil { outcomeFix = nil } }
    }
    /// What a finished delivery offers to fix, such as the expired sign-in a
    /// fallback stood in for; nil when it offers nothing. Its outcome wraps
    /// at the shared width, as a problem does, to the height below.
    @Published var outcomeFix: SpeecherErrorAction?
    @Published var outcomeHeight = PopupGeometry.pillHeight
    /// The update banner's message, empty while there is nothing to offer, and
    /// the label of the button beside it, empty for a passive progress state.
    @Published var updateMessage = ""
    @Published var updateAction = ""
    @Published var updateActionEnabled = true
    /// The what's-new banner's message, empty once hidden or dismissed.
    @Published var whatsNewMessage = ""

    var presentation: (symbol: String, label: String, finished: Bool) {
        if !problem.isEmpty {
            return ("exclamationmark.triangle.fill", SpeecherBridge.statusLabel(for: .error), false)
        }
        if let outcome {
            return (Self.symbol(for: outcome), status, true)
        }
        // Set by the OAuth refresh callback in wire(): ongoing work, not an
        // outcome, so it must not present as a finished delivery.
        if status == SpeecherBridge.renewingSignInText {
            return ("arrow.triangle.2.circlepath", status, false)
        }
        let label = SpeecherBridge.statusLabel(for: sessionState)
        switch sessionState {
        case .idle, .starting:
            return ("arrow.triangle.2.circlepath", SpeecherBridge.statusLabel(for: .starting), false)
        case .listening: return ("mic.fill", label, false)
        case .refining: return ("sparkles", label, false)
        default: return ("waveform", label, false)
        }
    }

    /// The receipt's symbol: sent into the Target, left on the clipboard, or
    /// cancelled.
    static func symbol(for outcome: SpeecherPopupOutcome) -> String {
        switch outcome {
        case .inserted: return "paperplane.fill"
        case .copied: return "doc.on.clipboard"
        case .fallback: return "info.circle"
        case .error: return "exclamationmark.triangle.fill"
        case .cancelled: return "xmark.circle"
        @unknown default: return "info.circle"
        }
    }

    var finished: Bool { presentation.finished }

    var showsPreview: Bool { problem.isEmpty && !finished && !preview.isEmpty }
    /// Words were cut from the preview's front, so its start fades out.
    var previewFades: Bool { showsPreview && previewCut }

    private func trimPreview() {
        let line = SpeecherBridge.trimPreview(
            preview, toWidth: pillWidth - 2 * PopupGeometry.previewSideMargin, font: popupTextFont)
        shownPreview = line.text
        previewCut = line.cut
    }

    var waitingLabel: String? {
        if status == SpeecherBridge.renewingSignInText { return status }
        switch phase {
        case .transcribing: return SpeecherBridge.statusLabel(for: .stopping)
        case .refining: return SpeecherBridge.statusLabel(for: .refining)
        case .live: return nil
        }
    }

    /// The buttons show while the session is live, never over a problem or a
    /// receipt.
    var showsControls: Bool {
        problem.isEmpty && !finished && (pauseVisible || busyVisible || cancelVisible)
    }
    var controlsWidth: CGFloat {
        guard showsControls else { return 0 }
        let slot = PopupGeometry.buttonSize + PopupGeometry.buttonGap
        return (pauseVisible || busyVisible ? slot : 0) + (cancelVisible ? slot : 0)
    }
    /// Paused keeps the bars, flat and still in the caution colour.
    var showsPaused: Bool { showsControls && paused }

    var lineHeight: CGFloat {
        let font = popupTextFont
        return ceil(font.ascender - font.descender + font.leading)
    }
    /// The text bar down to where the lobe leaves it.
    var shoulderHeight: CGFloat {
        PopupGeometry.previewTopMargin + lineHeight + PopupGeometry.shoulderDrop
    }
    var stripHeight: CGFloat {
        guard showsPreview else { return PopupGeometry.pillHeight }
        // Room for the buttons as well as the bars or the label.
        return showsControls ? max(PopupGeometry.compactStripHeight, PopupGeometry.buttonSize)
            : PopupGeometry.compactStripHeight
    }
    /// A problem, or an outcome with a fix, wraps rather than keeping to the
    /// pill's one line.
    var wraps: Bool { !problem.isEmpty || (finished && outcomeFix != nil) }
    var height: CGFloat {
        if !problem.isEmpty { return problemHeight }
        if wraps { return outcomeHeight }
        return showsPreview
            ? PopupGeometry.previewTopMargin + lineHeight + PopupGeometry.previewStripSpacing
                + stripHeight + PopupGeometry.previewBottomMargin
            : PopupGeometry.pillHeight
    }
    /// The strip's row: the buttons, the gaps beside them and the dots or the
    /// label.
    var inkWidth: CGFloat {
        guard let label = waitingLabel else { return waveformInkWidth + controlsWidth }
        return ceil((label as NSString).size(withAttributes: [.font: popupTextFont]).width)
            + controlsWidth
    }
    /// The row and the air before the rounded ends around it: the lobe under
    /// words and the whole pill without them. A bare pill's ends are its own
    /// caps.
    var lobeWidth: CGFloat {
        inkWidth + 2 * (showsControls || showsPreview ? PopupGeometry.lobeAir
            : PopupGeometry.pillHeight / 2)
    }
    /// However few the words, the text bar stays wide enough to carve the
    /// contour around the lobe rather than becoming a plain rounded box.
    var minimumPreviewBarWidth: CGFloat {
        PopupGeometry.minimumPreviewBarWidth(lobeWidth: lobeWidth, shoulderHeight: shoulderHeight)
    }
}

/// One banner capsule above the pill, on the pill's own material: a plain
/// message with an explicitly labelled accent button beside it, so the action
/// reads as a button rather than asking the user to guess that a colored
/// capsule is clickable, exactly as on the Qt popup. Passive progress states
/// pass no label and get no button.
private struct PanelBanner: View {
    let message: String
    var actionLabel = ""
    var actionEnabled = true
    var action: () -> Void = {}
    var dismiss: (() -> Void)? = nil
    var dismissLabel = ""

    var body: some View {
        HStack(spacing: 10) {
            Text(message)
                .font(.callout)
                .lineLimit(1)
            if !actionLabel.isEmpty {
                Button(actionLabel, action: action)
                    .buttonStyle(.borderedProminent)
                    .buttonBorderShape(.capsule)
                    .disabled(!actionEnabled)
            }
            if let dismiss {
                Button(action: dismiss) {
                    Image(systemName: "xmark")
                        .imageScale(.small)
                }
                .buttonStyle(.borderedProminent)
                .buttonBorderShape(.circle)
                .accessibilityLabel(dismissLabel)
            }
        }
        .padding(.leading, 16)
        .padding(.trailing, 5)
        .frame(height: bannerHeight)
        .background(.regularMaterial, in: .capsule)
    }
}

/// Waveform and latest words share one capsule on the platform material.
struct DictationPanelView: View {
    @ObservedObject var state: DictationPanelState
    let dismiss: () -> Void
    var performFix: (SpeecherErrorAction) -> Void = { _ in }
    let installUpdate: () -> Void
    let openWhatsNew: () -> Void
    let dismissWhatsNew: () -> Void
    var togglePause: () -> Void = {}
    var cancelSession: () -> Void = {}
    var whatsNew = SpeecherWhatsNewBanner.preview(forVersion: "")

    var body: some View {
        VStack(spacing: bannerSpacing) {
            // What's-new above the update offer, the order the Qt and Windows
            // panels stack them in.
            if !state.whatsNewMessage.isEmpty {
                PanelBanner(message: state.whatsNewMessage,
                            actionLabel: whatsNew.action,
                            action: openWhatsNew,
                            dismiss: dismissWhatsNew,
                            dismissLabel: whatsNew.dismiss)
            }
            if !state.updateMessage.isEmpty {
                PanelBanner(message: state.updateMessage,
                            actionLabel: state.updateAction,
                            actionEnabled: state.updateActionEnabled,
                            action: installUpdate)
            }
            pill
        }
        .frame(maxHeight: .infinity, alignment: .bottom)
    }

    private var pill: some View {
        let shape = PanelContour(shoulder: state.showsPreview ? state.shoulderHeight : 0,
                                 lobeWidth: state.lobeWidth)
        return VStack(spacing: 0) {
            if state.showsPreview {
                Text(state.shownPreview)
                    .font(popupFont)
                    .lineLimit(1)
                    // Words cut from the front fade in from the text's own
                    // left edge; the newest stay fully opaque.
                    .mask(alignment: .leading) {
                        HStack(spacing: 0) {
                            LinearGradient(colors: [state.previewFades ? .clear : .black, .black],
                                           startPoint: .leading, endPoint: .trailing)
                                .frame(width: PopupGeometry.previewFadeWidth)
                            Color.black
                        }
                    }
                    .frame(height: state.lineHeight)
                    .padding(.horizontal, PopupGeometry.previewSideMargin)
                    .padding(.top, PopupGeometry.previewTopMargin)
            }
            HStack(spacing: state.showsControls ? PopupGeometry.buttonGap : 10) {
                if !state.problem.isEmpty {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .symbolRenderingMode(.multicolor)
                    Text(state.problem)
                        .font(.body)
                        .multilineTextAlignment(.center)
                        .fixedSize(horizontal: false, vertical: true)
                        .frame(maxWidth: .infinity)
                    if let fix = state.problemFix {
                        Button(fix.label) { performFix(fix) }
                    }
                    Button(SpeecherBridge.popupDismissCaption, action: dismiss)
                } else if let fix = state.outcomeFix, finished {
                    Label(state.status, systemImage: symbol)
                        .font(popupFont)
                        .fixedSize(horizontal: false, vertical: true)
                        .frame(maxWidth: .infinity, alignment: .leading)
                    Button(fix.label) { performFix(fix) }
                        .reportingFrame { state.outcomeFixFrame = $0 }
                } else if finished {
                    Label(state.status, systemImage: symbol)
                        .font(popupFont)
                        .lineLimit(1)
                } else {
                    if state.showsControls, state.pauseVisible {
                        sessionButton(state.paused ? SpeecherBridge.resumeCaption : SpeecherBridge.pauseCaption,
                                      symbol: state.paused ? "play.fill" : "pause.fill",
                                      action: togglePause)
                            .disabled(!state.pauseEnabled)
                    } else if state.showsControls, state.busyVisible {
                        // In pause's place, so the status sits between two
                        // circles of the same size.
                        ProgressView()
                            .progressViewStyle(.circular)
                            .controlSize(.small)
                            .frame(width: PopupGeometry.buttonSize, height: PopupGeometry.buttonSize)
                            .reportingFrame { state.busyFrame = $0 }
                    }
                    if let waiting = state.waitingLabel {
                        ShimmerText(text: waiting)
                            .fixedSize()
                            .reportingFrame { state.statusFrame = $0 }
                    } else {
                        DotWaveform(level: state.$level, frozen: state.frozen, paused: state.showsPaused,
                                    barCount: PopupGeometry.barCount, barWidth: PopupGeometry.barWidth,
                                    barGap: PopupGeometry.barGap, dotHeight: PopupGeometry.barDotHeight,
                                    width: waveformInkWidth, height: state.stripHeight,
                                    floor: $state.waveformFloor)
                            .accessibilityElement(children: .ignore)
                            .accessibilityLabel(state.showsPaused ? SpeecherBridge.statusLabel(for: .paused) : phaseLabel)
                            .accessibilityValue(Text(Double(state.level), format: .percent.precision(.fractionLength(0))))
                            .help(state.showsPaused ? SpeecherBridge.statusLabel(for: .paused) : "")
                    }
                    if state.showsControls, state.cancelVisible {
                        sessionButton(SpeecherBridge.cancelCaption, symbol: "xmark", action: cancelSession)
                            .reportingFrame { state.cancelFrame = $0 }
                    }
                }
            }
            .padding(.horizontal, !state.problem.isEmpty || finished ? 24 : 0)
            .frame(height: state.wraps ? nil : state.stripHeight)
            .padding(.vertical, state.wraps ? 10 : 0)
            .padding(.top, state.showsPreview ? PopupGeometry.previewStripSpacing : 0)
            .padding(.bottom, state.showsPreview ? PopupGeometry.previewBottomMargin : 0)
            if !state.problem.isEmpty {
                // The time left before the problem dismisses itself, held
                // while the pointer is over the panel.
                Group {
                    if let left = state.problemPausedAt {
                        ProgressView(value: left)
                    } else {
                        ProgressView(timerInterval: state.problemCountdown, countsDown: true) {
                            EmptyView()
                        } currentValueLabel: {
                            EmptyView()
                        }
                    }
                }
                .progressViewStyle(.linear)
                .padding(.horizontal, 24)
                .padding(.bottom, 10)
            }
        }
        .frame(width: state.pillWidth, height: state.height)
        .background(DictationPanelBackground(shape: shape,
                                             cornerRadius: min(capsuleCornerRadius, state.height / 2)))
    }

    private var symbol: String { state.presentation.symbol }

    /// A round button beside the waveform, the same size on both sides; the
    /// panel never becomes key, so clicking it leaves the Target focused.
    private func sessionButton(_ caption: String, symbol: String, action: @escaping () -> Void) -> some View {
        // A titled Label shown icon-only, as Home's buttons do, and named
        // outright so VoiceOver and automation never read the symbol's name.
        Button(caption, systemImage: symbol, action: action)
            .labelStyle(.iconOnly)
            .accessibilityLabel(caption)
            .buttonStyle(.bordered)
            .buttonBorderShape(.circle)
            .controlSize(.regular)
            .font(.system(size: PopupGeometry.buttonIconSize))
            .frame(width: PopupGeometry.buttonSize, height: PopupGeometry.buttonSize)
            .help(caption)
    }

    /// Whether the dictation has ended in an outcome ("Input sent") rather
    /// than a live phase, so the panel shows the receipt and nothing live.
    private var finished: Bool { state.finished }

    /// The phase in words, for the screen reader that can't see the symbol.
    private var phaseLabel: String { state.presentation.label }
}

/// The Linux waveform's dots, adaptive level and one-second travelling crest,
/// as the panel and Home draw them. Paused, the row lies flat and still in the
/// system's caution colour.
struct DotWaveform: View {
    let level: Published<Float>.Publisher
    var frozen = false
    var paused = false
    /// Home's row by default: fifteen dots in a 126-point frame. The popup
    /// passes its own, fewer and finer.
    var barCount = 15
    var barWidth: CGFloat = 3.2
    var barGap: CGFloat = 3.2
    var dotHeight: CGFloat = 3.2
    var width: CGFloat = 126
    var height: CGFloat
    /// The quietest level heard, which the bars' range starts from; its owner
    /// keeps it so it outlives this view.
    @Binding var floor: Float
    /// The bars still follow the voice, but no crest travels across them.
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var sum: Float = 0
    @State private var chunks = 0
    @State private var target: Float = 0
    @State private var smoothed: Float = 0
    @State private var windowStart = Date.now
    @State private var lastFrame = Date.now
    @State private var phase: Double = 0
    @State private var timer = Timer.publish(every: 0.016, on: .main, in: .common).autoconnect()

    var body: some View {
        Canvas { context, size in
            let inkWidth = CGFloat(barCount) * barWidth + CGFloat(barCount - 1) * barGap
            for index in 0..<barCount {
                let bulge = paused ? 1 : 1 - abs(Double(barCount - 1) / 2 - Double(index)) / 24
                let offset = phase - Double(index) / Double(barCount)
                let wave = reduceMotion || paused ? 1 : multiplier(offset - Foundation.floor(offset))
                let scale = paused ? 1 : Double(max(1, smoothed * 5))
                let height = dotHeight * scale * bulge * wave
                let rect = CGRect(x: (size.width - inkWidth) / 2 + Double(index) * (barWidth + barGap),
                                  y: (size.height - height) / 2, width: barWidth, height: height)
                context.fill(Path(roundedRect: rect, cornerSize: CGSize(width: barWidth / 4, height: height / 4)),
                             with: .foreground)
            }
        }
        .foregroundStyle(paused ? Color(nsColor: .systemOrange) : Color.primary)
        .opacity(frozen && !paused ? 0.4 : 1)
        .frame(width: width, height: height)
        .onReceive(level) { value in
            var mapped: Float = 0
            if value > 0 {
                let db = 20 * log10(value)
                floor = max(-46, min(floor, db))
                mapped = min(1, max(0, (db - floor) / 20))
            }
            sum += mapped
            chunks += 1
        }
        .onReceive(timer) { now in
            let elapsed = min(0.1, max(0, now.timeIntervalSince(lastFrame)))
            lastFrame = now
            guard !frozen, !paused else { return }
            phase = (phase + elapsed).truncatingRemainder(dividingBy: 1)
            if now.timeIntervalSince(windowStart) >= 0.15 {
                if chunks > 0 { target = sum / Float(chunks) }
                sum = 0
                chunks = 0
                windowStart = windowStart.addingTimeInterval(0.15)
                if now.timeIntervalSince(windowStart) >= 0.15 { windowStart = now }
            }
            smoothed = Foundation.floor((smoothed * 0.85 + target * 0.15) * 100) / 100
        }
    }

    private func multiplier(_ phase: Double) -> Double {
        let frames: [(Double, Double)] = [(0, 1), (0.2, 1.2), (0.4, 1.5),
                                          (0.8, 1.1), (0.9, 1.3), (1, 1)]
        for index in 1..<frames.count where phase <= frames[index].0 {
            let (start, from) = frames[index - 1]
            let (end, to) = frames[index]
            let progress = (phase - start) / (end - start)
            var t = progress
            for _ in 0..<6 {
                let inverse = 1 - t
                let x = 1.26 * t * inverse * inverse + 1.74 * t * t * inverse + t * t * t
                let dx = 1.26 * (1 - 4 * t + 3 * t * t) + 1.74 * (2 * t - 3 * t * t) + 3 * t * t
                guard dx > 0 else { break }
                t = min(1, max(0, t - (x - progress) / dx))
            }
            return from + (to - from) * t * t * (3 - 2 * t)
        }
        return 1
    }
}

private extension View {
    /// Reports where this view was laid out, in the window's coordinates, and
    /// an empty rect once it goes.
    func reportingFrame(_ report: @escaping (CGRect) -> Void) -> some View {
        onGeometryChange(for: CGRect.self) { $0.frame(in: .global) } action: { report($0) }
            .onDisappear { report(.zero) }
    }
}

/// Dimmed text with a looping highlight sweep, this panel's version of the Qt
/// popup's status shimmer. Driven by TimelineView rather than a repeating
/// SwiftUI animation so every rendered frame carries the sweep's position.
private struct ShimmerText: View {
    let text: String
    private let loop: TimeInterval = 1.5
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        if reduceMotion {
            Text(text)
                .font(popupFont)
                .lineLimit(1)
                .foregroundStyle(.secondary)
        } else {
            sweep
        }
    }

    private var sweep: some View {
        TimelineView(.animation) { context in
            let progress = context.date.timeIntervalSinceReferenceDate
                .truncatingRemainder(dividingBy: loop) / loop
            Text(text)
                .font(popupFont)
                .lineLimit(1)
                .foregroundStyle(.secondary)
                .overlay(
                    // The highlight is only drawn: VoiceOver reads the one
                    // text beneath it.
                    Text(text)
                        .font(popupFont)
                        .lineLimit(1)
                        .accessibilityHidden(true)
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
    private let model: AppModel
    private let bridge: SpeecherBridge
    private let panel: NSPanel
    private var frozen = false
    private var e2eFrameIndex = 0
    private(set) var presentedGeneration: UInt64 = 0
    private var levelObserver: AnyCancellable?
    private var screenObserver: AnyCancellable?
    private var updateObserver: AnyCancellable?
    private var whatsNewObserver: AnyCancellable?
    private var whatsNewAutoHide: Timer?
    /// A problem tidies itself away after the time core gives its length; the
    /// Dismiss button remains the early way out, and the pointer holds it.
    private var problemTick: Timer?
    /// Scratch-branch-only E2E seam: pins both notices on so the capture rig
    /// can film how they stack above the pill. A CI run has no update pending,
    /// so the stack is otherwise never on screen to photograph.
    private let e2eBanners = ProcessInfo.processInfo
        .environment["SPEECHER_E2E_PANEL_BANNERS"] == "1"
    /// Opens the settings window on the What's New pane. Set by SpeecherMacUI,
    /// which owns that window.
    var openWhatsNew: (() -> Void)?
    /// The panel is 28pt above the bottom of the screen it sits on, which is
    /// where the Qt popup put itself and where the eye expects it.
    private let bottomMargin: CGFloat = 28

    init(model: AppModel) {
        self.model = model
        bridge = model.bridge
        // Non-activating is the whole point, and only an NSPanel accepts that
        // style mask. Borderless because the pill is the window: a titlebar
        // over a floating status readout would be chrome nobody asked for.
        panel = NSPanel(contentRect: NSRect(x: 0, y: 0,
                                            width: PopupGeometry.pillHeight,
                                            height: PopupGeometry.pillHeight),
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
        panel.contentView = NSHostingView(rootView: DictationPanelView(
            state: state,
            dismiss: { [weak self] in self?.dismiss() },
            performFix: { [weak self] fix in self?.perform(fix) },
            installUpdate: { [weak self] in self?.bridge.runUpdateAction() },
            openWhatsNew: { [weak self] in self?.openWhatsNew?() },
            dismissWhatsNew: { [weak self] in
                self?.setWhatsNewMessage("")
                self?.bridge.clearPendingWhatsNew()
            },
            togglePause: { [weak self] in self?.model.togglePause() },
            cancelSession: { [weak self] in self?.bridge.cancel() },
            whatsNew: model.whatsNewBanner))
        wire()
        installE2ECaptureSeam()
        // The level arrives through the model, which is the one reader of the
        // bridge's audio callback: two readers of one block would mean the
        // second one silently replaced the first.
        levelObserver = model.$level.sink { [weak self] level in self?.state.level = level }
        updateObserver = model.$update.sink { [weak self] update in
            self?.refreshUpdateBanner(update)
        }
        // A what's-new offer dismissed from the settings window leaves here too.
        whatsNewObserver = model.$whatsNewPending.sink { [weak self] pending in
            if !pending {
                self?.setWhatsNewMessage("")
            }
        }
        screenObserver = NotificationCenter.default
            .publisher(for: NSApplication.didChangeScreenParametersNotification)
            .sink { [weak self] _ in
                guard self?.panel.isVisible == true else { return }
                self?.position()
            }
    }

    private func wire() {
        bridge.popupStatusChanged = { [weak self] status, sessionState in
            guard let self else { return }
            E2EPanelEvidence.record("status", status)
            // A live state follows any earlier receipt.
            state.outcome = nil
            state.status = status
            if state.sessionState != sessionState, sessionState != .error, sessionState != .idle {
                announce(SpeecherBridge.statusLabel(for: sessionState))
            }
            state.sessionState = sessionState
            let controls = SpeecherBridge.sessionControls(for: sessionState)
            state.pauseVisible = controls.pauseVisible
            state.pauseEnabled = controls.pauseEnabled
            state.paused = controls.paused
            state.cancelVisible = controls.cancelVisible
            state.busyVisible = controls.busyVisible
            // The mic is closed but the provider is still finalising, so the
            // shimmer takes the line and the stale speech preview goes away.
            if sessionState == .stopping {
                state.phase = .transcribing
                state.preview = ""
            }
            syncFrameHeight()
        }
        bridge.popupMessageRequested = { [weak self] message, outcome, fix in
            guard let self else { return }
            // The receipt is also a status, which the E2E flow waits for.
            E2EPanelEvidence.record("status", message)
            state.outcome = outcome
            state.outcomeFix = fix.fix == .none ? nil : fix
            state.status = message
            announce(message)
            syncFrameHeight()
        }
        bridge.popupPreviewChanged = { [weak self] preview in self?.setPreview(preview) }
        bridge.popupFrozenChanged = { [weak self] frozen in
            guard let self else { return }
            self.frozen = frozen
            state.frozen = frozen
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
            syncFrameHeight()
        }
        bridge.popupRefinementPreviewChanged = { [weak self] preview in
            guard let self, state.phase == .refining else { return }
            E2EPanelEvidence.record("refinement-preview", preview)
            applyPreview(preview)
        }
        bridge.popupOAuthRefreshRequested = { [weak self] in
            self?.state.phase = .live
            self?.state.status = SpeecherBridge.renewingSignInText
            self?.state.preview = ""
            self?.syncFrameHeight()
        }
        bridge.popupListeningIndicatorRequested = { [weak self] in
            self?.state.phase = .live
            self?.state.sessionState = .listening
            self?.state.status = SpeecherBridge.statusLabel(for: .listening)
            self?.syncFrameHeight()
        }
        bridge.popupErrorRequested = { [weak self] message, fix in
            self?.model.noteFailure(fix: fix)
            self?.show(problem: message, fix: fix)
        }
        bridge.popupShowRequested = { [weak self] generation in
            self?.state.problem = ""
            self?.state.outcome = nil
            self?.show(generation: generation)
        }
        bridge.popupHideRequested = { [weak self] in
            guard let self else { return }
            // A stale problem timer must not fire into whatever shows next.
            problemTick?.invalidate()
            problemTick = nil
            panel.orderOut(nil)
        }
    }

    func show(generation: UInt64) {
        // A dictation starting inside a problem's five seconds must not be
        // torn down when that problem's timer fires.
        problemTick?.invalidate()
        problemTick = nil
        present()
        // The session waits out a 50ms fallback otherwise; telling it the panel
        // is up lets the microphone open as soon as the frame is on screen.
        DispatchQueue.main.async { [weak self, bridge] in
            guard let self else { return }
            presentedGeneration = generation
            bridge.notePopupPresented(generation: generation)
        }
    }

    func show(problem: String, fix: SpeecherErrorAction? = nil) {
        // The problem takes the one line of type the pill has, so the transcript
        // of the attempt that failed goes with it rather than lingering in the
        // state for the next show to flash.
        state.preview = ""
        state.outcome = nil
        let seconds = SpeecherBridge.popupErrorDismissSeconds(for: problem)
        state.problemCountdown = Date.now...Date.now.addingTimeInterval(seconds)
        state.problemPausedAt = nil
        state.problemFix = fix?.fix == SpeecherErrorFix.none ? nil : fix
        state.problem = problem
        applyPreview("")
        state.phase = .live
        present()
        announce(problem)
        problemTick?.invalidate()
        problemTick = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { [weak self] _ in
            DispatchQueue.main.async { self?.tickProblem() }
        }
    }

    /// The pointer over the panel holds a problem's countdown where it is, so
    /// the text and its button can be read; leaving lets the rest run. The
    /// tick reads the pointer rather than waiting for tracking events, which
    /// never come when the problem appears under a pointer that is not moving.
    /// invalidate() cannot recall a tick already queued, so a dictation that
    /// started in the meantime would be torn down by the previous problem's
    /// countdown; a problem is cleared before such a session shows, which
    /// says so.
    private func tickProblem() {
        guard !state.problem.isEmpty else { return }
        holdProblem(panel.frame.contains(NSEvent.mouseLocation))
        if state.problemPausedAt == nil, state.problemCountdown.upperBound <= Date.now {
            dismiss()
        }
    }

    private func holdProblem(_ inside: Bool) {
        let countdown = state.problemCountdown
        let total = countdown.upperBound.timeIntervalSince(countdown.lowerBound)
        guard total > 0 else { return }
        if inside {
            guard state.problemPausedAt == nil else { return }
            state.problemPausedAt = max(0, countdown.upperBound.timeIntervalSinceNow) / total
        } else if let left = state.problemPausedAt {
            let remaining = left * total
            state.problemCountdown = Date.now.addingTimeInterval(remaining - total)...Date.now.addingTimeInterval(remaining)
            state.problemPausedAt = nil
        }
    }

    /// Says a change aloud: the panel never takes focus, so VoiceOver would
    /// otherwise not notice it.
    private func announce(_ text: String) {
        guard !text.isEmpty else { return }
        NSAccessibility.post(element: NSApp as Any,
                             notification: .announcementRequested,
                             userInfo: [.announcement: text,
                                        .priority: NSAccessibilityPriorityLevel.high.rawValue])
    }

    func dismiss() {
        problemTick?.invalidate()
        problemTick = nil
        state.problem = ""
        state.problemFix = nil
        state.problemPausedAt = nil
        panel.orderOut(nil)
        // Only an errored session is the one this problem belongs to. On a
        // live session stopListening() cancels the refinement or stops the
        // mic, which the countdown must never do behind the user's back; the
        // Qt front end has always guarded it this way.
        if bridge.stateName == "error" {
            bridge.stopListening()
        }
    }

    /// What a fix button does: the panel goes, and the fix runs.
    func perform(_ fix: SpeecherErrorAction) {
        dismiss()
        model.perform(fix)
    }

    /// The fix a finished delivery offers, as its button would run it.
    func performOutcomeFix() {
        if let fix = state.outcomeFix { perform(fix) }
    }

    var isVisible: Bool { panel.isVisible }
    var previewFades: Bool { state.previewFades }
    var busyFrame: CGRect { state.busyFrame }
    var statusFrame: CGRect { state.statusFrame }
    var cancelFrame: CGRect { state.cancelFrame }
    var outcomeFixFrame: CGRect { state.outcomeFixFrame }
    var level: NSWindow.Level { panel.level }

    private func setPreview(_ preview: String) {
        guard !frozen, state.phase == .live else { return }
        applyPreview(preview)
    }

    /// The one line of type and the pill's width around it, shared by the live
    /// speech preview and the streamed refinement text.
    private func applyPreview(_ preview: String) {
        state.preview = preview.split(whereSeparator: { $0.isWhitespace }).joined(separator: " ")
        syncFrameHeight()

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
        refreshWhatsNewBanner()
        position()
        panel.orderFrontRegardless()
    }

    /// The update chip, as core words it for every platform's popup.
    private func refreshUpdateBanner(_ update: SpeecherUpdateBanner) {
        guard !e2eBanners else {
            // ui-lint: allow core-string (the e2e fixture stands in for core's model)
            setUpdateBanner("Speecher nightly build 481 (gabc1234) is available", action: "Install and restart")
            return
        }
        guard update.visible, update.showInPopup else {
            setUpdateBanner("")
            return
        }
        setUpdateBanner(update.text, action: update.action, enabled: update.actionEnabled)
    }

    private func setUpdateBanner(_ message: String, action: String = "", enabled: Bool = true) {
        state.updateMessage = message
        state.updateAction = action
        state.updateActionEnabled = enabled
        syncFrameHeight()
    }

    /// The offer returns with every showing of the panel and tidies itself away
    /// six seconds later; only the dismiss button clears the pending state.
    private func refreshWhatsNewBanner() {
        guard !e2eBanners else {
            setWhatsNewMessage(model.whatsNewBanner.text)
            return
        }
        setWhatsNewMessage(model.whatsNewPending ? model.whatsNewBanner.text : "")
        whatsNewAutoHide?.invalidate()
        guard !state.whatsNewMessage.isEmpty else { return }
        whatsNewAutoHide = Timer.scheduledTimer(withTimeInterval: 6, repeats: false) { [weak self] _ in
            DispatchQueue.main.async { self?.setWhatsNewMessage("") }
        }
    }

    private func setWhatsNewMessage(_ message: String) {
        state.whatsNewMessage = message
        syncFrameHeight()
    }

    /// The window grows upward to make room for the banners: its origin is the
    /// bottom-left corner, which position() pins above the screen edge.
    private func syncFrameHeight() {
        let banners = (state.updateMessage.isEmpty ? 0 : 1)
            + (state.whatsNewMessage.isEmpty ? 0 : 1)
        let font = NSFont.systemFont(ofSize: NSFont.systemFontSize)
        let screenArea = (panel.screen ?? NSScreen.main)?.visibleFrame
        let availableWidth = screenArea?.width ?? maximumPreviewBarWidth + screenEdgeMargin
        // A problem wraps at the width every platform shares, and the capsule
        // grows taller rather than wider.
        let wrapWidth = SpeecherBridge.popupErrorWrapWidth
        let problemChrome = problemChromeWidth(font: font)
        let widthLimit: CGFloat = !state.problem.isEmpty ? wrapWidth + problemChrome
            : state.wraps ? wrapWidth + outcomeChromeWidth(font: font)
            : state.showsPreview ? maximumPreviewBarWidth : 568
        let maximumWidth = min(widthLimit, availableWidth - screenEdgeMargin)
        let textWidth = { (text: String) in
            ceil((text as NSString).size(withAttributes: [.font: popupTextFont]).width)
        }
        let contentWidth: CGFloat
        if !state.problem.isEmpty {
            let bounds = (state.problem as NSString).boundingRect(
                with: NSSize(width: maximumWidth - problemChrome, height: .greatestFiniteMagnitude),
                options: [.usesLineFragmentOrigin], attributes: [.font: font])
            // Text, the air around it and the countdown bar beneath.
            state.problemHeight = max(problemMinimumHeight, ceil(bounds.height) + 20 + 14)
            contentWidth = ceil(bounds.width) + problemChrome
        } else if state.wraps {
            // The outcome's symbol and its fix button beside the wrapped line.
            let chrome = outcomeChromeWidth(font: font)
            let bounds = (state.status as NSString).boundingRect(
                with: NSSize(width: min(wrapWidth, maximumWidth - chrome), height: .greatestFiniteMagnitude),
                options: [.usesLineFragmentOrigin], attributes: [.font: popupTextFont])
            state.outcomeHeight = max(PopupGeometry.pillHeight, ceil(bounds.height) + 20)
            contentWidth = ceil(bounds.width) + chrome
        } else if state.finished {
            contentWidth = max(PopupGeometry.pillMinimumWidth, textWidth(state.status) + 78)
        } else if state.showsPreview {
            contentWidth = max(state.minimumPreviewBarWidth,
                               textWidth(state.preview) + 2 * PopupGeometry.previewSideMargin)
        } else {
            // The status hugs its label and the pill hugs the row, no
            // narrower than a bare pill.
            contentWidth = state.showsControls ? state.lobeWidth
                : max(PopupGeometry.pillMinimumWidth, state.lobeWidth)
        }
        state.pillWidth = min(contentWidth, maximumWidth)
        let height = state.height + CGFloat(banners) * (bannerHeight + bannerSpacing)
        // Each banner is one line sized to its message and buttons, so the
        // window is as wide as the widest, within the screen.
        let width = min(max(state.pillWidth, bannerWidth(font: font)),
                        availableWidth - screenEdgeMargin)
        var frame = panel.frame
        guard abs(frame.width - width) >= 1 || abs(frame.height - height) >= 1 else { return }
        // AppKit rounds window frames. Reusing the previous frame's center
        // accumulates that rounding on every streamed preview update.
        frame.origin.x = (screenArea?.midX ?? frame.midX) - width / 2
        frame.size = NSSize(width: width, height: height)
        panel.setFrame(frame, display: true)
    }

    /// The widest banner showing: its message in the callout font, the
    /// capsule buttons beside it and the padding around them.
    private func bannerWidth(font: NSFont) -> CGFloat {
        let callout = NSFont.preferredFont(forTextStyle: .callout)
        let button = { (title: String) -> CGFloat in
            title.isEmpty ? 0 : (title as NSString).size(withAttributes: [.font: font]).width + 34
        }
        let rows: [(String, String, Bool)] = [
            (state.updateMessage, state.updateAction, false),
            (state.whatsNewMessage, model.whatsNewBanner.action, true),
        ]
        return rows.filter { !$0.0.isEmpty }.map { message, action, closes in
            ceil((message as NSString).size(withAttributes: [.font: callout]).width)
                + button(action) + (closes ? 38 : 0) + 16 + 5 + 10 * 2 + 4
        }.max() ?? 0
    }

    /// What sits beside a problem's text: the padding, the warning symbol and
    /// the bordered fix and Dismiss buttons, with the gaps between them.
    /// Measured from the real controls, so a one-line problem never wraps for
    /// want of a point.
    private func problemChromeWidth(font: NSFont) -> CGFloat {
        let buttons = [state.problemFix?.label, SpeecherBridge.popupDismissCaption].compactMap { $0 }
        let buttonWidths = buttons.map { title -> CGFloat in
            let button = NSButton(title: title, target: nil, action: nil)
            button.bezelStyle = .push
            return ceil(button.fittingSize.width) + 10
        }
        let symbol = NSImage(systemSymbolName: "exclamationmark.triangle.fill",
                             accessibilityDescription: nil)?
            .withSymbolConfiguration(.init(pointSize: font.pointSize, weight: .regular))
        return 2 * 24 + ceil(symbol?.size.width ?? font.pointSize) + 10 + buttonWidths.reduce(0, +)
    }

    /// What sits beside a fixable outcome's line: the padding, its symbol and
    /// the fix button, measured as problemChromeWidth measures its own.
    private func outcomeChromeWidth(font: NSFont) -> CGFloat {
        let button = NSButton(title: state.outcomeFix?.label ?? "", target: nil, action: nil)
        button.bezelStyle = .push
        // The symbol is about a point size wide, with the label's own gap after it.
        return 2 * 24 + font.pointSize + 18 + ceil(button.fittingSize.width) + 10
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
