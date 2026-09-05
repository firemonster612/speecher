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
    /// The sidebar's panes and their runs, as the schema arranges them. Fixed
    /// for the life of the app, so a plain let.
    let panes: [Pane]
    let sidebarRuns: [[String]]
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
        let panes = bridge.settingsSchema.panes.map(Pane.init)
        self.panes = panes
        sidebarRuns = bridge.settingsSchema.sidebarRuns
        status = bridge.stateName
        transcript = bridge.lastTranscript
        shortcut = bridge.shortcutDisplay
        accessibilityEnabled = bridge.accessibilityEnabled
        whatsNewPending = bridge.whatsNewPending
        anthropicCredentialStatus = bridge.anthropicCredentialStatus
        pane = UserDefaults.standard.string(forKey: Self.paneKey) ?? panes[0].id
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

    func pane(withId id: String) -> Pane? {
        panes.first { $0.id == id }
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

    /// The rows a group asks for that the schema currently offers, in the order
    /// the group named them. A pattern ending in `*` takes every row whose id
    /// starts with it, which is how the per-category paste rules arrive.
    func rows(matching patterns: [String]) -> [SettingsRowModel] {
        var found: [SettingsRowModel] = []
        for pattern in patterns {
            guard pattern.hasSuffix("*") else {
                if let row = row(pattern) { found.append(row) }
                continue
            }
            let prefix = String(pattern.dropLast())
            for page in pages {
                for section in page.sections {
                    found += section.rows.filter { $0.rowId.hasPrefix(prefix) }
                }
            }
        }
        return found
    }

    /// A pane's groups, in order, each with the rows the schema currently offers
    /// it. A group whose rows are all absent stays in the list and draws
    /// nothing, so the index a segmented picker holds keeps meaning what it did.
    func groupCards(for pane: Pane) -> [PaneCard] {
        pane.groups.map { group in
            let placed = rows(matching: group.rows)
            return PaneCard(title: group.title, help: footnote(group, placed), rows: placed)
        }
    }

    /// Schema rows no pane placed explicitly, as the cards the schema itself
    /// describes: they appear on the pane that owns their schema page, keeping
    /// their section's title and help. A newly added schema page falls back to
    /// General.
    func unclaimedCards(for pane: Pane) -> [PaneCard] {
        let claimed = Set(panes.flatMap { candidate in
            candidate.groups.flatMap { rows(matching: $0.rows).map(\.rowId) }
        })
        let ownedPages = Set(panes.flatMap(\.schemaPages))
        return pages.flatMap { page -> [PaneCard] in
            let belongsHere = pane.schemaPages.contains(page.pageId)
                || (pane.id == "general" && !ownedPages.contains(page.pageId))
            guard belongsHere else { return [] }
            return page.sections.compactMap { section -> PaneCard? in
                let unplaced = page.pageId == "whatsNew"
                    ? section.rows
                    : section.rows.filter { !claimed.contains($0.rowId) }
                guard !unplaced.isEmpty else { return nil }
                return PaneCard(title: section.title.isEmpty ? page.title : section.title,
                                help: section.help,
                                rows: unplaced)
            }
        }
    }

    /// What a group says about itself, or failing that what the schema says
    /// under the section these rows came from, or the help of a row that fills
    /// the whole card and so has nowhere else to put it.
    private func footnote(_ group: PaneGroup, _ rows: [SettingsRowModel]) -> String {
        if !group.help.isEmpty { return group.help }
        let ids = Set(rows.map(\.rowId))
        for page in pages {
            for section in page.sections where !section.help.isEmpty {
                if section.rows.contains(where: { ids.contains($0.rowId) }) {
                    return section.help
                }
            }
        }
        return rows.first { $0.collection != nil }?.help ?? ""
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
        if hit(pane.title) { return true }
        return (groupCards(for: pane) + unclaimedCards(for: pane)).contains { card in
            hit(card.title) || hit(card.help)
                || card.rows.contains { hit($0.label) || hit($0.help) }
        }
    }
}
