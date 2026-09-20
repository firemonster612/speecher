import AppKit
import AVFoundation
import Combine
import SwiftUI

// The setup assistant: the steps the Qt assistant walks, as a native window
// over the same schema rows the settings window renders. Only the state no
// schema row holds — the provider check, the meter, the permissions and the
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
                  intro: "Choose the service Speecher uses to turn speech into a raw transcript."),
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
        SetupStep(id: "shortcut",
                  title: "Dictation shortcut",
                  intro: "Choose what starts dictation: a key combination, or one key on "
                      + "its own, such as Right Option. Then choose what pressing it does."),
        SetupStep(id: "ready",
                  title: "Ready to dictate",
                  intro: "Almost done."),
        SetupStep(id: "login",
                  title: "Start at login",
                  intro: "Dictation only works while Speecher is running."),
    ]
}

/// A provider as the assistant lists it: the registry's strings, plus what the
/// readiness probe last said about it.
struct ProviderRow: Identifiable {
    let id: String
    let label: String
    let credentialSource: String
    let setupHint: String
    /// Whether a probe has answered at all. A row that has never been probed
    /// says so rather than claiming the provider is not set up.
    var probed = false
    var ready = false
    /// Why the provider is not ready; empty while it is.
    var message = ""

    init(_ provider: SpeecherProviderModel) {
        id = provider.providerId
        label = provider.label
        credentialSource = provider.credentialSource
        setupHint = provider.setupHint
    }

    /// The welcome step's verdict, which is about the sign-in behind the
    /// provider rather than the provider itself.
    var credentialStatus: String {
        guard probed else { return "Checking…" }
        return ready ? "Sign-in found" : "Not found"
    }

    /// The verdict the transcription and refinement rows carry.
    var readinessStatus: String {
        guard probed else { return "Checking…" }
        return ready ? "Ready" : "Not set up"
    }
}

/// The keys the finish step will hand the shortcut binder. Held rather than
/// bound as they are typed: the Qt assistant only registered the shortcut when
/// setup finished, and skipping must not leave a half-chosen binding behind.
/// One binding, either a combination or a single key: recording one kind
/// replaces the other.
struct PendingShortcut {
    let characters: String
    let flags: NSEvent.ModifierFlags
    /// The W3C KeyboardEvent.code name of a recorded single key; nil for a
    /// combination.
    let keyCode: String?
    let display: String

    init(characters: String,
         flags: NSEvent.ModifierFlags,
         keyCode: String? = nil,
         display: String) {
        self.characters = characters
        self.flags = flags
        self.keyCode = keyCode
        self.display = display
    }

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
    /// E2E capture seam only: called from the step content's onAppear, so a
    /// snapshot can never precede the destination step's view existing.
    var stepRendered: ((Int) -> Void)?

    @Published var step = 0

    // Transcription and refinement. Every registered provider carries its own
    // probe verdict, which the welcome step reads as a credential check and the
    // two provider steps read as a readiness one.
    @Published var speechProviders: [ProviderRow]
    @Published var refinementProviders: [ProviderRow]
    /// Auto-selecting a ready provider is a one-time courtesy per wizard run,
    /// and never overrules a choice the person made in the wizard.
    private var speechAutoSelected = false
    private var refinementAutoSelected = false
    private var speechChosenByUser = false
    private var refinementChosenByUser = false

