import AppKit
import SwiftUI

// Speecher as SwiftUI sees it. Everything arrives through SpeecherBridge, which
// is this target's bridging header, so there is no C++ here.

/// The settings surface and the live dictation state.
///
/// The bridge hands over immutable snapshots of the schema, so a write goes back
/// through the bridge and the snapshot is taken again — which is also what
/// re-derives the rows a changed value gates.
@MainActor
final class AppModel: ObservableObject {
    @Published private(set) var pages: [SettingsPageModel]
    /// The sidebar's panes and their groups, as the schema arranges them.
    /// Fixed for the life of the app, so a plain let.
    let panes: [Pane]
    let sidebarGroups: [SidebarGroupModel]
    /// The pages a step below a pane, such as Dictation's Fallbacks.
    let subpages: [Pane]
    /// What dictation is doing, in the words and controls core gives every
    /// platform, re-read whole on every state change.
    @Published private(set) var status: String
    @Published private(set) var dictationState: SpeecherDictationState
    @Published private(set) var listening: Bool
    @Published private(set) var recording: Bool
    @Published private(set) var toggleLabel: String
    @Published private(set) var toggleEnabled: Bool
    /// The buttons either side of the waveform, and whether the menu bar
    /// panel offers Cancel.
    @Published private(set) var pauseVisible: Bool
    @Published private(set) var pauseEnabled: Bool
    @Published private(set) var paused: Bool
    @Published private(set) var cancelVisible: Bool
    @Published private(set) var cancelable: Bool
    @Published private(set) var level: Float = 0
    /// The last thing Speecher heard, which the menu bar panel offers to copy.
    @Published private(set) var transcript: String
    /// Why the last session failed, until the next one starts; empty while the
    /// status itself says it.
    @Published private(set) var failureNote: String
    /// What fixes the last failure, as its popup offered it, for as long as
    /// the failure is still being reported.
    @Published private(set) var failureFix: SpeecherErrorAction?
    /// The transcript was just copied, so the button says so for a moment.
    @Published private(set) var transcriptCopied = false
    @Published private(set) var accessibilityEnabled: Bool
    @Published private(set) var whatsNewPending: Bool
    /// The update banner as core words it, re-read whole on every change.
    @Published private(set) var update: SpeecherUpdateBanner
    /// What LocalSetup knows about running models on this computer, re-read
    /// whole on every change it announces.
    @Published private(set) var local: LocalSetupState
    /// Why the accessibility grant could not be asked for, when it could not.
    @Published var accessibilityProblem = ""
    /// The app settings key, which lives in the keyring rather than in the
    /// settings, so the schema knows nothing about it.
    @Published var apiKey = ""
    @Published var credentialProblem = ""
    // Resolving the OpenAI status can enter the keyring, so it is read on
    // loadApiKey()'s deferred turn, never from a SwiftUI body. nil until then.
    @Published private(set) var credentialStatus: SpeecherCredentialStatus?
    @Published private(set) var anthropicCredentialStatus: SpeecherCredentialStatus
    // Bumped when the settings draft is reloaded (window reopen, setup), so
    // retained collection editors can reload from the fresh snapshot.
    @Published private(set) var draftGeneration = 0
    // Bumped when `speecher vocabulary add` saved terms, so the Terms table
    // alone reloads: reloading every table would drop a cell being typed in.
    @Published private(set) var vocabularyGeneration = 0
    @Published private(set) var shortcut: String
    /// The Cancel and Pause Shortcut rows, by role.
    @Published private(set) var sessionShortcuts: [SpeecherShortcutRole: SessionShortcut]
    /// The Test microphone row's test, which the bridge runs. Mirrored here
    /// rather than in the row, which a Form drops when it scrolls off; the
    /// window ends the test on a pane change and on close.
    @Published private(set) var microphoneTestCaption: String
    @Published private(set) var microphoneTestEnabled: Bool
    @Published private(set) var microphoneTestLevel: Float = 0
    /// Why the device would not open, until the next test starts.
    @Published private(set) var microphoneTestProblem = ""
    @Published private(set) var shortcutProblem = ""
    /// The non-blocking caveat the last single-key binding earned, such as
    /// "E still types". Saved anyway; this only informs.
    @Published private(set) var shortcutWarning = ""
    /// The last single-key binding was refused for the missing Accessibility
    /// grant, which is what makes the grant call-to-action appear.
    @Published private(set) var shortcutNeedsAccessibility = false
    /// The pane the sidebar is on. A window opened from closed starts on
    /// reopenPane.
    @Published var pane = "home" {
        didSet {
            guard pane != oldValue else { return }
            subpage = nil
            activeShortcutRecorder?.stop()
            bridge.stopMicrophoneTest()
            if pane != "whatsNew", !Self.screenshotRun {
                UserDefaults.standard.set(pane, forKey: Self.lastPaneKey)
            }
        }
    }
    /// The subpage showing in place of the pane, by id; the sidebar keeps the
    /// pane selected and Back returns to it.
    @Published var subpage: String?
    private static let lastPaneKey = "lastSettingsPane"
    /// A screenshot run names a page or a window size. It neither reads nor
    /// stores the last pane, so what it grabs does not depend on what the
    /// machine showed before, and the pane the machine reopens stays as it was.
    private static let screenshotRun = ["SPEECHER_GRAB_PAGE", "SPEECHER_GRAB_SIZE"]
        .contains { !(ProcessInfo.processInfo.environment[$0] ?? "").isEmpty }

