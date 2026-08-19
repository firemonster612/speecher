import AppKit
import SwiftUI

// Speecher's macOS window. Everything it knows about the app arrives through
// SpeecherBridge, which is this target's bridging header; there is no C++ here.
//
// The rule this file follows: if a stock container draws it, let it. There is no
// padding, corner radius, colour, background or fixed width set anywhere below,
// because System Settings is what SwiftUI's own sidebar list and grouped form
// look like when nobody dresses them up.

/// The dictation page is not one of the schema's, so it needs an id of its own
/// to sit beside them in the sidebar.
let dictationPageId = "dictation"

/// The sidebar's groups, in order, the way System Settings groups its own: the
/// page you land on, then the dictation pipeline, then what Speecher has
/// learned, then the app itself. A page id no group names joins the last group,
/// so a page added to the schema shows up rather than disappearing.
private let sidebarGroups = [
    [dictationPageId],
    ["audio", "refinement", "output", "applications"],
    ["vocabulary", "corrections", "bindings"],
    ["general", "providers"],
]

/// The settings surface as SwiftUI reads it. The bridge hands over immutable
/// snapshots, so a write goes back through the bridge and the snapshot is taken
/// again — which is also what re-derives the rows a changed value gates.
@MainActor
final class SettingsModel: ObservableObject {
    @Published private(set) var pages: [SettingsPageModel]
    @Published private(set) var status: String
    @Published private(set) var level: Float = 0
    @Published private(set) var summary: SpeecherDictationSummary
    @Published private(set) var accessibilityEnabled: Bool
    /// Why the accessibility grant could not be asked for, when it could not.
    @Published var accessibilityProblem = ""
    /// The app settings key, which lives in the keyring rather than in the
    /// settings, so the schema knows nothing about it.
    @Published var apiKey = ""
    @Published var credentialProblem = ""
    /// The page the sidebar is on. It lives here rather than in the view so the
    /// front end can open the window on a page of its choosing.
    @Published var selection = dictationPageId

    let bridge: SpeecherBridge
    /// A keyring read that lands after typing started must not overwrite it.
    private var apiKeyEdits = 0
    private var apiKeyLoaded = false
    /// Whether the slow rows have been asked for once, after which asking again
    /// costs nothing new.
    private var deferredLoaded = false

    var accessibilitySupported: Bool { bridge.accessibilitySupported }

    init(bridge: SpeecherBridge) {
        self.bridge = bridge
        pages = bridge.settingsSchema.pages
        status = bridge.stateName
        summary = bridge.dictationSummary(resolvingMicrophone: false)
        accessibilityEnabled = bridge.accessibilityEnabled
        bridge.statusChanged = { [weak self] status in
            self?.status = status
        }
        bridge.audioLevelChanged = { [weak self] level in
            self?.level = level
        }
        bridge.accessibilityChanged = { [weak self] in
            guard let self else { return }
            accessibilityEnabled = bridge.accessibilityEnabled
        }
    }

    /// The work the first frame must not wait for: enumerating audio devices,
    /// listing CLI Proxy API accounts, and reading the keyring.
    func loadDeferredRows() {
        deferredLoaded = true
        bridge.settingsSchema.loadExpensiveRows()
        summary = bridge.dictationSummary(resolvingMicrophone: true)
        pages = bridge.settingsSchema.pages
        // Only the keyring can stop to ask for an unlock, so it waits another
        // turn rather than holding up the other two.
        DispatchQueue.main.async { [weak self] in self?.loadApiKey() }
    }

    private func loadApiKey() {
        let edits = apiKeyEdits
        let key = bridge.readApiKey()
        apiKeyLoaded = true
        if edits == apiKeyEdits {
            apiKey = key
        }
    }

    func page(_ pageId: String) -> SettingsPageModel? {
        pages.first { $0.pageId == pageId }
    }

    func trigger(_ rowId: String) {
        bridge.settingsSchema.actionTriggered?(rowId)
    }

    func setValue(_ value: Any?, for rowId: String) {
        bridge.settingsSchema.setValue(value, forRowId: rowId)
        bridge.settingsSchema.commit()
        pages = bridge.settingsSchema.pages
        summary = bridge.dictationSummary(resolvingMicrophone: deferredLoaded)
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

    /// Whether anything on the page — its name, a section, a row, or the help
    /// under one — answers to what was typed in the sidebar's search field.
    func page(_ page: SettingsPageModel, matches query: String) -> Bool {
        if query.isEmpty { return true }
        let needle = query.lowercased()
        let hit = { (text: String) in text.lowercased().contains(needle) }
        if hit(page.title) { return true }
        return page.sections.contains { section in
            hit(section.title) || hit(section.help)
                || section.rows.contains { hit($0.label) || hit($0.help) }
        }
    }
}

struct RootView: View {
    @ObservedObject var model: SettingsModel
    @State private var query = ""
    // Pinned open: left to decide for itself the split view starts with the
    // sidebar collapsed, and a settings window has no way to bring it back —
    // System Settings' sidebar cannot be closed either.
    @State private var columns = NavigationSplitViewVisibility.all

