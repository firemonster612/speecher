import AppKit
import AVFoundation
import Combine
import SwiftUI

// The setup assistant: the same nine steps the Qt assistant walked, as a native
// window over the same schema rows the settings window renders. Only the state
// no schema row holds — the provider check, the meter, the permissions and the
// finish rules — lives in the flow model below.

struct SetupStep: Identifiable {
    let id: String
    let title: String
    let intro: String

    static let all: [SetupStep] = [
        SetupStep(id: "welcome",
                  title: "Welcome to Speecher",
                  intro: "Speecher records a short dictation, turns it into text, "
                      + "and sends it to the app you were using."),
        SetupStep(id: "transcription",
                  title: "Transcription",
                  intro: "Choose the service Speecher uses to turn speech into a Raw Transcript."),
        SetupStep(id: "microphone",
                  title: "Microphone",
                  intro: "Choose the input Speecher should record. "
                      + "Speak normally and check that the level moves."),
        SetupStep(id: "accessibility",
                  title: "Accessibility",
                  intro: "Speecher pastes your dictation into the frontmost app with a synthetic "
                      + "Cmd+V. macOS calls that controlling your computer, so it needs "
                      + "Accessibility permission."),
        SetupStep(id: "delivery",
                  title: "Text delivery",
                  intro: "Speecher puts the finished text on your clipboard and pastes it into "
                      + "the frontmost app with Cmd+V. The paste needs the Accessibility "
                      + "permission from the previous step; without it the text still reaches "
                      + "your clipboard."),
        SetupStep(id: "refinement",
                  title: "Refinement",
                  intro: "Refinement can clean up a raw transcript after dictation. "
                      + "Choose a provider, or None to skip cleanup."),
        SetupStep(id: "profiles",
                  title: "Writing profiles",
                  intro: "Choose the fallback Writing Profile and how much cleanup and tone "
                      + "adjustment each profile receives."),
        SetupStep(id: "ready",
                  title: "Ready to dictate",
                  intro: "Setup is complete."),
        SetupStep(id: "login",
                  title: "Start at login",
                  intro: "Dictation only works while Speecher is running."),
    ]
}

/// The keys the finish step will hand the shortcut binder. Held rather than
/// bound as they are typed: the Qt assistant only registered the shortcut when
/// setup finished, and skipping must not leave a half-chosen binding behind.
struct PendingShortcut {
    let characters: String
    let flags: NSEvent.ModifierFlags
    let display: String

    /// ⌃⌥D, which reaches the binder as Qt's Meta+Alt+D.
    static let standard = PendingShortcut(characters: "d",
                                          flags: [.control, .option],
                                          display: "⌃⌥D")

    /// The HIG's modifier order, then the key the way the menu bar would
    /// write it.
    static func display(characters: String, flags: NSEvent.ModifierFlags) -> String {
        var text = ""
        if flags.contains(.control) { text += "⌃" }
        if flags.contains(.option) { text += "⌥" }
        if flags.contains(.shift) { text += "⇧" }
        if flags.contains(.command) { text += "⌘" }
        return text + keyName(characters)
    }

    private static func keyName(_ characters: String) -> String {
        guard let scalar = characters.unicodeScalars.first else { return "" }
        let functionKeyRange = UnicodeScalar(NSF1FunctionKey)!...UnicodeScalar(NSF12FunctionKey)!
        if functionKeyRange.contains(scalar) {
            return "F\(scalar.value - UInt32(NSF1FunctionKey) + 1)"
        }
        switch scalar {
        case " ": return "Space"
        case "\r": return "↩"
        case "\t": return "⇥"
        case "\u{1b}": return "⎋"
        default: return characters.uppercased()
        }
    }
}

/// Everything the assistant shows that no schema row holds. The settings the
/// steps edit go through the shared AppModel, exactly as the settings window
/// writes them.
@MainActor
final class SetupFlowModel: ObservableObject {
    let model: AppModel
    /// What happens once setup ends without a relaunch; the front end shows the
    /// settings window here.
    var onFinished: () -> Void = {}
    var closeWindow: () -> Void = {}