    /// Where a settings window opened from closed starts: the pane last shown,
    /// or Home.
    var reopenPane: String {
        Self.screenshotRun ? "home" : UserDefaults.standard.string(forKey: Self.lastPaneKey) ?? "home"
    }
    /// The recorder whose recording is in progress, if any. Stopped directly
    /// when the pane changes or the settings window closes, rather than
    /// through a view, which SwiftUI may already have taken away.
    private weak var activeShortcutRecorder: ShortcutRecorder?

    /// Home's numbers for the chosen period, re-read whole when the log, the
    /// Insights setting or the period changes.
    @Published private(set) var insights: SpeecherInsightsModel
    @Published private(set) var insightsEnabled: Bool
    @Published var insightsRange = SpeecherInsightsRange.last30Days {
        didSet { refreshInsights() }
    }
    /// "12 words, Mail, today": the last transcript's length and, when
    /// insights recorded it, where it went and when.
    @Published private(set) var transcriptDetail = ""
    /// The Clear insights history row asked, and the confirmation is up.
    @Published var confirmingClearInsights = false
    /// Deleting the history failed, and the alert saying so is up.
    @Published var clearInsightsFailed = false
    /// The transfer the "Copy settings to your phone" sheet shows while it is
    /// up. Replacing or clearing it stops the old one, which closes its port.
    @Published var phoneTransfer: SpeecherPhoneTransfer? {
        didSet { if oldValue !== phoneTransfer { oldValue?.stop() } }
    }
    /// The view an alternatives pane should switch to when it next shows,
    /// by view id; the pane clears it once it has.
    @Published var requestedView: String? = nil
    /// The row a search led to, which the pane scrolls to once it shows; the
    /// pane clears it once it has.
    @Published var requestedRow: String? = nil
    /// Brings the settings window up on a page, which SpeecherMacUI owns.
    var openSettingsPage: (String) -> Void = { _ in }

    let bridge: SpeecherBridge
    /// The Transcribe pane's batch, kept here so it outlives the pane's view:
    /// a batch keeps running while another pane is on screen.
    let transcription: TranscriptionModel
    /// The pane that was showing when What's New opened.
    private var whatsNewReturnPane = "home"
    /// A keyring read that lands after typing started must not overwrite it.
    private var apiKeyEdits = 0
    private var apiKeyLoaded = false
    /// Whether the slow rows have been asked for once, after which asking again
    /// costs nothing new.
    private var deferredLoaded = false
    private var keyWindowObserver: NSObjectProtocol?

    var accessibilitySupported: Bool { bridge.accessibilitySupported }
    var shortcutSupported: Bool { bridge.shortcutSupported }