    var body: some View {
        NavigationSplitView(columnVisibility: $columns) {
            // Both of these have to sit out here on the column, and in this
            // order: the split view reads them off the outermost view of the
            // column, so a modifier applied inside SidebarList's own body — or
            // wrapped by another one out here — is a modifier it never sees.
            SidebarList(model: model, query: $query)
                // System Settings' sidebar does not collapse, and neither does
                // this one: the toggle would hide a column nothing here brings
                // back, and System Settings has no such button in its toolbar.
                .toolbar(removing: .sidebarToggle)
                // The width a settings sidebar is on macOS. Left to itself the
                // split view picks one narrow enough to clip a page name, which
                // is the one measurement in this file that has to be stated.
                .navigationSplitViewColumnWidth(min: 200, ideal: 215)
        } detail: {
            detail
        }
    }

    @ViewBuilder private var detail: some View {
        if model.selection == dictationPageId {
            DictationView(model: model, select: { model.selection = $0 })
                .navigationTitle("Dictation")
        } else if let page = model.page(model.selection) {
            PageForm(page: page, model: model).navigationTitle(page.title)
        } else {
            ContentUnavailableView("No page selected",
                                   systemImage: "sidebar.left",
                                   description: Text("Pick a page in the sidebar."))
        }
    }
}

/// One sidebar row, whichever kind of page it stands for.
private struct SidebarItem: Identifiable {
    let id: String
    let title: String
    let symbol: String
}

/// The source list: the dictation page and the schema's, in groups, filtered by
/// whatever the search field holds. The schema is the index, so a page answers
/// to its own name and to any section, row or help text it carries.
struct SidebarList: View {
    @ObservedObject var model: SettingsModel
    @Binding var query: String

    var body: some View {
        List(selection: $model.selection) {
            if query.isEmpty {
                // Groups are a run of rows with a gap above, which is all
                // System Settings' sidebar sections are.
                ForEach(Array(groups.enumerated()), id: \.offset) { _, group in
                    Section {
                        ForEach(group) { row($0) }
                    }
                }
            } else {
                // A search shows its hits as one flat list, not as the groups
                // they came from.
                ForEach(items.filter(matches)) { row($0) }
            }
        }
        .listStyle(.sidebar)
        .searchable(text: $query, placement: .sidebar, prompt: "Search")
    }

    private func row(_ item: SidebarItem) -> some View {
        Label(item.title, systemImage: item.symbol).tag(item.id)
    }

    private var items: [SidebarItem] {
        [SidebarItem(id: dictationPageId, title: "Dictation", symbol: "mic")]
            + model.pages.map {
                SidebarItem(id: $0.pageId, title: $0.title, symbol: $0.symbolName)
            }
    }

    private var groups: [[SidebarItem]] {
        var remaining = items
        var built: [[SidebarItem]] = []
        for group in sidebarGroups {
            let picked = group.compactMap { id -> SidebarItem? in
                guard let index = remaining.firstIndex(where: { $0.id == id }) else { return nil }
                return remaining.remove(at: index)
            }
            if !picked.isEmpty { built.append(picked) }
        }
        // A page the groups above do not name still has to appear somewhere.
        if !remaining.isEmpty {
            if built.isEmpty {
                built.append(remaining)
            } else {
                built[built.count - 1] += remaining
            }
        }
        return built
    }

    private func matches(_ item: SidebarItem) -> Bool {
        guard let page = model.page(item.id) else {
            return item.title.lowercased().contains(query.lowercased())
        }
        return model.page(page, matches: query)
    }
}

/// One card of a page's form. A schema section becomes one of these, except that
/// a row which fills a card of its own — a collection's table, the writing
/// profiles — takes its own, with its label and help as that card's header and
/// footnote instead of as text drawn inside it.
private struct FormCard: Identifiable {
    let id: Int
    let header: String
    let footer: String
    let rows: [SettingsRowModel]
}

struct PageForm: View {
    let page: SettingsPageModel
    @ObservedObject var model: SettingsModel