    @Published var step = 0

    // Transcription.
    @Published var providerStatus = ""
    @Published var providerReady = false

    // Microphone.
    @Published var meterLevel: Float = 0
    @Published var meterStatus = ""
    @Published var microphonePermission = AVCaptureDevice.authorizationStatus(for: .audio)
    @Published var inputVolumeNote = ""
    private var meterRunning = false
    private var lastVolumeRefresh = Date.distantPast

    // Accessibility. The grant recorded on first sight of the page decides
    // whether finishing must relaunch: a grant that pre-dated this run does not.
    @Published var accessibilityProblem = ""
    private var initialGrant: Bool?
    private var accessibilityPoll: Timer?

    // Finish.
    @Published var createShortcut = true { didSet { resetShortcutFailure() } }
    @Published var pendingShortcut = PendingShortcut.standard { didSet { resetShortcutFailure() } }
    @Published var shortcutStatus = SetupFlowModel.shortcutHint
    private var shortcutFailureAcknowledged = false

    // Start at login, applied when setup finishes so a skip leaves it alone.
    @Published var launchAtLogin: Bool

    static let shortcutHint = "Tap the shortcut to start dictation and tap it again to stop, "
        + "or hold it and talk — dictation ends when you let go."

    var steps: [SetupStep] { SetupStep.all }
    var isLastStep: Bool { step == steps.count - 1 }

    init(model: AppModel) {
        self.model = model
        launchAtLogin = RowView.flag(model.row("launchAtLogin")?.value)
        if !model.shortcut.isEmpty {
            // The binder already holds a shortcut; finishing keeps it unless a
            // new one is recorded over it.
            pendingShortcut = PendingShortcut(characters: "", flags: [], display: model.shortcut)
        }
    }

    func advance() {
        if isLastStep {
            finish()
            return
        }
        leave(steps[step].id)
        step += 1
    }

    func back() {
        guard step > 0 else { return }
        leave(steps[step].id)
        step -= 1
    }

    private func leave(_ stepId: String) {
        if stepId == "microphone" {
            stopMeter()
        }
        if stepId == "accessibility" {
            stopAccessibilityPoll()
        }
    }

    // MARK: Transcription

    var providerId: String { RowView.text(model.row("speechProvider")?.value) }
    var providerHint: String { model.bridge.setupHint(forSpeechProvider: providerId) }

    func checkProvider() {
        providerReady = false
        providerStatus = "Checking…"
        let checked = providerId
        model.bridge.checkSpeechProviderReady { [weak self] ok, message in
            guard let self, checked == providerId else { return }
            providerReady = ok
            providerStatus = message
        }
    }

    // MARK: Microphone

    func enterMicrophoneStep() {
        refreshMicrophonePermission()
        startMeter()
    }

    func refreshMicrophonePermission() {
        microphonePermission = AVCaptureDevice.authorizationStatus(for: .audio)
    }

    func requestMicrophoneAccess() {
        AVCaptureDevice.requestAccess(for: .audio) { [weak self] granted in
            DispatchQueue.main.async {
                guard let self else { return }
                self.refreshMicrophonePermission()
                if granted, self.meterRunning {
                    self.startMeter()
                }
            }
        }
    }

    func openMicrophoneSettings() {
        let pane = "x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone"
        NSWorkspace.shared.open(URL(string: pane)!)
    }

    func startMeter() {
        meterRunning = true
        meterLevel = 0
        refreshInputVolume()
        meterStatus = "Listening for microphone input…"
        model.bridge.startMicrophoneMeter(onLevel: { [weak self] level in
            guard let self else { return }
            meterLevel = level
            if Date().timeIntervalSince(lastVolumeRefresh) >= 0.5 {
                lastVolumeRefresh = Date()
                refreshInputVolume()
            }
            if level > 0.01 {
                meterStatus = "Microphone input detected."
            }
        }, failure: { [weak self] message in
            guard let self else { return }
            meterStatus = message
            meterLevel = 0
        })
    }