    init(bridge: SpeecherBridge) {
        self.bridge = bridge
        transcription = TranscriptionModel(bridge: bridge)
        pages = bridge.settingsSchema.pages
        let panes = bridge.settingsSchema.panes.map(Pane.init)
        self.panes = panes
        sidebarGroups = bridge.settingsSchema.sidebarGroups
        subpages = bridge.settingsSchema.subpages.map(Pane.init)
        status = bridge.statusLabel
        dictationState = bridge.dictationState
        listening = bridge.listening
        recording = bridge.recording
        toggleLabel = bridge.toggleLabel
        toggleEnabled = bridge.toggleEnabled
        pauseVisible = bridge.pauseVisible
        pauseEnabled = bridge.pauseEnabled
        paused = bridge.paused
        cancelVisible = bridge.cancelVisible
        cancelable = bridge.cancelable
        transcript = bridge.lastTranscript
        failureNote = bridge.failureNote
        local = bridge.localSetupState
        shortcut = bridge.shortcutDisplay
        sessionShortcuts = Dictionary(uniqueKeysWithValues: Self.sessionShortcutRoles.map {
            ($0, SessionShortcut(display: bridge.shortcutDisplay(for: $0)))
        })
        microphoneTestCaption = bridge.microphoneTestCaption
        microphoneTestEnabled = bridge.microphoneTestEnabled
        accessibilityEnabled = bridge.accessibilityEnabled
        whatsNewPending = bridge.whatsNewPending
        update = bridge.updateBanner
        anthropicCredentialStatus = bridge.anthropicCredentialStatus
        insights = bridge.insightsSummary(range: .last30Days)
        insightsEnabled = bridge.insightsEnabled
        bridge.statusChanged = { [weak self] status in
            guard let self else { return }
            self.status = status
            dictationState = self.bridge.dictationState
            failureNote = self.bridge.failureNote
            if failureNote.isEmpty, dictationState != .error { failureFix = nil }
            listening = self.bridge.listening
            recording = self.bridge.recording
            toggleLabel = self.bridge.toggleLabel
            toggleEnabled = self.bridge.toggleEnabled
            pauseVisible = self.bridge.pauseVisible
            pauseEnabled = self.bridge.pauseEnabled
            paused = self.bridge.paused
            cancelVisible = self.bridge.cancelVisible
            cancelable = self.bridge.cancelable
        }
        bridge.microphoneTestChanged = { [weak self] in
            guard let self else { return }
            microphoneTestCaption = self.bridge.microphoneTestCaption
            microphoneTestEnabled = self.bridge.microphoneTestEnabled
            switch self.bridge.microphoneTestState {
            case .starting: microphoneTestProblem = ""
            case .running: break
            default: microphoneTestLevel = 0
            }
        }
        bridge.microphoneTestLevelChanged = { [weak self] level in
            self?.microphoneTestLevel = level
        }
        bridge.microphoneTestFailed = { [weak self] message in
            self?.microphoneTestProblem = message
        }
        bridge.audioLevelChanged = { [weak self] level in
            self?.level = level
        }
        bridge.transcriptChanged = { [weak self] transcript in
            self?.transcript = transcript
            self?.refreshTranscriptDetail()
        }
        bridge.insightsChanged = { [weak self] in
            self?.refreshInsights()
        }
        bridge.lastRecordChanged = { [weak self] in
            self?.refreshTranscriptDetail()
        }
        // Every edit is committed as it is made, so re-reading loses none.
        bridge.vocabularyAdded = { [weak self] in
            guard let self else { return }
            self.bridge.settingsSchema.reloadDraft()
            pages = self.bridge.settingsSchema.pages
            vocabularyGeneration += 1
        }
        // These closures must reach the bridge through self: the bridge owns
        // them, so capturing the local `bridge` would retain it in a cycle and
        // its dealloc — the watcher, the signal connections — would never run.
        bridge.accessibilityChanged = { [weak self] in
            guard let self else { return }
            accessibilityEnabled = self.bridge.accessibilityEnabled
            pages = self.bridge.settingsSchema.pages
        }
        bridge.anthropicCredentialsChanged = { [weak self] in
            guard let self else { return }
            anthropicCredentialStatus = self.bridge.anthropicCredentialStatus
            // The watcher also fires for the Codex CLI's file, so the OpenAI
            // status line is worth re-reading too — once the keyring is warm.
            if apiKeyLoaded {
                credentialStatus = self.bridge.credentialStatus
            }
        }
        bridge.whatsNewChanged = { [weak self] in
            guard let self else { return }
            whatsNewPending = self.bridge.whatsNewPending
        }
        bridge.updateChanged = { [weak self] in
            self?.refreshUpdate()
        }
        bridge.localSetupChanged = { [weak self] rowsChanged in
            guard let self else { return }
            local = self.bridge.localSetupState
            // Endpoint verdicts, runners and model lists are row text, and
            // LocalSetup writes settings (a pulled model, a Speed Test), so
            // the draft is re-read too. Every edit here is already committed.
            if rowsChanged {
                self.bridge.settingsSchema.reloadDraft()
                pages = self.bridge.settingsSchema.pages
            }
        }
        refreshTranscriptDetail()
        // A snapshot reuses the last device list, so a microphone plugged in
        // while Speecher was in the background is listed when one of its
        // windows comes forward.
        keyWindowObserver = NotificationCenter.default.addObserver(
            forName: NSWindow.didBecomeKeyNotification, object: nil, queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated { self?.refreshAudioInputs() }
        }
    }