    var body: some View {
        Form {
            ForEach(cards) { card in
                Section {
                    ForEach(card.rows, id: \.rowId) { row in
                        RowView(row: row, model: model)
                    }
                } header: {
                    if !card.header.isEmpty { Text(card.header) }
                } footer: {
                    if !card.footer.isEmpty { Text(card.footer) }
                }
            }
        }
        .formStyle(.grouped)
    }

    /// A table needs the whole width of a card, so a section holding one splits
    /// into the rows before it, the table's own card, and the rows after. The
    /// section's title heads the first card it produces and its footnote sits
    /// under the last, whichever of them that turns out to be.
    private var cards: [FormCard] {
        var built: [FormCard] = []
        for section in page.sections {
            var made: [FormCard] = []
            var pending: [SettingsRowModel] = []
            var titleTaken = section.title.isEmpty
            // The section's title, once, and the row's own label after that.
            func header(orElse fallback: String) -> String {
                guard !titleTaken else { return fallback }
                titleTaken = true
                return section.title
            }
            func add(_ rows: [SettingsRowModel], _ head: String, _ foot: String) {
                made.append(FormCard(id: built.count + made.count,
                                     header: head,
                                     footer: foot,
                                     rows: rows))
            }
            for row in section.rows {
                if RowView.fillsCard(row) {
                    if !pending.isEmpty {
                        add(pending, header(orElse: ""), "")
                        pending = []
                    }
                    add([row], header(orElse: row.label), row.help)
                } else {
                    pending.append(row)
                }
            }
            if !pending.isEmpty {
                add(pending, header(orElse: ""), "")
            }
            if !section.help.isEmpty, let last = made.popLast() {
                let footer = last.footer.isEmpty ? section.help
                                                 : last.footer + "\n\n" + section.help
                made.append(FormCard(id: last.id,
                                     header: last.header,
                                     footer: footer,
                                     rows: last.rows))
            }
            built += made
        }
        return built
    }
}

struct RowView: View {
    let row: SettingsRowModel
    @ObservedObject var model: SettingsModel

    /// A row whose control is as wide as the card, so its label and help belong
    /// above the card rather than inside it.
    static func fillsCard(_ row: SettingsRowModel) -> Bool {
        row.collection != nil
    }

    var body: some View {
        control
            .disabled(!row.enabled)
            .help(row.enabled ? row.tooltip : row.disabledHelp)
    }

    @ViewBuilder private var control: some View {
        switch row.kind {
        case .toggle:
            Toggle(isOn: model.binding(row, read: Self.flag, write: { $0 as NSNumber })) { label }
        case .choice:
            picker
        case .number:
            LabeledContent {
                NumberField(row: row, model: model)
            } label: {
                label
            }
        case .text:
            LabeledContent {
                TextRowField(row: row, model: model)
            } label: {
                label
            }
        case .info:
            LabeledContent { Text(Self.text(row.value)) } label: { label }
        case .action:
            LabeledContent { Button(row.actionLabel) { model.trigger(row.rowId) } } label: { label }
        case .collection:
            CollectionRow(row: row, model: model)
        case .custom:
            custom
        @unknown default:
            EmptyView()
        }
    }

    // Custom rows are the ones the schema leaves to the front end. Two are
    // shapes of their own; the rest are pickers whose choices this front end
    // supplies, the way OutputCustomRows and ProviderCustomRows do on Qt.
    @ViewBuilder private var custom: some View {
        if row.collection != nil {
            WritingProfileRows(row: row, model: model)
        } else if row.rowId == "openAiAuth" {
            LabeledContent { CredentialField(model: model) } label: { label }
        } else {
            picker
        }
    }

    private var picker: some View {
        Picker(selection: model.binding(row, read: Self.text, write: { $0 })) {
            ForEach(row.options, id: \.rowOptionId) { option in
                Text(option.label).tag(option.rowOptionId).help(option.help)
            }
        } label: {
            label
        }
        .disabled(row.options.allSatisfy { !$0.enabled })
    }

    /// The name of the setting and, under it, what it does. Two Texts in a
    /// stock label is how a settings row says that; SwiftUI sizes and colours
    /// the second one, which is why there is no font or colour here.
    @ViewBuilder private var label: some View {
        Text(row.label)
        if !row.help.isEmpty {
            Text(row.help)
        }
    }