    // Microphone.
    @Published var meterLevel: Float = 0
    @Published var meterStatus = ""
    @Published var microphonePermission = AVCaptureDevice.authorizationStatus(for: .audio)
    @Published var inputVolumeNote = ""
    /// Whether the meter has seen the level move since it last started. The
    /// gate cannot read the current sample: a person who spoke is silent again
    /// by the time they reach for Continue.
    @Published private var microphoneInputDetected = false
    private var meterRunning = false
    private var lastVolumeRefresh = Date.distantPast
    /// macOS grants microphone access in System Settings and tells this process
    /// nothing, so the only way to notice is to keep asking while the page is up.
    private var microphonePermissionPoll: Timer?

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
        speechProviders = model.bridge.speechProviders.map(ProviderRow.init)
        refinementProviders = model.bridge.refinementProviders.map(ProviderRow.init)
        launchAtLogin = RowView.flag(model.row("launchAtLogin")?.value)
        if !model.shortcut.isEmpty {
            // The binder already holds a shortcut; finishing keeps it unless a
            // new one is recorded over it. A bound single key carries its code
            // so it shows on the right recorder button.
            pendingShortcut = PendingShortcut(characters: "",
                                              flags: [],
                                              keyCode: model.bridge.currentSingleKeyCode,
                                              display: model.shortcut)
        }
    }

    /// Whether the step showing may be left. Every gate is the state the step
    /// itself already displays, so a disabled Continue always has a visible
    /// reason next to it.
    private func isSatisfied(_ stepId: String) -> Bool {
        switch stepId {
        // Nothing later in the assistant can succeed without one of the
        // provider sign-ins, so the first step holds until a probe finds one.
        // With no speech provider registered at all there is nothing to sign in
        // to, and holding here would strand the person on step one.
        case "welcome":
            return speechProviders.isEmpty || speechProviders.contains(where: \.ready)
        case "transcription": return providerReady
        case "microphone":
            return microphonePermission == .authorized && microphoneInputDetected
        case "accessibility": return model.accessibilityEnabled
        default: return true
        }
    }

    var canAdvance: Bool { isSatisfied(steps[step].id) }

    /// Skipping is only offered once it would leave a working app, which means
    /// every gate in the flow, not only the ones walked so far.
    var canSkip: Bool { steps.allSatisfy { isSatisfied($0.id) } }

    /// The first step whose gate is unmet, which is where a finish that cannot
    /// complete sends the person.
    private var firstUnsatisfiedStep: Int? {
        steps.firstIndex { !isSatisfied($0.id) }
    }

    func advance() {
        guard canAdvance else { return }
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
            stopMicrophonePermissionPoll()
        }
        if stepId == "accessibility" {
            stopAccessibilityPoll()
        }
    }

    // MARK: Providers

    var providerId: String { RowView.text(model.row("speechProvider")?.value) }
    var refinementProviderId: String { RowView.text(model.row("refinementProvider")?.value) }

    private var selectedSpeechProvider: ProviderRow? {
        speechProviders.first { $0.id == providerId }
    }

    /// nil while None is selected, which is a real answer rather than a
    /// missing one: None is not a registered provider.
    private var selectedRefinementProvider: ProviderRow? {
        refinementProviders.first { $0.id == refinementProviderId }
    }

    var providerHint: String { selectedSpeechProvider?.setupHint ?? "" }
    var providerReady: Bool { selectedSpeechProvider?.ready ?? false }

    /// The line under the transcription rows, which describes the selected
    /// service: the probe's own words when it refused, ours when it did not.
    var providerStatus: String {
        guard let provider = selectedSpeechProvider else {
            return "No transcription service is available."
        }
        guard provider.probed else { return "Checking…" }
        return provider.ready ? "\(provider.label) is ready." : provider.message
    }

    /// Refinement stays optional, so an unready provider is a warning rather
    /// than a gate: dictation still delivers, just without the cleanup.
    var refinementWarning: String {
        guard let provider = selectedRefinementProvider, provider.probed, !provider.ready else {
            return ""
        }
        return "\(provider.label) is not signed in. Dictation will deliver the raw transcript."
    }

    func checkSpeechProviders() {
        model.bridge.checkSpeechProviders { [weak self] id, ready, message in
            guard let self else { return }
            record(&speechProviders, id: id, ready: ready, message: message)
            autoSelectReadySpeechProvider()
        }
    }

    func checkRefinementProviders() {
        model.bridge.checkRefinementProviders { [weak self] id, ready, message in
            guard let self else { return }
            record(&refinementProviders, id: id, ready: ready, message: message)
            autoSelectReadyRefinementProvider()
        }
    }

    /// A re-probe leaves the last verdict on screen until the new one lands:
    /// clearing it first would shut the gate every time a step is revisited.
    private func record(_ rows: inout [ProviderRow], id: String, ready: Bool, message: String) {
        guard let index = rows.firstIndex(where: { $0.id == id }) else { return }
        rows[index].probed = true
        rows[index].ready = ready
        rows[index].message = message
    }

    func chooseSpeechProvider(_ id: String) {
        guard id != providerId else { return }
        speechChosenByUser = true
        model.setValue(id, for: "speechProvider")
    }

    func chooseRefinementProvider(_ id: String) {
        guard id != refinementProviderId else { return }
        refinementChosenByUser = true
        model.setValue(id, for: "refinementProvider")
    }

    /// The saved service cannot transcribe but another one can: start the
    /// person on the one that works rather than on a dead end.
    private func autoSelectReadySpeechProvider() {
        guard !speechAutoSelected, !speechChosenByUser,
              speechProviders.allSatisfy(\.probed) else { return }
        speechAutoSelected = true
        guard selectedSpeechProvider?.ready != true,
              let ready = speechProviders.first(where: \.ready) else { return }
        model.setValue(ready.id, for: "speechProvider")
    }

    /// The same courtesy on the refinement step, except that None is a choice
    /// in its own right: someone who wants no cleanup is left on it.
    private func autoSelectReadyRefinementProvider() {
        guard !refinementAutoSelected, !refinementChosenByUser,
              refinementProviders.allSatisfy(\.probed) else { return }
        refinementAutoSelected = true
        guard let selected = selectedRefinementProvider, !selected.ready,
              let ready = refinementProviders.first(where: \.ready) else { return }
        model.setValue(ready.id, for: "refinementProvider")
    }

    // MARK: Microphone

    func enterMicrophoneStep() {
        refreshMicrophonePermission()
        // SwiftUI can re-attach a view and call onAppear again, so the previous
        // timer must not be orphaned.
        microphonePermissionPoll?.invalidate()
        microphonePermissionPoll = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) {
            [weak self] _ in
            DispatchQueue.main.async {
                guard let self else { return }
                let previous = microphonePermission
                refreshMicrophonePermission()
                // Access just granted in System Settings: the meter could not
                // have started before, so start it now that it can.
                if previous != .authorized, microphonePermission == .authorized, meterRunning {
                    startMeter()
                }
            }
        }
        startMeter()
    }

    func stopMicrophonePermissionPoll() {
        microphonePermissionPoll?.invalidate()
        microphonePermissionPoll = nil
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
        // Deliberately does not clear microphoneInputDetected: re-entering the
        // step must not make the user speak again. The failure callback and a
        // device change clear it, matching the Qt page.
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
                microphoneInputDetected = true
            }
        }, failure: { [weak self] message in
            guard let self else { return }
            meterStatus = message
            meterLevel = 0
            microphoneInputDetected = false
        })
    }

    func stopMeter() {
        meterRunning = false
        model.bridge.stopMicrophoneMeter()
        meterLevel = 0
    }

    /// The meter follows the settings, so a device change is a restart. The
    /// gate is about the input that will actually record, so a switch has to
    /// prove itself again.
    func microphoneDeviceChanged() {
        microphoneInputDetected = false
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
        // looking while the page is up. SwiftUI can re-attach a view and call
        // onAppear again, so the previous timer must not be orphaned.
        accessibilityPoll?.invalidate()
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

    // MARK: Shortcut

    func recordShortcut(characters: String, flags: NSEvent.ModifierFlags) {
        pendingShortcut = PendingShortcut(
            characters: characters,
            flags: flags,
            display: PendingShortcut.display(characters: characters, flags: flags))
    }

    func recordSingleKey(code: String) {
        pendingShortcut = PendingShortcut(
            characters: "",
            flags: [],
            keyCode: code,
            display: model.bridge.display(forSingleKeyCode: code))
    }

    /// The inline, non-blocking note under a recorded single key: the
    /// backend's refusal when it has one (missing Accessibility being the
    /// common case), otherwise the caveat that the key keeps its normal job.
    /// Empty for a combination or a silent key.
    var pendingSingleKeyNote: String {
        guard let code = pendingShortcut.keyCode else { return "" }
        return model.bridge.unsupportedReason(forSingleKeyCode: code)
            ?? model.bridge.warning(forSingleKeyCode: code)
    }

    /// Whether the backend refuses the recorded single key, which is what
    /// makes the grant call-to-action appear.
    var pendingSingleKeyRefused: Bool {
        guard let code = pendingShortcut.keyCode else { return false }
        return model.bridge.unsupportedReason(forSingleKeyCode: code) != nil
    }

    /// How the Ready page describes dictating, which has to match the
    /// activation mode chosen on the shortcut step.
    var activationInstruction: String {
        let key = pendingShortcut.display
        switch RowView.text(model.row("activationMode")?.value) {
        case "toggle": return "press \(key) to start, press it again to stop"
        case "push_to_talk": return "hold \(key) while you speak"
        default: return "tap \(key) to toggle, or hold it to dictate until release"
        }
    }

    /// The Ready page's footer. A refused registration is let through on the
    /// second finish, so the page says the app will have no shortcut rather
    /// than repeating the offer to try again.
    var readyStatus: String {
        shortcutFailureAcknowledged
            ? "No dictation shortcut is set. You can set one in Settings > Shortcut."
            : shortcutStatus
    }

    private func resetShortcutFailure() {
        shortcutFailureAcknowledged = false
        shortcutStatus = Self.shortcutHint
    }

    /// Mirrors the Qt assistant: finishing always tries to register the shown
    /// binding — the binder reports its built-in default even before anything
    /// was ever bound, so "nothing recorded" still has to register and store
    /// it. A failed registration holds setup open once, back on the shortcut
    /// step; finishing again continues without the shortcut.
    private func applyShortcut() -> Bool {
        guard createShortcut, !shortcutFailureAcknowledged else { return true }
        if let code = pendingShortcut.keyCode {
            model.bindSingleKey(code: code)
        } else if pendingShortcut.characters.isEmpty {
            model.bindCurrentShortcut()
        } else {
            model.bindShortcut(characters: pendingShortcut.characters,
                               modifierFlags: pendingShortcut.flags)
        }
        if model.shortcutProblem.isEmpty {
            shortcutStatus = "Dictation shortcut registered."
            return true
        }
        shortcutFailureAcknowledged = true
        shortcutStatus = "Could not register the shortcut: \(model.shortcutProblem). "
            + "Change the shortcut and try again, or finish setup again to "
            + "continue without it."
        return false
    }

    /// Jumps to another step the way next() and back() do, so the step being
    /// left puts its hardware away. Assigning `step` on its own left the
    /// microphone meter capturing behind whatever page came up.
    private func jump(to target: Int) {
        guard target != step else { return }
        leave(steps[step].id)
        step = target
    }

    private func finish() {
        // Gates can lapse behind the flow — a permission revoked in System
        // Settings while the assistant sat on a later step — so finishing
        // checks them all again rather than trusting the walk here. The
        // microphone grant is read back first: the permission poll only runs
        // while its own step is on screen, and the read is a local, synchronous
        // one. The provider probes are not, so those answer from the re-check
        // the ready step kicked off.
        refreshMicrophonePermission()
        if let blocked = firstUnsatisfiedStep {
            jump(to: blocked)
            return
        }
        if !applyShortcut() {
            jump(to: steps.firstIndex { $0.id == "shortcut" } ?? step)
            return
        }
        model.setValue(launchAtLogin as NSNumber, for: "launchAtLogin")
        complete()
    }

    /// What both Finish and Skip Setup do: the assistant never comes back at
    /// launch, and a grant given while it was open restarts the app.
    func complete() {
        stopMeter()
        stopMicrophonePermissionPoll()
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

    /// Skip is Finish minus the pages in between: it is only offered when every
    /// gate passes, and it must leave the same shortcut and login setting
    /// behind, or the app it completes has no way to start dictation.
    func skip() {
        finish()
    }

    /// A window closed mid-flow leaves setup incomplete, so only the hardware
    /// listeners need putting away.
    func abandon() {
        stopMeter()
        stopMicrophonePermissionPoll()
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
                // onAppear must sit inside the .id boundary or it keeps the
                // outer wrapper's identity and only ever fires once.
                .onAppear { flow.stepRendered?(flow.step) }
                .id(flow.step)
            Divider()
            controls
        }
    }

    @ViewBuilder private func content(_ step: SetupStep) -> some View {
        switch step.id {
        case "welcome": WelcomeStep(flow: flow)
        case "transcription": TranscriptionStep(flow: flow, model: model)
        case "microphone": MicrophoneStep(flow: flow, model: model)
        case "accessibility": AccessibilityStep(flow: flow, model: model)
        case "delivery": DeliveryStep(model: model)
        case "refinement": RefinementStep(flow: flow, model: model)
        case "profiles": ProfilesStep(model: model)
        case "shortcut": ShortcutStep(flow: flow, model: model)
        case "ready": ReadyStep(flow: flow)
        default: LoginStep(flow: flow)
        }
    }

    private var controls: some View {
        HStack {
            // The last step carries Finish, at which point leaving is what the
            // big button does. Skipping is hidden until every gate passes:
            // a skipped setup never comes back, so it must not be the way out
            // of a step the person could not complete.
            if !flow.isLastStep, flow.canSkip {
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
                .disabled(!flow.canAdvance)
        }
        .padding(12)
    }
}

private struct WelcomeStep: View {
    @ObservedObject var flow: SetupFlowModel

    var body: some View {
        Form {
            Section {
                VStack(spacing: 16) {
                    Image(nsImage: NSApp.applicationIconImage)
                        .resizable()
                        .frame(width: 96, height: 96)
                    Text("This assistant checks everything dictation needs: your speech "
                        + "service, microphone, and how text reaches your apps.")
                        .multilineTextAlignment(.center)
                        .foregroundStyle(.secondary)
                        .frame(maxWidth: 420)
                }
                .frame(maxWidth: .infinity)
            }
            // Nothing later in the assistant can succeed without one of these
            // sign-ins, so the one real prerequisite is stated on the first step.
            Section("Before you start") {
                Text("Speecher uses your existing ChatGPT or Claude sign-in. Install and sign "
                    + "in to one of these, then choose Check again:")
                ForEach(flow.speechProviders) { provider in
                    VStack(alignment: .leading, spacing: 2) {
                        LabeledContent(provider.credentialSource) {
                            Text(provider.credentialStatus)
                                .foregroundStyle(provider.ready ? AnyShapeStyle(.green)
                                                                : AnyShapeStyle(.secondary))
                        }
                        if provider.probed, !provider.ready, !provider.setupHint.isEmpty {
                            Text(provider.setupHint)
                                .font(.callout)
                                .foregroundStyle(.secondary)
                        }
                    }
                }
                Button("Check Again") { flow.checkSpeechProviders() }
            }
        }
        .formStyle(.grouped)
        // Probed on appearance rather than at construction: a sign-in made in a
        // terminal while the assistant sat open counts as soon as the person
        // comes back to this step.
        .onAppear { flow.checkSpeechProviders() }
    }
}

/// One selectable provider: its label, the probe's verdict under it, and for
/// refinement the sign-in the provider borrows.
private struct ProviderOptionLabel: View {
    let title: String
    let status: String
    let positive: Bool
    var note = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(title)
                .fontWeight(.semibold)
            Text(status)
                .font(.callout)
                .foregroundStyle(positive ? AnyShapeStyle(.green) : AnyShapeStyle(.secondary))
            if !note.isEmpty {
                Text(note)
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
        }
    }
}