    /// Stops what the model hears from outside the controller: a window coming
    /// forward, and the app becoming active or a credential file changing.
    func stopObserving() {
        if let keyWindowObserver { NotificationCenter.default.removeObserver(keyWindowObserver) }
        bridge.anthropicCredentialsChanged = nil
    }

    /// Re-reads Home's numbers. Home calls it when it appears, so a day that
    /// turned over while the window stayed open is picked up.
    func refreshInsights() {
        insightsEnabled = bridge.insightsEnabled
        insights = bridge.insightsSummary(range: insightsRange)
        refreshTranscriptDetail()
    }

    private func refreshTranscriptDetail() {
        let words = bridge.lastTranscriptWords
        transcriptDetail = (["\(words.formatted()) \(words == 1 ? "word" : "words")",
                             bridge.lastRecordApp, bridge.lastRecordDay])
            .filter { !$0.isEmpty }
            .joined(separator: ", ")
    }

    var learnedCorrectionCount: Int { bridge.learnedCorrectionCount }

    func clearInsights() {
        let cleared = bridge.clearInsights()
        // On the next turn: this runs from the confirmation dialog's button,
        // and an alert raised while that dialog is still dismissing is lost.
        DispatchQueue.main.async { [weak self] in
            self?.clearInsightsFailed = !cleared
        }
    }

    /// The popup reported a failure with this fix.
    func noteFailure(fix: SpeecherErrorAction) {
        failureFix = fix.fix == .none ? nil : fix
    }

    /// A dictation that never started for a reason the user has to fix, such
    /// as the microphone grant. No session failed, so the reason is held here
    /// rather than read from the session, until the next session starts.
    func noteRefusedStart(_ message: String, fix: SpeecherErrorAction) {
        failureNote = message
        failureFix = fix
    }

    /// Runs what an error offered: a settings page, the Accessibility grant,
    /// or the system panel for a permission.
    func perform(_ fix: SpeecherErrorAction) {
        switch fix.fix {
        case .settingsPage:
            openSettingsPage(fix.pageId)
        case .accessibilityPermission:
            requestAccessibility()
        default:
            if let url = fix.systemSettingsURL { NSWorkspace.shared.open(url) }
        }
    }

    /// Shows a page by id: a pane id, "pane:view" for one of its views, or a
    /// subpage's id. An unknown id shows Home (speecher::resolvePage). A row id
    /// scrolls the pane to that row, and on an Alternatives pane picks the view
    /// holding it.
    func showPage(_ pageId: String, row rowId: String? = nil) {
        let page = bridge.settingsSchema.resolvePage(pageId)
        if !page[1].isEmpty { requestedView = page[1] }
        if let rowId,
           let group = pane(withId: page[0])?.groups.first(where: { $0.rows.contains(rowId) }) {
            if !group.view.isEmpty { requestedView = group.view }
            requestedRow = rowId
        }
        if page[0] == "whatsNew" {
            showWhatsNew()
        } else {
            pane = page[0]
        }
        // After the pane, whose change closes any subpage.
        subpage = page[2].isEmpty ? nil : page[2]
    }