    static func flag(_ value: Any?) -> Bool { (value as? NSNumber)?.boolValue ?? false }
    static func number(_ value: Any?) -> Int { (value as? NSNumber)?.intValue ?? 0 }
    static func text(_ value: Any?) -> String { value as? String ?? "" }
}

/// A number with its range and its unit: the system's numeric field and stepper,
/// at the size they come out at. Typing into the field is the way to cross a
/// wide range that a stepper alone would take all day to walk.
struct NumberField: View {
    let row: SettingsRowModel
    @ObservedObject var model: SettingsModel
    @State private var value = 0
    @FocusState private var editing: Bool

    var body: some View {
        HStack {
            TextField("", value: $value, format: .number)
                .labelsHidden()
                .multilineTextAlignment(.trailing)
                .fixedSize()
                .focused($editing)
                .onSubmit { commit() }
            if !row.suffix.isEmpty {
                Text(row.suffix.trimmingCharacters(in: .whitespaces))
            }
            Stepper("", value: $value, in: row.minimum...row.maximum, step: row.step)
                .labelsHidden()
        }
        .onAppear { value = RowView.number(row.value) }
        .onChange(of: value) { commit() }
        .onChange(of: RowView.number(row.value)) { _, stored in
            if !editing { value = stored }
        }
    }

    private func commit() {
        let clamped = min(max(value, row.minimum), row.maximum)
        if clamped != value { value = clamped }
        if clamped != RowView.number(row.value) {
            model.setValue(clamped as NSNumber, for: row.rowId)
        }
    }
}

/// Free text, saved when the field is done rather than on every keystroke. A row
/// that names values worth offering gets the system's editable combo box, which
/// is the control for that, rather than a field with a menu bolted beside it.
struct TextRowField: View {
    let row: SettingsRowModel
    @ObservedObject var model: SettingsModel
    @State private var text = ""
    @FocusState private var editing: Bool

    var body: some View {
        if row.suggestions.isEmpty {
            TextField("", text: $text)
                .labelsHidden()
                .focused($editing)
                .onSubmit { commit() }
                .onAppear { text = RowView.text(row.value) }
                .onChange(of: editing) { if !editing { commit() } }
                .onChange(of: RowView.text(row.value)) { _, stored in
                    if !editing { text = stored }
                }
        } else {
            SuggestingField(text: RowView.text(row.value),
                            suggestions: row.suggestions.map(\.rowOptionId)) { edited in
                model.setValue(edited, for: row.rowId)
            }
        }
    }

    private func commit() {
        if text != RowView.text(row.value) {
            model.setValue(text, for: row.rowId)
        }
    }
}

/// An NSComboBox: text a person can type, with the values worth offering behind
/// the same control, which SwiftUI has no equivalent of.
struct SuggestingField: NSViewRepresentable {
    let text: String
    let suggestions: [String]
    let commit: (String) -> Void

    func makeNSView(context: Context) -> NSComboBox {
        let box = NSComboBox()
        box.isEditable = true
        box.completes = true
        box.addItems(withObjectValues: suggestions)
        box.delegate = context.coordinator
        return box
    }

    func updateNSView(_ box: NSComboBox, context: Context) {
        context.coordinator.commit = commit
        // Replacing the text under someone who is typing in it is the one thing
        // a redraw must not do.
        if !context.coordinator.editing, box.stringValue != text {
            box.stringValue = text
        }
    }

    func makeCoordinator() -> Coordinator { Coordinator(commit: commit) }

    @MainActor
    final class Coordinator: NSObject, NSComboBoxDelegate {
        var commit: (String) -> Void
        var editing = false

        init(commit: @escaping (String) -> Void) {
            self.commit = commit
        }

        func controlTextDidBeginEditing(_ notification: Notification) {
            editing = true
        }

        func controlTextDidEndEditing(_ notification: Notification) {
            editing = false
            guard let box = notification.object as? NSComboBox else { return }
            commit(box.stringValue)
        }

        func comboBoxSelectionDidChange(_ notification: Notification) {
            guard let box = notification.object as? NSComboBox else { return }
            // The selected item becomes the field's text after this call, so
            // the value to save is the item, not what the field still holds.
            let picked = box.objectValueOfSelectedItem as? String
            commit(picked ?? box.stringValue)
        }
    }
}

/// The OpenAI credential: a secret to type while the app settings key is the
/// chosen source, and the resolved status of whichever source it is otherwise.
struct CredentialField: View {
    @ObservedObject var model: SettingsModel
    @FocusState private var editing: Bool

