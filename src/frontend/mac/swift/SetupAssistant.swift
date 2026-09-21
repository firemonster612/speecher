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
                      + "Speak normally; setup continues once the level moves."),
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
                  intro: "Speecher picks a writing profile from the app you dictate into. "
                      + "Choose the fallback profile and how much cleanup and tone "
                      + "adjustment each one gets."),
        SetupStep(id: "shortcut",
                  title: "Dictation shortcut",
                  intro: "Choose what starts dictation: a key combination, or one key on "
                      + "its own, such as Right Option. Then choose what pressing it does."),
        // The ready step's lead depends on whether anything is still unfinished,
        // so the flow supplies it; see SetupFlowModel.intro(for:).
        SetupStep(id: "ready",
                  title: "Ready to dictate",
                  intro: ""),
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

    /// The sign-in instruction the welcome step prints under a row it could
    /// not find. The registry's hint is written for the provider steps and
    /// every front end shares it; the Claude one names only the CLI, so the
    /// welcome step states the fuller instruction that covers the app too.
    var credentialHint: String {
        guard id == "claude" else { return setupHint }
        return "Install Claude Code from claude.com/code and sign in — the desktop app "
            + "or the claude CLI (/login) both work."
    }

    /// The verdict the transcription and refinement rows carry.
    var readinessStatus: String {
        guard probed else { return "Checking…" }
        return ready ? "Ready" : "Not set up"
    }
}

/// One CLI Proxy API account the transcription step's picker offers.
struct CliproxyAccountChoice: Identifiable {
    let id: String
    let label: String
    let enabled: Bool
}

/// One unfinished gate, as the ready step lists it: the step it belongs to,
/// and the one line saying what is missing.
struct BlockedStep: Identifiable {
    let id: String
    let index: Int
    let title: String
    let reason: String
}

/// One line of the ready step's checklist: what was set up, and to what.
struct ReadyItem: Identifiable {
    let id: String
    /// The provider whose mark belongs on the row; empty for a plain glyph.
    let providerId: String
    /// The glyph an item with no brand behind it wears.
    let symbol: String
    let label: String
    let status: String
    let ready: Bool
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