    /// What a window showing local models asks for on the way up: the
    /// hardware (probed once) and the runners (looked for every time), which
    /// refinement uses even where speech cannot run here.
    func refreshLocalSetup() {
        bridge.probeLocalHardware()
        bridge.detectLocalRunners()
    }

    private func refreshUpdate() {
        update = bridge.updateBanner
        // The Check for updates row's caption and help follow the update too.
        pages = bridge.settingsSchema.pages
    }

    var whatsNewBanner: SpeecherWhatsNewBanner { bridge.whatsNewBanner }

    func runUpdateAction() { bridge.runUpdateAction() }
    func deferUpdate() { bridge.deferUpdate() }
    func dismissUpdate() { bridge.dismissUpdate() }

    /// The work the first frame must not wait for: enumerating audio devices,
    /// listing CLI Proxy API accounts, and reading the keyring.
    func loadDeferredRows() {
        guard !deferredLoaded else { return }
        deferredLoaded = true
        bridge.settingsSchema.loadExpensiveRows()
        pages = bridge.settingsSchema.pages
        // Only the keyring can stop to ask for an unlock, so it waits another
        // turn rather than holding up the other two.
        DispatchQueue.main.async { [weak self] in self?.loadApiKey() }
    }

    private func refreshAudioInputs() {
        bridge.settingsSchema.refreshAudioInputs()
        pages = bridge.settingsSchema.pages
    }

    func reloadSettingsDraft() {
        bridge.settingsSchema.reloadDraft()
        pages = bridge.settingsSchema.pages
        // After pages, so an editor reacting to the bump reads fresh rows.
        draftGeneration += 1
    }

    private func loadApiKey() {
        let edits = apiKeyEdits
        let key = bridge.readApiKey()
        apiKeyLoaded = true
        if edits == apiKeyEdits {
            apiKey = key
        }
        // The keyring is warm now, so the resolved status can be cached for
        // CredentialField instead of being read inside its body.
        credentialStatus = bridge.credentialStatus
    }

    func pane(withId id: String) -> Pane? {
        panes.first { $0.id == id }
    }

    /// Back from a subpage to the pane it belongs to, at the row that opened
    /// it, or from What's New to the pane it was opened from.
    func goBack() {
        guard let subpage else {
            pane = whatsNewReturnPane
            return
        }
        requestedRow = pages.flatMap(\.sections).flatMap(\.rows).first { $0.targetPage == subpage }?.rowId
        self.subpage = nil
    }

    /// The page the detail column shows: the subpage open, else the pane.
    var shownPage: Pane? {
        subpages.first { $0.id == subpage } ?? pane(withId: pane)
    }

    func row(_ rowId: String) -> SettingsRowModel? {
        for page in pages {
            for section in page.sections {
                if let row = section.rows.first(where: { $0.rowId == rowId }) {
                    return row
                }
            }
        }
        return nil
    }

    /// The rows with these ids that the schema currently offers, in this order.
    func rows(matching ids: [String]) -> [SettingsRowModel] {
        ids.compactMap(row)
    }

    /// A pane's groups, in order, each with the rows the schema currently offers
    /// it. A group whose rows are all hidden stays in the list and draws
    /// nothing, so the index a segmented picker holds keeps meaning what it did.
    func groupCards(for pane: Pane) -> [PaneCard] {
        pane.groups.map { group in
            PaneCard(title: group.title, help: footnote(group), rows: group.rows.compactMap(row))
        }
    }

    /// The group's footnote, or the help of a row that fills the whole card
    /// and so has nowhere else to put it.
    private func footnote(_ group: PaneGroup) -> String {
        if !group.help.isEmpty { return group.help }
        return group.rows.compactMap(row).first { $0.collection != nil }?.help ?? ""
    }

