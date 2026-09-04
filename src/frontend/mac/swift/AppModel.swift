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
    /// The dictation state's name, as the controller reports it.
    @Published private(set) var status: String
    @Published private(set) var level: Float = 0
    /// The last thing Speecher heard, which the menu bar panel offers to copy.
    @Published private(set) var transcript: String
    @Published private(set) var accessibilityEnabled: Bool
    @Published private(set) var whatsNewPending: Bool
    /// Why the accessibility grant could not be asked for, when it could not.
    @Published var accessibilityProblem = ""
    /// The app settings key, which lives in the keyring rather than in the
    /// settings, so the schema knows nothing about it.
    @Published var apiKey = ""
    @Published var credentialProblem = ""
    @Published private(set) var anthropicCredentialStatus: String
    @Published private(set) var shortcut: String
    @Published private(set) var shortcutProblem = ""
    /// The pane the sidebar is on, remembered between openings because people
    /// adjust related settings more than once.
    @Published var pane: String {
        didSet { UserDefaults.standard.set(pane, forKey: Self.paneKey) }
    }

    let bridge: SpeecherBridge
    private static let paneKey = "settingsPane"
    /// A keyring read that lands after typing started must not overwrite it.
    private var apiKeyEdits = 0
    private var apiKeyLoaded = false
    /// Whether the slow rows have been asked for once, after which asking again
    /// costs nothing new.
    private var deferredLoaded = false

    var accessibilitySupported: Bool { bridge.accessibilitySupported }
    var shortcutSupported: Bool { bridge.shortcutSupported }
    var listening: Bool { Self.listening(status) }

    /// Whether a state name is one where the microphone is open.
    static func listening(_ status: String) -> Bool {
        ["starting", "listening"].contains(status.lowercased())
    }

    init(bridge: SpeecherBridge) {
        self.bridge = bridge
        pages = bridge.settingsSchema.pages
        status = bridge.stateName
        transcript = bridge.lastTranscript
        shortcut = bridge.shortcutDisplay
        accessibilityEnabled = bridge.accessibilityEnabled
        whatsNewPending = bridge.whatsNewPending
        anthropicCredentialStatus = bridge.anthropicCredentialStatus
        // A pane remembered from before the pane list changed names no longer
        // exists; start from the first pane rather than an empty window.
        let remembered = UserDefaults.standard.string(forKey: Self.paneKey) ?? ""
        pane = Pane.with(id: remembered) != nil ? remembered : Pane.all[0].id
        bridge.statusChanged = { [weak self] status in
            self?.status = status
        }
        bridge.audioLevelChanged = { [weak self] level in
            self?.level = level
        }
        bridge.transcriptChanged = { [weak self] transcript in
            self?.transcript = transcript
        }
        bridge.accessibilityChanged = { [weak self] in
            guard let self else { return }
            accessibilityEnabled = bridge.accessibilityEnabled
            pages = bridge.settingsSchema.pages
        }
        bridge.anthropicCredentialsChanged = { [weak self] in
            self?.anthropicCredentialStatus = bridge.anthropicCredentialStatus
        }
        bridge.whatsNewChanged = { [weak self] in
            self?.whatsNewPending = bridge.whatsNewPending
        }
    }

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

    func reloadSettingsDraft() {
        bridge.settingsSchema.reloadDraft()
        pages = bridge.settingsSchema.pages
    }

    private func loadApiKey() {
        let edits = apiKeyEdits
        let key = bridge.readApiKey()
        apiKeyLoaded = true
        if edits == apiKeyEdits {
            apiKey = key
        }
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

    func title(for pane: Pane) -> String {
        if pane.id == "dictation" { return "Dictation" }
        return settingsPage(for: pane)?.title ?? "Settings"
    }

    func symbol(for pane: Pane) -> String {
        if pane.id == "dictation" { return "mic" }
        return settingsPage(for: pane)?.symbolName ?? "gearshape"
    }

    /// A pane's cards: the sections of the schema page it shows, with the rows
    /// the schema currently offers. A schema page no pane names falls back to
    /// General, so a newly added page is never invisible.
    func cards(for pane: Pane) -> [PaneCard] {
        let ownedPages = Set(Pane.all.flatMap(\.schemaPages))
        return pages.flatMap { page -> [PaneCard] in
            let belongsHere = pane.schemaPages.contains(page.pageId)
                || (pane.id == "general" && !ownedPages.contains(page.pageId))
            guard belongsHere else { return [] }
            return page.sections.compactMap { section -> PaneCard? in
                guard !section.rows.isEmpty else { return nil }
                return PaneCard(title: section.title.isEmpty ? page.title : section.title,
                                help: section.help,
                                rows: section.rows)
            }
        }
    }

    func trigger(_ rowId: String) {
        if rowId == "whatsNew" { showWhatsNew() }
        bridge.settingsSchema.actionTriggered?(rowId)
    }

    func showWhatsNew() {
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
    }

    func binding<Value>(_ row: SettingsRowModel,
                        read: @escaping (Any?) -> Value,
                        write: @escaping (Value) -> Any) -> Binding<Value> {
        Binding(get: { read(row.value) },
                set: { [weak self] newValue in
                    self?.setValue(write(newValue), for: row.rowId)
                })
    }

    /// Empty when the records are consistent, in which case they are also saved.
    func save(records: [[String: Any]], for rowId: String) -> [String] {
        let problems = bridge.settingsSchema.problems(with: records, forRowId: rowId)
        if problems.isEmpty {
            setValue(records, for: rowId)
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

    func bindShortcut(characters: String, modifierFlags: NSEvent.ModifierFlags) {
        shortcutProblem = bridge.bindShortcut(characters: characters,
                                             modifierFlags: modifierFlags.rawValue) ?? ""
        shortcut = bridge.shortcutDisplay
    }

    func copyTranscript() {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(transcript, forType: .string)
    }

    /// Whether anything on a pane — its name, a card heading, a row, or the
    /// help under one — answers to what was typed in the search field. The cards
    /// are the ones the pane draws, so nothing visible is unsearchable.
    func pane(_ pane: Pane, matches query: String) -> Bool {
        if query.isEmpty { return true }
        let needle = query.lowercased()
        let hit = { (text: String) in text.lowercased().contains(needle) }
        if hit(title(for: pane)) { return true }
        return cards(for: pane).contains { card in
            hit(card.title) || hit(card.help)
                || card.rows.contains { hit($0.label) || hit($0.help) }
        }
    }

    private func settingsPage(for pane: Pane) -> SettingsPageModel? {
        pages.first { pane.schemaPages.contains($0.pageId) }
    }
}