    func stopMeter() {
        meterRunning = false
        model.bridge.stopMicrophoneMeter()
        meterLevel = 0
    }

    /// The meter follows the settings, so a device change is a restart.
    func microphoneDeviceChanged() {
        if meterRunning {
            startMeter()
        }
    }

    private func refreshInputVolume() {
        let volume = model.bridge.microphoneInputVolume()
        guard volume >= 0, volume < 0.5 else {
            inputVolumeNote = ""
            return
        }
        inputVolumeNote = "macOS input volume for the default microphone is at "
            + "\(Int((volume * 100).rounded()))% — raise it in System Settings > Sound > Input "
            + "if Speecher hears you too quietly."
    }

    // MARK: Accessibility

    func enterAccessibilityStep() {
        model.bridge.refreshAccessibilityState()
        if initialGrant == nil {
            initialGrant = model.accessibilityEnabled
        }
        // macOS records the grant against the app signature and never delivers
        // it to the process that asked, so the only way to notice is to keep
        // looking while the page is up.
        accessibilityPoll = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            DispatchQueue.main.async { self?.model.bridge.refreshAccessibilityState() }
        }
    }

    func stopAccessibilityPoll() {
        accessibilityPoll?.invalidate()
        accessibilityPoll = nil
    }

    func requestAccessibility() {
        accessibilityProblem = model.bridge.requestAccessibilityGrant() ?? ""
    }

    var accessibilityStatus: String {
        if !accessibilityProblem.isEmpty { return accessibilityProblem }
        let grantedDuringSetup = initialGrant == false
        if model.accessibilityEnabled {
            return grantedDuringSetup
                ? "Accessibility is granted. Speecher will restart when setup finishes "
                    + "so macOS hands it the permission."
                : "Accessibility is granted."
        }
        return "Accessibility is off, so Speecher can copy your dictation but not paste it. "
            + "Grant it below — Speecher restarts itself when setup finishes."
    }

    /// A grant that appeared while this assistant was open only reaches Speecher
    /// after a relaunch; one that pre-dated it already did.
    private var accessibilityGrantAppearedDuringSetup: Bool {
        model.bridge.refreshAccessibilityState()
        return initialGrant == false && model.accessibilityEnabled
    }

    // MARK: Finish

    func recordShortcut(characters: String, flags: NSEvent.ModifierFlags) {
        pendingShortcut = PendingShortcut(
            characters: characters,
            flags: flags,
            display: PendingShortcut.display(characters: characters, flags: flags))
    }

    private func resetShortcutFailure() {
        shortcutFailureAcknowledged = false
        shortcutStatus = Self.shortcutHint
    }

    /// Mirrors the Qt assistant: a failed registration holds setup open once,
    /// with the failure on the shortcut step; finishing again continues without
    /// the shortcut.
    private func applyShortcut() -> Bool {
        guard createShortcut, !shortcutFailureAcknowledged else { return true }
        guard !pendingShortcut.characters.isEmpty else {
            // Nothing recorded over an existing binding, which stays as it is.
            return true
        }
        model.bindShortcut(characters: pendingShortcut.characters,
                           modifierFlags: pendingShortcut.flags)
        if model.shortcutProblem.isEmpty {
            shortcutStatus = "Dictation shortcut registered."
            return true
        }
        shortcutFailureAcknowledged = true
        shortcutStatus = "Could not register the shortcut: \(model.shortcutProblem). "
            + "Another app probably owns that combination. Change the shortcut and try again, "
            + "or click Finish again to continue without it."
        return false
    }

    private func finish() {
        if !applyShortcut() {
            step = steps.firstIndex { $0.id == "ready" } ?? step
            return
        }
        model.setValue(launchAtLogin as NSNumber, for: "launchAtLogin")
        complete()
    }

    /// What both Finish and Skip Setup do: the assistant never comes back at
    /// launch, and a grant given while it was open restarts the app.
    func complete() {
        stopMeter()
        stopAccessibilityPoll()
        model.bridge.completeSetup()
        if accessibilityGrantAppearedDuringSetup {
            let alert = NSAlert()
            alert.messageText = "Accessibility granted"
            alert.informativeText = "Speecher will now restart to apply the Accessibility grant."
            alert.runModal()
            closeWindow()
            model.bridge.relaunch()
            return
        }
        closeWindow()
        onFinished()
    }

    func skip() {
        complete()
    }

    /// A window closed mid-flow leaves setup incomplete, so only the hardware
    /// listeners need putting away.
    func abandon() {
        stopMeter()
        stopAccessibilityPoll()
    }
}