    func trigger(_ rowId: String) {
        if rowId == "whatsNew" { showWhatsNew() }
        if rowId == "speechLocalModelDownload" {
            showPage("localModels")
            return
        }
        // Asked here rather than in MacFrontEnd.mm: the confirmation is a
        // SwiftUI dialog on the settings window (RootView).
        if rowId == "clearInsights" {
            confirmingClearInsights = true
            return
        }
        // A SwiftUI sheet on the settings window too (RootView).
        if rowId == "copyToPhone" {
            phoneTransfer = bridge.startPhoneTransfer()
            return
        }
        if rowId == "resetCustomSystemPrompt" {
            setValue(bridge.builtInSystemPrompt, for: "customSystemPrompt")
            return
        }
        if rowId == "openMicrophoneSettings" {
            perform(SpeecherErrorAction(fix: .microphonePermission, pageId: ""))
            return
        }
        // Every schema action, enableAccessibility included, goes to the
        // front end's one dispatcher (MacFrontEnd.mm).
        guard Self.testsTypedText.contains(rowId) else {
            bridge.settingsSchema.actionTriggered?(rowId)
            return
        }
        // A button click leaves a field being typed in still editing, and the
        // field saves when it lets go: end that first, and test a turn later,
        // so the address on screen is the one tested.
        NSApp.keyWindow?.makeFirstResponder(nil)
        DispatchQueue.main.async { [bridge] in
            bridge.settingsSchema.actionTriggered?(rowId)
        }
    }

    /// The actions that read what the fields beside them hold.
    private static let testsTypedText: Set<String> = ["speechEndpointTest", "refinementEndpointTest"]

    func showWhatsNew() {
        if pane != "whatsNew" { whatsNewReturnPane = pane }
        pane = "whatsNew"
        bridge.clearPendingWhatsNew()
    }

    func dismissWhatsNew() {
        bridge.clearPendingWhatsNew()
    }

    func setValue(_ value: Any?, for rowId: String) {
        bridge.settingsSchema.setValue(value, forRowId: rowId)
        bridge.settingsSchema.commit()
        pages = bridge.settingsSchema.pages
        if rowId == "anthropicAuthMode" {
            anthropicCredentialStatus = bridge.anthropicCredentialStatus
        }
        if rowId == "insightsEnabled" {
            refreshInsights()
        }
        if apiKeyLoaded,
           ["openAiAuthMode", "openAiCliproxyAccount", "cliproxyOauthDir",
            "cliproxyBaseUrl", "cliproxyApiKey"].contains(rowId) {
            credentialStatus = bridge.credentialStatus
        }
    }

    func binding<Value>(_ row: SettingsRowModel,
                        read: @escaping (Any?) -> Value,
                        write: @escaping (Value) -> Any) -> Binding<Value> {
        Binding(get: { read(row.value) },
                set: { [weak self] newValue in
                    self?.setValue(write(newValue), for: row.rowId)
                })
    }

    /// Saves one edit of a fallback list, which the schema makes like any
    /// row's value.
    func editFallbacks(_ edit: (SettingsSchemaModel) -> Void) {
        edit(bridge.settingsSchema)
        bridge.settingsSchema.commit()
        pages = bridge.settingsSchema.pages
    }

    /// Empty when the records are consistent, in which case they are also saved.
    func save(records: [[String: Any]], previous: [[String: Any]], for rowId: String) -> [String] {
        let problems = bridge.settingsSchema.save(records: records, previousRecords: previous, forRowId: rowId)
        if problems.isEmpty {
            pages = bridge.settingsSchema.pages
        }
        return problems
    }

    func noteApiKeyEdited() {
        apiKeyEdits += 1
    }

    func saveApiKey() {
        guard bridge.credentialIsEditable, apiKeyLoaded || apiKeyEdits > 0 else { return }
        credentialProblem = bridge.saveApiKey(apiKey) ?? ""
    }

    func requestAccessibility() {
        accessibilityProblem = bridge.enableAccessibility() ?? ""
    }