    // The CLI Proxy API sign-in, mirrored from the bridge's ProviderSignIn
    // seams so this assistant behaves exactly like the Qt and WinUI ones.
    @Published var cliproxyAvailable = false
    @Published var signInSupported = false
    @Published var usingCliproxy = false
    @Published var cliproxyAccounts: [CliproxyAccountChoice] = []
    @Published var cliproxyAccount = ""
    @Published var cliproxyDirectory = ""
    @Published var cliproxyDirectoryPlaceholder = ""

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
        cliproxyDirectory = model.bridge.setupCliproxyDirectory
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
        // The E2E seam: provider_steps_run.sh walks the assistant to verify
        // stats rendering, not gating (setup_run.sh covers the gates), and the
        // runner has no sign-ins, microphone, or accessibility grant to
        // satisfy them for real.
        if ProcessInfo.processInfo.environment["SPEECHER_E2E_SKIP_SETUP_GATES"] == "1" {
            return true
        }
        switch stepId {
        // Nothing later in the assistant can succeed without one of the
        // provider sign-ins, so the first step holds until a probe finds one.
        // With no speech provider registered at all there is nothing to sign in
        // to, and holding here would strand the person on step one.
        case "welcome":
            return speechProviders.isEmpty || speechProviders.contains(where: \.ready)
                || cliproxyAvailable
        case "transcription": return providerReady
        case "microphone":
            return microphonePermission == .authorized && microphoneInputDetected
        case "accessibility": return model.accessibilityEnabled
        // The ready step's own gate is every other gate: a Finish that could
        // not work is held here, next to the list of what is holding it.
        case "ready": return blockedSteps.isEmpty
        default: return true
        }
    }

    var canAdvance: Bool { isSatisfied(steps[step].id) }

    var isReadyStep: Bool { steps[step].id == "ready" }

    /// The lead under the title. Only the ready step's changes with the flow.
    func intro(for step: SetupStep) -> String {
        guard step.id == "ready", !blockedSteps.isEmpty else { return step.intro }
        return "Speecher can't dictate yet. Finish the steps below, or go back "
            + "and change your choices."
    }

    /// Every gate still unmet, in flow order. Never includes the ready step,
    /// whose own gate is exactly this list being empty.
    var blockedSteps: [BlockedStep] {
        steps.enumerated().compactMap { index, step -> BlockedStep? in
            guard step.id != "ready", !isSatisfied(step.id) else { return nil }
            return BlockedStep(id: step.id,
                               index: index,
                               title: step.title,
                               reason: blockedReason(step.id))
        }
    }

    /// Why one gate is shut, in the step's own words where it has them.
    private func blockedReason(_ stepId: String) -> String {
        switch stepId {
        case "welcome":
            return "No ChatGPT, Claude, or CLI Proxy API sign-in was found."
        case "transcription":
            let line = providerStatus
            return line.isEmpty ? "The transcription service is not signed in." : line
        case "microphone":
            return microphonePermission == .authorized
                ? "No microphone input has been detected."
                : "Microphone access is off."
        case "accessibility":
            return "Accessibility is off, so Speecher cannot paste your dictation."
        default:
            return ""
        }
    }

    /// What the ready step confirms once every gate passes: one line per step
    /// that chose something, naming the choice rather than repeating the step.
    var readyChecklist: [ReadyItem] {
        var items: [ReadyItem] = []
        if let speech = selectedSpeechProvider {
            let signIn = model.bridge.setupUsesCliproxy(provider: speech.id)
                ? " (CLI Proxy API)" : ""
            items.append(ReadyItem(id: "transcription",
                                   providerId: speech.id,
                                   symbol: "waveform",
                                   label: "Transcription — \(speech.label)\(signIn)",
                                   status: "Ready",
                                   ready: true))
        }
        items.append(ReadyItem(id: "microphone",
                               providerId: "",
                               symbol: "mic",
                               label: "Microphone — \(microphoneDeviceLabel)",
                               status: "Ready",
                               ready: true))
        items.append(ReadyItem(id: "delivery",
                               providerId: "",
                               symbol: "keyboard",
                               label: "Text delivery — paste with Cmd+V",
                               status: "Ready",
                               ready: true))
        // Refinement is the one line that can say something other than Ready:
        // it is never gated, so this step is reachable with None chosen or
        // with a provider that is not signed in.
        if let refinement = selectedRefinementProvider {
            items.append(ReadyItem(id: "refinement",
                                   providerId: refinement.id,
                                   symbol: "wand.and.stars",
                                   label: "Refinement — \(refinement.label)",
                                   status: refinement.ready ? "Ready" : "Not signed in",
                                   ready: refinement.ready))
        } else {
            items.append(ReadyItem(id: "refinement",
                                   providerId: "none",
                                   symbol: "minus.circle",
                                   label: "Refinement — None",
                                   status: "No cleanup",
                                   ready: false))
        }
        return items
    }

    /// What the audio device row currently names, for the ready checklist.
    private var microphoneDeviceLabel: String {
        guard let row = model.row("audioDevice") else { return "system default" }
        let selected = RowView.text(row.value)
        return row.options.first { $0.rowOptionId == selected }?.label ?? "system default"
    }

    /// The ready step's "Go to step", which has to put the step being left
    /// away exactly as Back and Continue do.
    func goTo(step index: Int) {
        jump(to: index)
    }

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
    /// Whether a probe has answered about the selected service at all, which
    /// is what separates "not set up" from "not asked yet".
    var providerProbed: Bool { selectedSpeechProvider?.probed ?? false }

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
        // A directory typed but not yet submitted still counts: Check Again
        // must check what the person sees, not the last committed value.
        commitTypedDirectory()
        refreshCliproxy()
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
        commitTypedDirectory()
        model.setValue(id, for: "speechProvider")
        refreshCliproxy()
    }

    // MARK: CLI Proxy API sign-in

    /// Re-reads everything the sign-in section shows for the selected service.
    /// It never writes `cliproxyDirectory`: a refresh can land mid-typing, and
    /// resetting the field would erase what the person is entering. The field
    /// is seeded at init and re-read only after an explicit commit.
    func refreshCliproxy() {
        cliproxyAvailable = model.bridge.setupCliproxyAccountsAvailable
        cliproxyDirectoryPlaceholder = model.bridge.setupCliproxyDirectoryPlaceholder
        let id = providerId
        signInSupported = model.bridge.setupSupportsCliproxy(provider: id)
        usingCliproxy = signInSupported && model.bridge.setupUsesCliproxy(provider: id)
        guard usingCliproxy else {
            cliproxyAccounts = []
            cliproxyAccount = ""
            return
        }
        cliproxyAccount = model.bridge.setupCliproxyAccount(provider: id)
        cliproxyAccounts = model.bridge.setupCliproxyAccountOptions(provider: id).map {
            CliproxyAccountChoice(id: $0.rowOptionId, label: $0.label, enabled: $0.enabled)
        }
    }

    func setUseCliproxy(_ use: Bool) {
        model.bridge.setSetupUseCliproxy(use, provider: providerId)
        refreshCliproxy()
        reprobeSelectedSpeechProvider()
    }

    func chooseCliproxyAccount(_ accountId: String) {
        model.bridge.setSetupCliproxyAccount(accountId, provider: providerId)
        refreshCliproxy()
        reprobeSelectedSpeechProvider()
    }

    func commitCliproxyDirectory() {
        guard cliproxyDirectory != model.bridge.setupCliproxyDirectory else { return }
        model.bridge.setSetupCliproxyDirectory(cliproxyDirectory)
        cliproxyDirectory = model.bridge.setupCliproxyDirectory
        refreshCliproxy()
        reprobeSelectedSpeechProvider()
    }

    /// The commit every other path shares: a directory typed but not submitted
    /// still counts when a check or a provider switch happens.
    private func commitTypedDirectory() {
        if cliproxyDirectory != model.bridge.setupCliproxyDirectory {
            model.bridge.setSetupCliproxyDirectory(cliproxyDirectory)
            cliproxyDirectory = model.bridge.setupCliproxyDirectory
        }
    }

    /// A sign-in change invalidates the selected service's verdict; showing
    /// "Checking…" holds the gate until the new probe answers.
    private func reprobeSelectedSpeechProvider() {
        if let index = speechProviders.firstIndex(where: { $0.id == providerId }) {
            speechProviders[index].probed = false
        }
        checkSpeechProviders()
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
                // The weak capture belongs to the timer closure, so this inner
                // async closure still needs explicit self after the guard.
                guard let self else { return }
                let previous = self.microphonePermission
                self.refreshMicrophonePermission()
                // Access just granted in System Settings: the meter could not
                // have started before, so start it now that it can.
                if previous != .authorized, self.microphonePermission == .authorized, self.meterRunning {
                    self.startMeter()
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

// MARK: - Marks and statuses

/// The path data behind the two brand marks, copied from assets/brand.
///
/// Drawn rather than bundled: the app bundle's Resources are listed in the
/// CMakeLists.txt every front end shares, and these are a few hundred bytes of
/// geometry that no build step needs to carry. Each string is the `d`
/// attribute of its `<path>` verbatim, wrapped between tokens, where SVG
/// treats a newline as whitespace.
private enum BrandArt {
    static let claudeExtent: CGFloat = 24
    /// Both marks keep their own colours. A flat silhouette in the label
    /// colour is what carries no recognition at 20 points.
    static let claudeColour = Color(red: 217 / 255, green: 119 / 255, blue: 87 / 255)
    static let claude = """
        m4.7144 15.9555 4.7174-2.6471.079-.2307-.079-.1275h-.2307l-.7893-.0486-2.6956-.0729-2.3375-.0971
        -2.2646-.1214-.5707-.1215-.5343-.7042.0546-.3522.4797-.3218.686.0608 1.5179.1032 2.2767.1578 1.6514.0972 2.4468.255
        h.3886l.0546-.1579-.1336-.0971-.1032-.0972L6.973 9.8356l-2.55-1.6879-1.3356-.9714-.7225-.4918
        -.3643-.4614-.1578-1.0078.6557-.7225.8803.0607.2246.0607.8925.686 1.9064 1.4754 2.4893 1.8336.3643.3035.1457
        -.1032.0182-.0728-.164-.2733-1.3539-2.4467-1.445-2.4893-.6435-1.032-.17-.6194c-.0607-.255-.1032
        -.4674-.1032-.7285L6.287.1335 6.6997 0l.9957.1336.419.3642.6192 1.4147 1.0018 2.2282 1.5543 3.0296.4553.8985.2429.8318.091.255
        h.1579v-.1457l.1275-1.706.2368-2.0947.2307-2.6957.0789-.7589.3764-.9107.7468-.4918.5828.2793.4797.686
        -.0668.4433-.2853 1.8517-.5586 2.9021-.3643 1.9429h.2125l.2429-.2429.9835-1.3053 1.6514-2.0643.7286
        -.8196.85-.9046.5464-.4311h1.0321l.759 1.1293-.34 1.1657-1.0625 1.3478-.8804 1.1414-1.2628 1.7
        -.7893 1.36.0729.1093.1882-.0183 2.8535-.607 1.5421-.2794 1.8396-.3157.8318.3886.091.3946-.3278.8075
        -1.967.4857-2.3072.4614-3.4364.8136-.0425.0304.0486.0607 1.5482.1457.6618.0364h1.621l3.0175.2247.7892.522.4736.6376
        -.079.4857-1.2142.6193-1.6393-.3886-3.825-.9107-1.3113-.3279h-.1822v.1093l1.0929 1.0686 2.0035 1.8092 2.5075 2.3314.1275.5768
        -.3218.4554-.34-.0486-2.2039-1.6575-.85-.7468-1.9246-1.621h-.1275v.17l.4432.6496 2.3436 3.5214.1214 1.0807
        -.17.3521-.6071.2125-.6679-.1214-1.3721-1.9246L14.38 17.959l-1.1414-1.9428-.1397.079-.674 7.2552
        -.3156.3703-.7286.2793-.6071-.4614-.3218-.7468.3218-1.4753.3886-1.9246.3157-1.53.2853-1.9004.17
        -.6314-.0121-.0425-.1397.0182-1.4328 1.9672-2.1796 2.9446-1.7243 1.8456-.4128.164-.7164-.3704.0667
        -.6618.4008-.5889 2.386-3.0357 1.4389-1.882.929-1.0868-.0062-.1579h-.0546l-6.3385 4.1164-1.1293.1457
        -.4857-.4554.0608-.7467.2307-.2429 1.9064-1.3114Z
        """

    static let openAIExtent: CGFloat = 2406
    static let openAIColour = Color(red: 116 / 255, green: 170 / 255, blue: 156 / 255)
    /// The rounded tile the knot sits on.
    static let openAITile = """
        M1 578.4C1 259.5 259.5 1 578.4 1h1249.1c319 0 577.5 258.5 577.5 577.4V2406H578.4C259.5 2406 1 2147.5 1 1828.6
        V578.4z
        """
    /// One cell of the knot, which chatgpt.svg stamps six times with `<use>`
    /// at 60-degree turns about the centre of the viewBox.
    static let openAICell = """
        M1107.3 299.1c-197.999 0-373.9 127.3-435.2 315.3L650 743.5v427.9c0 21.4 11 40.4 29.4 51.4l344.5 198.515
        V833.3h.1v-27.9L1372.7 604c33.715-19.52 70.44-32.857 108.47-39.828L1447.6 450.3C1361 353.5 1237.1 298.5 1107.3 299.1
        zm0 117.5-.6.6c79.699 0 156.3 27.5 217.6 78.4-2.5 1.2-7.4 4.3-11 6.1L952.8 709.3c-18.4 10.4-29.4 30
        -29.4 51.4V1248l-155.1-89.4V755.8c-.1-187.099 151.601-338.9 339-339.2z
        """

    /// The transform of the nth stamp of the knot cell.
    static func openAITurn(_ turn: Int) -> CGAffineTransform {
        guard turn > 0 else { return .identity }
        let centre = openAIExtent / 2
        return CGAffineTransform(translationX: centre, y: centre)
            .rotated(by: .pi / 3 * CGFloat(turn))
            .translatedBy(x: -centre, y: -centre)
    }
}

/// A shape from SVG path data, fitted to the rect it is drawn in.
///
/// Only what the two brand marks use: M, L, H, V, C, Z and their relative
/// forms, with implicit repeats. No arcs, no quadratics, no exponents — read
/// off the two files rather than assumed.
private struct BrandShape: Shape {
    let commands: String
    /// The viewBox edge. Both marks are square, so one number does.
    let extent: CGFloat
    /// Applied in viewBox coordinates, which is where the knot's turns are.
    var turn: CGAffineTransform = .identity

    func path(in rect: CGRect) -> Path {
        let scale = min(rect.width, rect.height) / extent
        let fit = CGAffineTransform(translationX: rect.minX, y: rect.minY)
            .scaledBy(x: scale, y: scale)
        return Self.parse(commands).applying(turn).applying(fit)
    }

    private static func parse(_ commands: String) -> Path {
        var path = Path()
        var reader = Reader(commands)
        var current = CGPoint.zero
        var subpathStart = CGPoint.zero
        var command: Character?

        while true {
            if let letter = reader.letter() { command = letter }
            guard let active = command else { return path }
            let relative = active.isLowercase
            // Every point of a relative run is measured from where the run
            // started, so `current` only moves once the command is finished.
            func place(_ x: CGFloat, _ y: CGFloat) -> CGPoint {
                relative ? CGPoint(x: current.x + x, y: current.y + y) : CGPoint(x: x, y: y)
            }
            switch active.uppercased() {
            case "Z":
                path.closeSubpath()
                current = subpathStart
                // Z takes no numbers, so it cannot repeat: whatever comes next
                // is a command letter or the data has ended.
                command = nil
            case "M":
                guard let x = reader.number(), let y = reader.number() else { return path }
                let point = place(x, y)
                path.move(to: point)
                current = point
                subpathStart = point
                // Every further pair of a moveto run is a lineto.
                command = relative ? "l" : "L"
            case "L":
                guard let x = reader.number(), let y = reader.number() else { return path }
                let point = place(x, y)
                path.addLine(to: point)
                current = point
            case "H":
                guard let x = reader.number() else { return path }
                let point = CGPoint(x: relative ? current.x + x : x, y: current.y)
                path.addLine(to: point)
                current = point
            case "V":
                guard let y = reader.number() else { return path }
                let point = CGPoint(x: current.x, y: relative ? current.y + y : y)
                path.addLine(to: point)
                current = point
            case "C":
                guard let x1 = reader.number(), let y1 = reader.number(),
                      let x2 = reader.number(), let y2 = reader.number(),
                      let x = reader.number(), let y = reader.number() else { return path }
                let end = place(x, y)
                path.addCurve(to: end, control1: place(x1, y1), control2: place(x2, y2))
                current = end
            default:
                return path
            }
        }
    }

    /// A reader over path data, where numbers run together, a sign doubles as
    /// a separator and a leading dot is a number of its own.
    private struct Reader {
        private let characters: [Character]
        private var index = 0

        init(_ text: String) { characters = Array(text) }

        mutating func letter() -> Character? {
            skipSeparators()
            guard index < characters.count, characters[index].isLetter else { return nil }
            defer { index += 1 }
            return characters[index]
        }

        mutating func number() -> CGFloat? {
            skipSeparators()
            var text = ""
            if index < characters.count, characters[index] == "-" || characters[index] == "+" {
                text.append(characters[index])
                index += 1
            }
            var sawPoint = false
            while index < characters.count {
                let character = characters[index]
                if character.isNumber {
                    text.append(character)
                } else if character == ".", !sawPoint {
                    sawPoint = true
                    text.append(character)
                } else {
                    break
                }
                index += 1
            }
            // ".079" and "-.2307" are how these files write a number below one.
            // Spell the zero rather than trust a parser to infer it.
            if text.hasPrefix(".") {
                text = "0" + text
            } else if text.hasPrefix("-.") {
                text = "-0" + String(text.dropFirst())
            }
            return Double(text).map { CGFloat($0) }
        }

        private mutating func skipSeparators() {
            while index < characters.count,
                  characters[index] == "," || characters[index].isWhitespace {
                index += 1
            }
        }
    }
}

/// The Claude starburst, in the brand colour.
private struct ClaudeMark: View {
    var body: some View {
        BrandShape(commands: BrandArt.claude, extent: BrandArt.claudeExtent)
            .fill(BrandArt.claudeColour)
    }
}

/// The ChatGPT knot on its tile, as chatgpt.svg draws it.
private struct OpenAIMark: View {
    var body: some View {
        ZStack {
            BrandShape(commands: BrandArt.openAITile, extent: BrandArt.openAIExtent)
                .fill(BrandArt.openAIColour)
            ForEach(0 ..< 6) { turn in
                BrandShape(commands: BrandArt.openAICell,
                           extent: BrandArt.openAIExtent,
                           turn: BrandArt.openAITurn(turn))
                    .fill(Color.white)
            }
        }
    }
}

/// The mark a provider id wears. The registry's ids name the sign-in rather
/// than the brand: codex and openai are the same ChatGPT credential, claude
/// and anthropic the same Claude one.
private struct ProviderMark: View {
    let providerId: String
    /// What an id with no brand behind it draws instead.
    var fallback = "minus.circle"

    var body: some View {
        Group {
            switch providerId {
            case "codex", "openai":
                OpenAIMark()
            case "claude", "anthropic":
                ClaudeMark()
            default:
                Image(systemName: fallback)
                    .resizable()
                    .scaledToFit()
                    .foregroundStyle(.secondary)
            }
        }
        .frame(width: 20, height: 20)
        .accessibilityHidden(true)
    }
}

/// A verdict with the glyph that carries it, which is how every status in the
/// mockup is written.
private struct StatusLabel: View {
    enum Tone { case positive, pending, warning }

    let text: String
    let tone: Tone

    var body: some View {
        Label {
            Text(text).fixedSize(horizontal: false, vertical: true)
        } icon: {
            Image(systemName: symbol)
        }
        .foregroundStyle(colour)
    }

    private var symbol: String {
        switch tone {
        case .positive: return "checkmark"
        case .pending: return "minus.circle"
        case .warning: return "exclamationmark.triangle"
        }
    }

    private var colour: AnyShapeStyle {
        switch tone {
        case .positive: return AnyShapeStyle(.green)
        case .pending: return AnyShapeStyle(.secondary)
        case .warning: return AnyShapeStyle(.orange)
        }
    }

    /// The tone a provider row earns: nothing heard yet, ready, or not set up.
    static func tone(for provider: ProviderRow) -> Tone {
        guard provider.probed else { return .pending }
        return provider.ready ? .positive : .warning
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
                if !flow.intro(for: step).isEmpty {
                    Text(flow.intro(for: step))
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
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
            // of a step the person could not complete. The ready step drops it
            // in both its states: it is where the flow accounts for itself, and
            // skipping past that account is not a thing to offer there.
            if !flow.isLastStep, !flow.isReadyStep, flow.canSkip {
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
                Text("Speecher uses your existing ChatGPT or Claude sign-in, or an account "
                    + "saved by CLI Proxy API. Sign in to one of these, then choose Check again:")
                    .fixedSize(horizontal: false, vertical: true)
                ForEach(flow.speechProviders) { provider in
                    // The mark, then the sign-in's name with its verdict on the
                    // trailing edge, and the hint indented under the name: the
                    // stack starts past the mark, so nothing measures an inset.
                    HStack(alignment: .top, spacing: 8) {
                        ProviderMark(providerId: provider.id)
                        VStack(alignment: .leading, spacing: 4) {
                            HStack(alignment: .firstTextBaseline) {
                                Text(provider.credentialSource)
                                    .fontWeight(.semibold)
                                Spacer(minLength: 12)
                                // Not found is a fact here, not a fault: the
                                // step exists to say which sign-ins are there.
                                StatusLabel(text: provider.credentialStatus,
                                            tone: provider.ready ? .positive : .pending)
                            }
                            if provider.probed, !provider.ready, !provider.credentialHint.isEmpty {
                                Text(provider.credentialHint)
                                    .font(.callout)
                                    .foregroundStyle(.secondary)
                                    .fixedSize(horizontal: false, vertical: true)
                            }
                        }
                    }
                }
                // Accounts saved by CLI Proxy API count as a sign-in of their
                // own: someone whose only login lives there opts in on the
                // Transcription step. The directory is enterable right here,
                // because a custom-directory user would otherwise be held on
                // this step with the field that could free them gated behind
                // Continue.
                VStack(alignment: .leading, spacing: 4) {
                    HStack(alignment: .firstTextBaseline) {
                        Text("CLI Proxy API")
                            .fontWeight(.semibold)
                        Spacer(minLength: 12)
                        StatusLabel(text: flow.cliproxyAvailable ? "Accounts found" : "Not found",
                                    tone: flow.cliproxyAvailable ? .positive : .pending)
                    }
                    Text(flow.cliproxyAvailable
                        ? "To use one of these accounts, turn on \"Use a CLI Proxy API "
                            + "account\" on the Transcription step."
                        : "Optional: Claude and Codex accounts saved by CLI Proxy API also "
                            + "work. If yours live in a custom directory, enter it below.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                    if !flow.cliproxyAvailable {
                        TextField("CLI Proxy API directory",
                                  text: $flow.cliproxyDirectory,
                                  prompt: Text(flow.cliproxyDirectoryPlaceholder))
                            .labelsHidden()
                            .onSubmit { flow.commitCliproxyDirectory() }
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

/// One selectable provider: its mark, its name, for refinement the sign-in it
/// borrows, and the probe's verdict on the trailing edge.
private struct ProviderOptionLabel: View {
    let providerId: String
    let title: String
    let status: String
    let tone: StatusLabel.Tone
    var note = ""

    var body: some View {
        HStack(alignment: .top, spacing: 8) {
            ProviderMark(providerId: providerId)
            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                    .fontWeight(.semibold)
                if !note.isEmpty {
                    Text(note)
                        .font(.callout)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            Spacer(minLength: 12)
            StatusLabel(text: status, tone: tone)
        }
        // A radio's label is offered the row, not only what it needs, which is
        // what puts the status against the trailing edge.
        .frame(maxWidth: .infinity, alignment: .leading)
        // The composed row otherwise reaches accessibility as an unnamed
        // element: VoiceOver users (and the E2E driver) need the provider's
        // name on the radio itself, with the status readable alongside.
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(Text(title))
        .accessibilityValue(Text(status))
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
                        ProviderOptionLabel(providerId: provider.id,
                                            title: provider.label,
                                            status: provider.readinessStatus,
                                            tone: StatusLabel.tone(for: provider))
                            .tag(provider.id)
                    }
                } label: {
                    Text("Transcription service")
                }
                .pickerStyle(.radioGroup)
                ProviderStatsRows(stats: model.bridge.stats(forSpeechProvider: flow.providerId))
                // Under the facts about the chosen service, matching the Qt and
                // Windows steps. The bridge already drops rows whose schema
                // visibility fails, so this only appears for the codex provider.
                if let row = model.row("codexFinalRetranscribe") {
                    RowView(row: row, model: model)
                }
                // The verdict and the way to ask again sit on one row, so a
                // held Continue and its remedy are read together.
                if !flow.providerStatus.isEmpty {
                    HStack(alignment: .firstTextBaseline) {
                        StatusLabel(text: flow.providerStatus, tone: statusTone)
                        Spacer(minLength: 12)
                        if !flow.providerReady {
                            Button("Check Again") { flow.checkSpeechProviders() }
                        }
                    }
                }
            } footer: {
                if !flow.providerReady, !flow.providerHint.isEmpty {
                    Text(flow.providerHint)
                }
            }
            // The service's own sign-in stays the silent default; CLI Proxy API
            // is the exception this toggle opts into, matching the Qt and
            // Windows assistants.
            if flow.signInSupported {
                Section("Sign-in") {
                    Toggle("Use a CLI Proxy API account instead of the service's own sign-in",
                           isOn: Binding(get: { flow.usingCliproxy },
                                         set: { flow.setUseCliproxy($0) }))
                    if flow.usingCliproxy {
                        Picker("CLI Proxy API account",
                               selection: Binding(get: { flow.cliproxyAccount },
                                                  set: { flow.chooseCliproxyAccount($0) })) {
                            ForEach(flow.cliproxyAccounts) { choice in
                                Text(choice.label)
                                    .tag(choice.id)
                                    // A disabled account is kept visible but not
                                    // selectable, matching the Qt and Windows rows.
                                    .selectionDisabled(!choice.enabled)
                            }
                        }
                        TextField("Account directory",
                                  text: $flow.cliproxyDirectory,
                                  prompt: Text(flow.cliproxyDirectoryPlaceholder))
                            .onSubmit { flow.commitCliproxyDirectory() }
                    }
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

    private var statusTone: StatusLabel.Tone {
        if flow.providerReady { return .positive }
        return flow.providerProbed ? .warning : .pending
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
                    .fixedSize(horizontal: false, vertical: true)
            }
            Section {
                // Both of these carry a sentence rather than a name, and the
                // dialog is a fixed width: they wrap here rather than run out
                // of the card.
                ForEach(model.rows(matching: ["outputFormat", "restoreClipboardAfterTyping"]),
                        id: \.rowId) { row in
                    RowView(row: row, model: model)
                        .fixedSize(horizontal: false, vertical: true)
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
                        ProviderOptionLabel(providerId: row.id,
                                            title: row.label,
                                            status: row.readinessStatus,
                                            tone: StatusLabel.tone(for: row),
                                            note: row.setupHint)
                            .tag(row.id)
                    }
                    ProviderOptionLabel(providerId: "none",
                                        title: "None",
                                        status: "No cleanup",
                                        tone: .pending,
                                        note: "Skip cleanup entirely.")
                        .tag("none")
                } label: {
                    Text("Cleanup provider")
                }
                .pickerStyle(.radioGroup)
                // Directly under the rows, above the facts, so it reads as
                // attached to the selection it is about.
                if !flow.refinementWarning.isEmpty {
                    StatusLabel(text: flow.refinementWarning, tone: .warning)
                }
                // None has no facts worth a block, so choosing it hides them.
                ProviderStatsRows(stats: model.bridge.stats(forRefinementProvider: provider))
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
            } footer: {
                Text("The default profile is used when Speecher does not recognise the "
                    + "app you are dictating into. Every profile can be changed later "
                    + "in Settings.")
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
                // The key itself reads as a fact on its own row; the button
                // below is what changes it.
                HStack(alignment: .firstTextBaseline, spacing: 8) {
                    Image(systemName: "keyboard")
                        .foregroundStyle(.secondary)
                        .accessibilityHidden(true)
                    Text("Dictation key")
                    Spacer(minLength: 12)
                    Text(flow.pendingShortcut.display)
                        .fontWeight(.semibold)
                }
                LabeledContent {
                    Button(caption) { record() }
                        .disabled(!flow.createShortcut)
                } label: {
                    Text("Press a key combination, or a single key such as "
                         + "Right Option or F13.")
                        .fixedSize(horizontal: false, vertical: true)
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

    private func record() {
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

    private var caption: String {
        recorder.recording ? "Press a Key…" : "Set Shortcut"
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
            if flow.blockedSteps.isEmpty {
                complete
            } else {
                blocked
            }
        }
        .formStyle(.grouped)
        // Finishing is two steps away, so the sign-in the welcome step found is
        // re-probed here rather than trusted: one that expired while the
        // assistant sat open shuts the gate again before Finish is offered.
        .onAppear { flow.checkSpeechProviders() }
    }

    @ViewBuilder private var complete: some View {
        Section {
            StatusLabel(text: "Setup is complete.", tone: .positive)
        }
        Section("How to dictate") {
            Text(flow.createShortcut
                ? "Finishing registers \(flow.pendingShortcut.display) as the "
                    + "dictation shortcut. To dictate, \(flow.activationInstruction)."
                : "Finishing completes setup without a dictation shortcut.")
                .fixedSize(horizontal: false, vertical: true)
            Text("Speecher stays out of the way until you press it. Its menu bar "
                + "icon shows when it is listening.")
                .font(.callout)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        Section {
            ForEach(flow.readyChecklist) { item in
                HStack(alignment: .firstTextBaseline, spacing: 8) {
                    ProviderMark(providerId: item.providerId, fallback: item.symbol)
                    Text(item.label)
                    Spacer(minLength: 12)
                    StatusLabel(text: item.status, tone: item.ready ? .positive : .pending)
                }
            }
        } footer: {
            Text(flow.readyStatus)
        }
    }

    /// Never a silently disabled Finish: every gate still shut is named here,
    /// with what is missing and the way back to the step that fixes it.
    @ViewBuilder private var blocked: some View {
        Section {
            ForEach(flow.blockedSteps) { step in
                HStack(alignment: .top, spacing: 8) {
                    Image(systemName: "exclamationmark.triangle")
                        .foregroundStyle(.orange)
                        .accessibilityHidden(true)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(step.title)
                            .fontWeight(.semibold)
                        Text(step.reason)
                            .font(.callout)
                            .foregroundStyle(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    Spacer(minLength: 12)
                    Button("Go to Step") { flow.goTo(step: step.index) }
                }
            }
        } header: {
            Text("A few steps still need attention:")
        } footer: {
            // The mockup writes "Finish" here because its ready page is the
            // last one. macOS keeps Start at login after this step, so the
            // button being held is Continue, and naming Finish would be a lie.
            Text("Continue becomes available once every step above is resolved.")
        }
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