/// The registry's facts about a provider, as small secondary label/value rows
/// under the picker that chooses it.
private struct ProviderStatsRows: View {
    let stats: [[String]]

    var body: some View {
        ForEach(stats, id: \.first) { stat in
            LabeledContent(stat[0]) { Text(stat[1]) }
                .font(.callout)
                .foregroundStyle(.secondary)
        }
    }
}

private struct TranscriptionStep: View {
    @ObservedObject var flow: SetupFlowModel
    @ObservedObject var model: AppModel

    var body: some View {
        Form {
            Section {
                // Every service is on the step with its own readiness, rather
                // than hidden behind a pop-up button that has to be opened to
                // find out what is there.
                Picker(selection: selection) {
                    ForEach(flow.speechProviders) { provider in
                        ProviderOptionLabel(title: provider.label,
                                            status: provider.readinessStatus,
                                            positive: provider.ready)
                            .tag(provider.id)
                    }
                } label: {
                    Text("Transcription service")
                }
                .pickerStyle(.radioGroup)
                ProviderStatsRows(stats: model.bridge.stats(forSpeechProvider: flow.providerId))
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
                    Button("Check Again") { flow.checkSpeechProviders() }
                }
            }
        }
        .formStyle(.grouped)
        // One round covers every row, so selecting a different service shows a
        // verdict this step already holds rather than starting a new probe.
        .onAppear { flow.checkSpeechProviders() }
    }

    /// Writing this binding is the person choosing, which is what stops the
    /// auto-selection from moving them afterwards.
    private var selection: Binding<String> {
        Binding(get: { flow.providerId }, set: { flow.chooseSpeechProvider($0) })
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
    @ObservedObject var flow: SetupFlowModel
    @ObservedObject var model: AppModel

    var body: some View {
        // Only the selected provider's fast-mode row: the settings window
        // separates these onto per-provider panes, so the schema does not gate
        // them on the chosen provider itself.
        let provider = flow.refinementProviderId
        let fastModeIds = (provider == "openai" ? ["openAiFastMode"] : [])
            + (provider == "anthropic" ? ["anthropicFastMode"] : [])
        Form {
            Section {
                Picker(selection: selection) {
                    ForEach(flow.refinementProviders) { row in
                        // The brands differ from the transcription step's, so
                        // each row says which sign-in it actually uses.
                        ProviderOptionLabel(title: row.label,
                                            status: row.readinessStatus,
                                            positive: row.ready,
                                            note: row.setupHint)
                            .tag(row.id)
                    }
                    ProviderOptionLabel(title: "None", status: "No cleanup", positive: false)
                        .tag("none")
                } label: {
                    Text("Cleanup provider")
                }
                .pickerStyle(.radioGroup)
                // None has no facts worth a block, so choosing it hides them.
                ProviderStatsRows(stats: model.bridge.stats(forRefinementProvider: provider))
                if !flow.refinementWarning.isEmpty {
                    Text(flow.refinementWarning)
                }
                ForEach(model.rows(matching: fastModeIds), id: \.rowId) { row in
                    RowView(row: row, model: model)
                }
            }
        }
        .formStyle(.grouped)
        .onAppear { flow.checkRefinementProviders() }
    }

    private var selection: Binding<String> {
        Binding(get: { flow.refinementProviderId }, set: { flow.chooseRefinementProvider($0) })
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

/// The shortcut step, which also carries the activation-mode choice: the key
/// and what pressing it does are decided together. The mode drives the same
/// activationMode schema row the General pane renders, so there is no second
/// source of truth.
private struct ShortcutStep: View {
    @ObservedObject var flow: SetupFlowModel
    @ObservedObject var model: AppModel
    @StateObject private var recorder = ShortcutRecorder()
    /// A key the recorder caught but could not bind (a media key); shown in
    /// the footer while the recorder stays armed.
    @State private var captureProblem = ""

    var body: some View {
        Form {
            Section {
                Toggle("Set up a dictation shortcut", isOn: $flow.createShortcut)
                LabeledContent {
                    Button(caption) {
                        captureProblem = ""
                        recorder.record(suspending: model, combination: { characters, flags in
                            flow.recordShortcut(characters: characters, flags: flags)
                        }, singleKey: { keyCode in
                            guard let code = model.keyCodeName(forMacKeyCode: keyCode) else {
                                captureProblem = "That key cannot be a dictation key."
                                return false
                            }
                            flow.recordSingleKey(code: code)
                            return true
                        })
                    }
                    .disabled(!flow.createShortcut)
                } label: {
                    Text("Dictation shortcut")
                    Text("Press a key combination, or a single key such as "
                         + "Right Option or F13.")
                }
                if flow.pendingSingleKeyRefused, !model.accessibilityEnabled {
                    Button("Grant Accessibility Access") { flow.requestAccessibility() }
                }
            } footer: {
                Text(footnote)
            }
            Section {
                if let row = model.row("activationMode") {
                    RowView(row: row, model: model)
                }
            }
        }
        .formStyle(.grouped)
        .onDisappear { recorder.stop() }
    }

    private var caption: String {
        if recorder.recording { return "Press a key or key combination…" }
        return flow.pendingShortcut.display
    }

    private var footnote: String {
        if recorder.recording {
            if !captureProblem.isEmpty {
                return captureProblem + " Try another, or press Escape to keep the current one."
            }
            return "Press the keys you want — a bare modifier like Right Option works — "
                + "or Escape to keep the current one."
        }
        // A registration failure explains how to continue, so it outranks the
        // recorded key's caveat; recording again resets it to the hint.
        if flow.shortcutStatus != SetupFlowModel.shortcutHint { return flow.shortcutStatus }
        let note = flow.pendingSingleKeyNote
        return note.isEmpty ? flow.shortcutStatus : note
    }
}

private struct ReadyStep: View {
    @ObservedObject var flow: SetupFlowModel

    var body: some View {
        Form {
            Section {
                Text(flow.createShortcut
                    ? "Finishing registers \(flow.pendingShortcut.display) as the "
                        + "dictation shortcut. To dictate, \(flow.activationInstruction)."
                    : "Finishing completes setup without a dictation shortcut.")
                    .foregroundStyle(.secondary)
            } footer: {
                Text(flow.readyStatus)
            }
        }
        .formStyle(.grouped)
        // Finishing is two steps away, so the sign-in the welcome step found is
        // re-probed here rather than trusted: one that expired while the
        // assistant sat open shuts the gate again before Finish is offered.
        .onAppear { flow.checkSpeechProviders() }
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

    /// Re-showing an already-open assistant must not keep running the finish
    /// handler of the request that first opened it.
    func update(onFinished: @escaping () -> Void) {
        flow.onFinished = onFinished
    }

    /// The front end going away takes the assistant with it; an orphaned
    /// window would keep a flow whose callbacks point into freed memory.
    func close() {
        window.close()
    }

    func windowWillClose(_ notification: Notification) {
        flow.abandon()
        onClosed()
    }

    /// The E2E seam: with SPEECHER_E2E_SETUP_CAPTURE_DIR set, every step lands
    /// as a PNG there, named by its position and id. The window's backing store
    /// needs no screen-recording grant, which a CI runner does not have.
    private func installCaptureSeam() {
        guard flow.stepRendered == nil,
              let dir = ProcessInfo.processInfo.environment["SPEECHER_E2E_SETUP_CAPTURE_DIR"],
              !dir.isEmpty else { return }
        // Driven by the step content's onAppear rather than $step: the model
        // publishes before SwiftUI renders, and a slow main-thread check (for
        // example the transcription step's Keychain read) can hold the commit
        // past any fixed delay, snapshotting the previous page under the new
        // step's name. onAppear cannot fire before the view exists; the short
        // settle lets the window finish drawing it.
        flow.stepRendered = { [weak self] (step: Int) in
            guard let self else { return }
            let id = flow.steps[step].id
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
                guard self.flow.step == step else { return }
                self.window.contentView?.layoutSubtreeIfNeeded()
                self.window.displayIfNeeded()
                self.capture(toPath: "\(dir)/step-\(step + 1)-\(id).png")
            }
        }
        // The first step rendered before this seam installed, so its onAppear
        // has already fired; capture it now. Later duplicates just overwrite.
        flow.stepRendered?(flow.step)
    }

    private func capture(toPath path: String) {
        guard let view = window.contentView?.superview ?? window.contentView,
              let bitmap = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { return }
        view.cacheDisplay(in: view.bounds, to: bitmap)
        guard let png = bitmap.representation(using: .png, properties: [:]) else { return }
        try? png.write(to: URL(fileURLWithPath: path), options: .atomic)
    }
}