    /// Writes the speecher command, asking for an administrator's password
    /// when its folder needs one, and says how it went. NSAppleScript rather
    /// than osascript, so the password prompt names Speecher as the app
    /// asking; it holds the main thread until the prompt is answered.
    /// AuthorizationExecuteWithPrivileges is deprecated, and a privileged
    /// helper is a lot of machinery for writing one file.
    func installCommandLineTool() {
        let userCancelled = -128 // userCanceledErr
        let folder = (SpeecherBridge.commandLineToolPath as NSString).deletingLastPathComponent
        let privileges = FileManager.default.isWritableFile(atPath: folder) ? "" : " with administrator privileges"
        let command = SpeecherBridge.commandLineToolInstallCommand
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "\"", with: "\\\"")
        let script = NSAppleScript(source: "do shell script \"\(command)\"\(privileges)")
        var error: NSDictionary?
        script?.executeAndReturnError(&error)
        if (error?[NSAppleScript.errorNumber] as? Int) == userCancelled { return }
        let alert = NSAlert()
        if script != nil && error == nil {
            alert.messageText = SpeecherBridge.commandLineToolInstalledTitle
            alert.informativeText = SpeecherBridge.commandLineToolInstalledText
        } else {
            alert.alertStyle = .warning
            alert.messageText = SpeecherBridge.commandLineToolFailedTitle
            alert.informativeText = error?[NSAppleScript.errorMessage] as? String ?? ""
        }
        NSApp.activate(ignoringOtherApps: true)
        alert.runModal()
    }

    /// A new recording starts clean: what the last one reported is stale.
    func beginShortcutRecording(by recorder: ShortcutRecorder) {
        activeShortcutRecorder = recorder
        shortcutProblem = ""
        for role in Self.sessionShortcutRoles {
            sessionShortcuts[role]?.problem = ""
        }
        bridge.beginShortcutRecording()
    }

    /// The recorder binds before it ends the recording, and the binding
    /// reported itself; only a shortcut that could not be taken back adds a
    /// problem here, on its own row.
    func endShortcutRecording() {
        var failedRole = SpeecherShortcutRole.dictation
        guard let problem = bridge.endShortcutRecording(failedRole: &failedRole) else { return }
        if failedRole == .dictation {
            shortcutProblem = problem
        } else {
            sessionShortcuts[failedRole]?.problem = problem
        }
    }

    /// Ends the recording in progress, if any, and restores the hotkey.
    func stopShortcutRecording() {
        activeShortcutRecorder?.stop()
    }

    func bindShortcut(characters: String, modifierFlags: NSEvent.ModifierFlags) {
        shortcutProblem = bridge.bindShortcut(characters: characters,
                                             modifierFlags: modifierFlags.rawValue) ?? ""
        shortcutWarning = ""
        shortcutNeedsAccessibility = false
        shortcut = bridge.shortcutDisplay
    }

    /// The vocabulary name for a key the recorder caught, or nil for a key
    /// outside it (a media key), which the recorder ignores.
    func keyCodeName(forMacKeyCode keyCode: UInt16) -> String? {
        bridge.keyCodeName(forMacKeyCode: keyCode)
    }

    /// Binds one physical key on its own. A refusal is shown, never saved;
    /// a save can still carry a non-blocking warning that the key keeps its
    /// normal job.
    func bindSingleKey(code: String) {
        shortcutProblem = bridge.bindSingleKey(code: code) ?? ""
        shortcutWarning = shortcutProblem.isEmpty ? bridge.warning(forSingleKeyCode: code) : ""
        shortcutNeedsAccessibility = !shortcutProblem.isEmpty && !bridge.accessibilityEnabled
        shortcut = bridge.shortcutDisplay
    }

    /// The recorder's entry point; says whether the key was one it could take.
    func bindSingleKey(macKeyCode keyCode: UInt16) -> Bool {
        guard let code = keyCodeName(forMacKeyCode: keyCode) else { return false }
        bindSingleKey(code: code)
        return true
    }

    /// The Global Shortcuts that act only during a Dictation Session, in the
    /// order their rows appear.
    static let sessionShortcutRoles: [SpeecherShortcutRole] = [.cancel, .pause]

    func sessionShortcut(_ role: SpeecherShortcutRole) -> SessionShortcut {
        sessionShortcuts[role] ?? SessionShortcut(display: "")
    }

    func bindShortcut(characters: String, modifierFlags: NSEvent.ModifierFlags, role: SpeecherShortcutRole) {
        let problem = bridge.bindShortcut(characters: characters, modifierFlags: modifierFlags.rawValue,
                                          role: role) ?? ""
        noteSessionShortcutChange(role, problem: problem, needsAccessibility: false)
    }

    /// A Cancel or Pause recorder's single-key entry point; says whether the
    /// key was one it could take.
    func bindSingleKey(macKeyCode keyCode: UInt16, role: SpeecherShortcutRole) -> Bool {
        guard let code = keyCodeName(forMacKeyCode: keyCode) else { return false }
        let problem = bridge.bindSingleKey(code: code, role: role) ?? ""
        noteSessionShortcutChange(role, problem: problem,
                                  needsAccessibility: !problem.isEmpty && !bridge.accessibilityEnabled)
        return true
    }

    func clearShortcut(role: SpeecherShortcutRole) {
        noteSessionShortcutChange(role, problem: bridge.clearShortcut(for: role) ?? "", needsAccessibility: false)
    }

    private func noteSessionShortcutChange(_ role: SpeecherShortcutRole, problem: String, needsAccessibility: Bool) {
        sessionShortcuts[role] = SessionShortcut(display: bridge.shortcutDisplay(for: role),
                                                 problem: problem,
                                                 needsAccessibility: needsAccessibility)
    }

    /// The pause button: pauses, or resumes while paused.
    func togglePause() {
        bridge.togglePause()
    }

    /// Goes back to the binder's built-in default.
    func resetShortcut() {
        shortcutProblem = bridge.resetShortcut() ?? ""
        shortcutWarning = ""
        shortcutNeedsAccessibility = false
        shortcut = bridge.shortcutDisplay
    }

    /// Registers the sequence the binder already reports — the stored one, or
    /// its built-in default on a first run — which is what finishing setup
    /// without recording a new shortcut means.
    func bindCurrentShortcut() {
        shortcutProblem = bridge.bindCurrentShortcut() ?? ""
        shortcutWarning = ""
        shortcut = bridge.shortcutDisplay
    }

    func copyTranscript() {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(transcript, forType: .string)
        transcriptCopied = true
        DispatchQueue.main.asyncAfter(deadline: .now() + SpeecherBridge.copiedFeedbackSeconds) { [weak self] in
            self?.transcriptCopied = false
        }
    }

    /// Home's fixed wording, from the core.
    func homeLabel(_ key: String) -> String { bridge.homeLabels[key] ?? "" }

    /// The panes a sidebar search shows, each with the first row on it that
    /// matched, from the core index every front end searches.
    func search(_ query: String) -> [SearchHit] {
        bridge.settingsSchema.searchSettings(query).compactMap { match in
            pane(withId: match.pane).map { SearchHit(pane: $0, row: match.rows.first) }
        }
    }
}

/// A Cancel or Pause Shortcut row: the binding, why the last change was
/// refused until the next one, and whether that refusal was the missing
/// Accessibility grant, which makes the grant call-to-action appear.
struct SessionShortcut {
    var display: String
    var problem = ""
    var needsAccessibility = false
}

/// A pane a search found, and the first row on it that matched; nil when the
/// pane matched by its title.
struct SearchHit: Identifiable {
    let pane: Pane
    let row: String?

    var id: String { pane.id }
}

extension SpeecherErrorAction {
    /// The Privacy & Security panel a permission fix opens; nil for the rest.
    var systemSettingsURL: URL? {
        let panel: String
        switch fix {
        case .microphonePermission: panel = "Privacy_Microphone"
        case .accessibilityPermission: panel = "Privacy_Accessibility"
        default: return nil
        }
        return URL(string: "x-apple.systempreferences:com.apple.preference.security?\(panel)")
    }
}