// MARK: - Views

struct SetupAssistantView: View {
    @ObservedObject var flow: SetupFlowModel
    @ObservedObject var model: AppModel

    var body: some View {
        let step = flow.steps[flow.step]
        VStack(alignment: .leading, spacing: 0) {
            VStack(alignment: .leading, spacing: 8) {
                Text(step.title)
                    .font(.title2.weight(.semibold))
                Text(step.intro)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .scenePadding([.top, .horizontal])
            .padding(.bottom, 4)
            content(step)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            Divider()
            controls
        }
    }

    @ViewBuilder private func content(_ step: SetupStep) -> some View {
        switch step.id {
        case "welcome": WelcomeStep()
        case "transcription": TranscriptionStep(flow: flow, model: model)
        case "microphone": MicrophoneStep(flow: flow, model: model)
        case "accessibility": AccessibilityStep(flow: flow, model: model)
        case "delivery": DeliveryStep(model: model)
        case "refinement": RefinementStep(model: model)
        case "profiles": ProfilesStep(model: model)
        case "ready": ReadyStep(flow: flow)
        default: LoginStep(flow: flow)
        }
    }

    private var controls: some View {
        HStack {
            // The last step carries Finish, at which point leaving is what the
            // big button does.
            if !flow.isLastStep {
                Button("Skip Setup") { flow.skip() }
            }
            Spacer()
            Text("Step \(flow.step + 1) of \(flow.steps.count)")
                .font(.callout)
                .foregroundStyle(.secondary)
            Spacer()
            Button("Back") { flow.back() }
                .disabled(flow.step == 0)
            Button(flow.isLastStep ? "Finish" : "Continue") { flow.advance() }
                .keyboardShortcut(.defaultAction)
        }
        .padding(12)
    }
}

private struct WelcomeStep: View {
    var body: some View {
        VStack(spacing: 16) {
            Image(nsImage: NSApp.applicationIconImage)
                .resizable()
                .frame(width: 96, height: 96)
            Text("This assistant checks your transcription provider, microphone, "
                + "accessibility, text delivery, refinement, and writing profiles.")
                .multilineTextAlignment(.center)
                .foregroundStyle(.secondary)
                .frame(maxWidth: 420)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

private struct TranscriptionStep: View {
    @ObservedObject var flow: SetupFlowModel
    @ObservedObject var model: AppModel

    var body: some View {
        Form {
            Section {
                if let row = model.row("speechProvider") {
                    RowView(row: row, model: model)
                }
                if !flow.providerStatus.isEmpty {
                    Text(flow.providerStatus)
                        .foregroundStyle(flow.providerReady ? AnyShapeStyle(.green)
                                                            : AnyShapeStyle(.secondary))
                }
            } footer: {
                if !flow.providerReady, !flow.providerHint.isEmpty {
                    Text(flow.providerHint)
                }
            }
            if !flow.providerReady {
                Section {
                    Button("Check Again") { flow.checkProvider() }
                }
            }
        }
        .formStyle(.grouped)
        .onAppear { flow.checkProvider() }
        .onChange(of: flow.providerId) { flow.checkProvider() }
    }
}

private struct MicrophoneStep: View {
    @ObservedObject var flow: SetupFlowModel
    @ObservedObject var model: AppModel

    var body: some View {
        Form {
            Section {
                permission
            }
            Section {
                if let row = model.row("audioDevice") {
                    RowView(row: row, model: model)
                }
                LabeledContent("Input level") {
                    ProgressView(value: min(max(flow.meterLevel, 0), 1))
                        .frame(width: 220)
                }
                if !flow.meterStatus.isEmpty {
                    Text(flow.meterStatus)
                        .foregroundStyle(.secondary)
                }
            } footer: {
                if !flow.inputVolumeNote.isEmpty {
                    Text(flow.inputVolumeNote)
                }
            }
        }
        .formStyle(.grouped)
        .onAppear { flow.enterMicrophoneStep() }
        .onChange(of: RowView.text(model.row("audioDevice")?.value)) {
            flow.microphoneDeviceChanged()
        }
        // The grant can change in System Settings while this page sits in the
        // assistant, and macOS does not push that back to a running process.
        .onReceive(NotificationCenter.default
            .publisher(for: NSApplication.didBecomeActiveNotification)) { _ in
            flow.refreshMicrophonePermission()
            if flow.microphonePermission == .authorized {
                flow.microphoneDeviceChanged()
            }
        }
    }

    @ViewBuilder private var permission: some View {
        switch flow.microphonePermission {
        case .authorized:
            Text("macOS lets Speecher use the microphone.")
                .foregroundStyle(.green)
        case .notDetermined:
            LabeledContent {
                Button("Allow Microphone Access") { flow.requestMicrophoneAccess() }
            } label: {
                Text("macOS has not been asked yet. Speecher only records while you dictate.")
            }
        default:
            LabeledContent {
                Button("Open Microphone Settings") { flow.openMicrophoneSettings() }
            } label: {
                Text("Microphone access is off, so Speecher records silence. Turn Speecher on "
                    + "under Privacy & Security > Microphone, then come back to this page.")
            }
        }
    }
}

private struct AccessibilityStep: View {
    @ObservedObject var flow: SetupFlowModel
    @ObservedObject var model: AppModel

    var body: some View {
        Form {
            Section {
                Text(flow.accessibilityStatus)
                    .foregroundStyle(model.accessibilityEnabled ? AnyShapeStyle(.green)
                                                                : AnyShapeStyle(.primary))
                if !model.accessibilityEnabled {
                    Button("Grant Accessibility Access") { flow.requestAccessibility() }
                }
            }
        }
        .formStyle(.grouped)
        .onAppear { flow.enterAccessibilityStep() }
    }
}

private struct DeliveryStep: View {
    @ObservedObject var model: AppModel

    var body: some View {
        Form {
            Section {
                Text("Nothing to install — Speecher uses the keyboard paste built into macOS.")
                    .foregroundStyle(.secondary)
            }
            Section {
                ForEach(model.rows(matching: ["outputFormat", "restoreClipboardAfterTyping"]),
                        id: \.rowId) { row in
                    RowView(row: row, model: model)
                }
            }
        }
        .formStyle(.grouped)
    }
}

private struct RefinementStep: View {
    @ObservedObject var model: AppModel

    var body: some View {
        // Only the selected provider's fast-mode row: the settings window
        // separates these onto per-provider panes, so the schema does not gate
        // them on the chosen provider itself.
        let provider = RowView.text(model.row("refinementProvider")?.value)
        let rowIds = ["refinementProvider"]
            + (provider == "openai" ? ["openAiFastMode"] : [])
            + (provider == "anthropic" ? ["anthropicFastMode"] : [])
        Form {
            Section {
                ForEach(model.rows(matching: rowIds), id: \.rowId) { row in
                    RowView(row: row, model: model)
                }
            }
        }
        .formStyle(.grouped)
    }
}

private struct ProfilesStep: View {
    @ObservedObject var model: AppModel

    var body: some View {
        Form {
            Section {
                if let row = model.row("defaultWritingProfile") {
                    RowView(row: row, model: model)
                }
            }
            Section("Profile behavior") {
                if let row = model.row("writingProfileBehavior") {
                    RowView(row: row, model: model)
                }
            }
        }
        .formStyle(.grouped)
    }
}

private struct ReadyStep: View {
    @ObservedObject var flow: SetupFlowModel
    @StateObject private var recorder = ShortcutRecorder()

    var body: some View {
        Form {
            Section {
                Toggle("Set up a dictation shortcut", isOn: $flow.createShortcut)
                LabeledContent("Dictation shortcut") {
                    Button(caption) {
                        recorder.record { characters, flags in
                            flow.recordShortcut(characters: characters, flags: flags)
                        }
                    }
                    .disabled(!flow.createShortcut)
                }
            } footer: {
                Text(footnote)
            }
        }
        .formStyle(.grouped)
        .onDisappear { recorder.stop() }
    }

    private var caption: String {
        recorder.recording ? "Type a shortcut…" : flow.pendingShortcut.display
    }

    private var footnote: String {
        recorder.recording
            ? "Press the keys you want, or Escape to keep the current one."
            : flow.shortcutStatus
    }
}

private struct LoginStep: View {
    @ObservedObject var flow: SetupFlowModel

    var body: some View {
        Form {
            Section {
                Toggle("Start Speecher at login", isOn: $flow.launchAtLogin)
            }
        }
        .formStyle(.grouped)
    }
}

// MARK: - Window

/// The assistant's window, so the Objective-C++ front end never has to know
/// what SwiftUI view is inside it.
@MainActor
final class SpeecherSetupAssistant: NSObject, NSWindowDelegate {
    private let flow: SetupFlowModel
    private let window: NSWindow
    private let onClosed: () -> Void
    private var stepObserver: AnyCancellable?

    init(model: AppModel, onFinished: @escaping () -> Void, onClosed: @escaping () -> Void) {
        flow = SetupFlowModel(model: model)
        self.onClosed = onClosed
        // Fixed size: an assistant is a fixed course, not a document. Closable
        // so setup can be abandoned, in which case it returns at next launch.
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 700, height: 560),
                          styleMask: [.titled, .closable],
                          backing: .buffered,
                          defer: false)
        window.isReleasedWhenClosed = false
        window.title = "Speecher Setup Assistant"
        let hosting = NSHostingController(
            rootView: SetupAssistantView(flow: flow, model: model))
        // The assistant owns its fixed size. SwiftUI's flexible content must
        // not turn its preferred size into a window taller than the screen.
        hosting.sizingOptions = []
        window.contentViewController = hosting
        window.setContentSize(NSSize(width: 700, height: 560))
        window.center()
        super.init()
        window.delegate = self
        flow.onFinished = onFinished
        flow.closeWindow = { [weak self] in self?.window.close() }
    }

    func show() {
        window.makeKeyAndOrderFront(nil)
        // Once shown, so the first capture is of a window that has laid out.
        installCaptureSeam()
    }

    func windowWillClose(_ notification: Notification) {
        flow.abandon()
        onClosed()
    }

    /// The E2E seam: with SPEECHER_E2E_SETUP_CAPTURE_DIR set, every step lands
    /// as a PNG there, named by its position and id. The window's backing store
    /// needs no screen-recording grant, which a CI runner does not have.
    private func installCaptureSeam() {
        guard stepObserver == nil,
              let dir = ProcessInfo.processInfo.environment["SPEECHER_E2E_SETUP_CAPTURE_DIR"],
              !dir.isEmpty else { return }
        let capture = { [weak self] (step: Int) in
            guard let self else { return }
            let id = flow.steps[step].id
            // @Published fires before SwiftUI renders. Two queued main-thread
            // blocks can still capture the previous page, especially while
            // the microphone starts. Give the renderer time to commit first.
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                guard self.flow.step == step else { return }
                self.window.contentView?.layoutSubtreeIfNeeded()
                self.window.displayIfNeeded()
                self.capture(toPath: "\(dir)/step-\(step + 1)-\(id).png")
            }
        }
        stepObserver = flow.$step.sink { capture($0) }
    }

    private func capture(toPath path: String) {
        guard let view = window.contentView?.superview ?? window.contentView,
              let bitmap = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { return }
        view.cacheDisplay(in: view.bounds, to: bitmap)
        guard let png = bitmap.representation(using: .png, properties: [:]) else { return }
        try? png.write(to: URL(fileURLWithPath: path), options: .atomic)
    }
}