    var body: some View {
        if model.bridge.credentialIsEditable {
            VStack(alignment: .trailing) {
                SecureField("Enter OpenAI API key", text: $model.apiKey)
                    .labelsHidden()
                    .focused($editing)
                    .onSubmit { model.saveApiKey() }
                    .onChange(of: model.apiKey) { model.noteApiKeyEdited() }
                    .onChange(of: editing) { if !editing { model.saveApiKey() } }
                if !model.credentialProblem.isEmpty {
                    Text(model.credentialProblem).foregroundStyle(.red)
                }
            }
        } else {
            Text(model.bridge.credentialStatus)
        }
    }
}

/// The cleanup strength and optional tone of each writing profile. The profiles
/// are fixed, so this is a run of ordinary settings rows rather than an editable
/// table — one row per profile, each with its pickers.
struct WritingProfileRows: View {
    let row: SettingsRowModel
    @ObservedObject var model: SettingsModel

    private var records: [[String: Any]] { row.value as? [[String: Any]] ?? [] }

    private var choices: [CollectionColumnModel] {
        row.collection?.columns.filter { $0.kind == .choice } ?? []
    }

    var body: some View {
        ForEach(Array(records.enumerated()), id: \.offset) { index, record in
            LabeledContent(record["profile"] as? String ?? "") {
                HStack {
                    ForEach(choices, id: \.columnId) { column in
                        Picker("", selection: choice(index, column.columnId)) {
                            ForEach(column.options, id: \.rowOptionId) { option in
                                Text(option.label).tag(option.rowOptionId)
                            }
                        }
                        .labelsHidden()
                    }
                }
            }
        }
    }

    private func choice(_ index: Int, _ columnId: String) -> Binding<String> {
        Binding(get: { records[index][columnId] as? String ?? "" },
                set: { newValue in
                    var edited = records
                    edited[index][columnId] = newValue
                    _ = model.save(records: edited, for: row.rowId)
                })
    }
}

/// The window itself, so the Objective-C++ front end never has to know what
/// SwiftUI view is inside it.
@objc public final class SpeecherMainWindow: NSObject {
    private let model: SettingsModel
    private let window: NSWindow

    @MainActor
    @objc public init(bridge: SpeecherBridge) {
        model = SettingsModel(bridge: bridge)
        // An ordinary titled window with a unified toolbar, which is what a
        // settings window is. Nothing full-size-content: the traffic lights
        // belong in a titlebar, not floating over the first row of a form.
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 940, height: 660),
                          styleMask: [.titled, .closable, .miniaturizable, .resizable],
                          backing: .buffered,
                          defer: false)
        super.init()
        window.title = "Speecher"
        window.minSize = NSSize(width: 820, height: 560)
        // Empty on purpose: the toolbar is what gives the titlebar its settings
        // height and puts the title and the sidebar's search where macOS puts
        // them. System Settings has no toolbar buttons of its own either, and an
        // interactive view in this strip would have its clicks eaten by the
        // glass container macOS 26 puts there.
        window.toolbar = NSToolbar()
        window.toolbarStyle = .unified
        // A controller rather than a bare hosting view: NavigationSplitView
        // becomes an NSSplitViewController, which needs a parent view
        // controller to install its sidebar item into.
        window.contentViewController = NSHostingController(rootView: RootView(model: model))
        // The controller brings its own idea of how big it wants to be, which
        // is the minimum rather than the size this window was made at.
        window.setContentSize(NSSize(width: 940, height: 660))
        window.center()
    }

    /// An empty page id leaves the window on whichever page it was showing.
    @MainActor
    @objc public func show(page pageId: String) {
        if !pageId.isEmpty {
            model.selection = pageId
        }
        window.makeKeyAndOrderFront(nil)
        // The device enumeration and the keyring read would both delay the first
        // frame, so they wait a turn of the run loop for it.
        DispatchQueue.main.async { [model] in
            model.loadDeferredRows()
        }
    }

    // Called from the front end on the main thread, which is where the window
    // has to be touched.
    //
    // This is the window's backing store, so nothing the compositor draws for
    // the window comes out: vibrancy materials are blank, and the whole sidebar
    // column, which SwiftUI puts inside a glass container, is missing. The
    // detail column and the titlebar are real. SwiftUI's ImageRenderer is not an
    // alternative: it refuses NavigationSplitView outright. For a composited
    // shot, screencapture with Screen Recording granted is the way.
    @MainActor
    @objc public func capture(toPath path: String) -> Bool {
        guard let content = window.contentView,
              let view = content.superview ?? window.contentView,
              let bitmap = view.bitmapImageRepForCachingDisplay(in: view.bounds) else {
            return false
        }
        view.cacheDisplay(in: view.bounds, to: bitmap)
        guard let png = bitmap.representation(using: .png, properties: [:]) else {
            return false
        }
        return (try? png.write(to: URL(fileURLWithPath: path))) != nil
    }
}
