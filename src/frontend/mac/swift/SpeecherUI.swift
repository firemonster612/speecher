import AppKit
import SwiftUI

// Speecher's macOS window. Everything it knows about the app arrives through
// SpeecherBridge, which is this target's bridging header; there is no C++ here.

/// The dictation page is not one of the schema's, so it needs an id of its own
/// to sit beside them in the sidebar.
let dictationPageId = "dictation"

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
    // sidebar collapsed, which is not what a settings window is.
    @State private var columns = NavigationSplitViewVisibility.all

    var body: some View {
        NavigationSplitView(columnVisibility: $columns) {
            SidebarList(model: model, query: $query)
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

/// The source list: the dictation page and the schema's, filtered by whatever
/// the search field holds. The schema is the index, so a page answers to its
/// own name and to any section, row or help text it carries.
struct SidebarList: View {
    @ObservedObject var model: SettingsModel
    @Binding var query: String

    var body: some View {
        List(selection: $model.selection) {
            if query.isEmpty {
                Label("Dictation", systemImage: "mic").tag(dictationPageId)
            }
            ForEach(model.pages.filter { model.page($0, matches: query) }, id: \.pageId) { page in
                Label(page.title, systemImage: page.symbolName).tag(page.pageId)
            }
        }
        .listStyle(.sidebar)
        .navigationSplitViewColumnWidth(min: 215, ideal: 230)
        .searchable(text: $query, placement: .sidebar, prompt: "Search")
        .safeAreaInset(edge: .bottom) { stateBadge }
    }

    private var badge: some View {
        Text(model.status.capitalized)
            .font(.caption)
            .padding(.horizontal, 12)
            .padding(.vertical, 7)
    }

    @ViewBuilder private var stateBadge: some View {
        if #available(macOS 26, *) {
            badge.glassEffect()
        } else {
            badge.background(.quaternary, in: .capsule)
        }
    }
}

struct PageForm: View {
    let page: SettingsPageModel
    @ObservedObject var model: SettingsModel

    var body: some View {
        Form {
            // A section title is not unique enough to key on: a page may have a
            // section only for the shape, with no title at all.
            ForEach(Array(page.sections.enumerated()), id: \.offset) { _, section in
                Section {
                    ForEach(section.rows, id: \.rowId) { row in
                        RowView(row: row, model: model)
                    }
                } header: {
                    if !section.title.isEmpty { Text(section.title) }
                } footer: {
                    if !section.help.isEmpty { Text(section.help) }
                }
            }
        }
        .formStyle(.grouped)
    }
}

struct RowView: View {
    let row: SettingsRowModel
    @ObservedObject var model: SettingsModel

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
            LabeledContent { Text(Self.text(row.value)).foregroundStyle(.secondary) } label: { label }
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
            WritingProfileGrid(row: row, model: model)
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

    private var label: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(row.label)
            if !row.help.isEmpty {
                Text(row.help).font(.caption).foregroundStyle(.secondary)
            }
        }
    }

    static func flag(_ value: Any?) -> Bool { (value as? NSNumber)?.boolValue ?? false }
    static func number(_ value: Any?) -> Int { (value as? NSNumber)?.intValue ?? 0 }
    static func text(_ value: Any?) -> String { value as? String ?? "" }
}

/// A number with its range and its unit. Typing into the field is the way to
/// cross a wide range that a stepper alone would take all day to walk.
struct NumberField: View {
    let row: SettingsRowModel
    @ObservedObject var model: SettingsModel
    @State private var value = 0
    @FocusState private var editing: Bool

    var body: some View {
        HStack(spacing: 6) {
            TextField("", value: $value, format: .number)
                .labelsHidden()
                .multilineTextAlignment(.trailing)
                .monospacedDigit()
                .frame(width: 62)
                .focused($editing)
                .onSubmit { commit() }
            if !row.suffix.isEmpty {
                Text(row.suffix.trimmingCharacters(in: .whitespaces)).foregroundStyle(.secondary)
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

/// Free text, saved when the field is done rather than on every keystroke, and
/// with a menu of the values worth offering when the row names any.
struct TextRowField: View {
    let row: SettingsRowModel
    @ObservedObject var model: SettingsModel
    @State private var text = ""
    @FocusState private var editing: Bool

    var body: some View {
        HStack(spacing: 4) {
            TextField("", text: $text)
                .labelsHidden()
                .frame(minWidth: 160)
                .focused($editing)
                .onSubmit { commit() }
            if !row.suggestions.isEmpty {
                Menu {
                    ForEach(row.suggestions, id: \.rowOptionId) { suggestion in
                        Button(suggestion.label) {
                            text = suggestion.rowOptionId
                            commit()
                        }
                    }
                } label: {
                    EmptyView()
                }
                .menuStyle(.borderlessButton)
                .frame(width: 16)
            }
        }
        .onAppear { text = RowView.text(row.value) }
        .onChange(of: editing) { if !editing { commit() } }
        .onChange(of: RowView.text(row.value)) { _, stored in
            if !editing { text = stored }
        }
    }

    private func commit() {
        if text != RowView.text(row.value) {
            model.setValue(text, for: row.rowId)
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
            VStack(alignment: .trailing, spacing: 4) {
                SecureField("Enter OpenAI API key", text: $model.apiKey)
                    .labelsHidden()
                    .frame(minWidth: 220)
                    .focused($editing)
                    .onSubmit { model.saveApiKey() }
                    .onChange(of: model.apiKey) { model.noteApiKeyEdited() }
                    .onChange(of: editing) { if !editing { model.saveApiKey() } }
                if !model.credentialProblem.isEmpty {
                    Text(model.credentialProblem).font(.caption).foregroundStyle(.red)
                }
            }
        } else {
            Text(model.bridge.credentialStatus).foregroundStyle(.secondary)
        }
    }
}

/// The cleanup strength and optional tone of each writing profile. The profiles
/// are fixed, so this is a short run of rows rather than an editable table.
struct WritingProfileGrid: View {
    let row: SettingsRowModel
    @ObservedObject var model: SettingsModel

    private var records: [[String: Any]] { row.value as? [[String: Any]] ?? [] }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(row.label)
            Text(row.help).font(.caption).foregroundStyle(.secondary)
            ForEach(Array(records.enumerated()), id: \.offset) { index, record in
                LabeledContent(record["profile"] as? String ?? "") {
                    HStack(spacing: 8) {
                        ForEach(row.collection?.columns.filter { $0.kind == .choice } ?? [],
                                id: \.columnId) { column in
                            Picker("", selection: choice(index, column.columnId)) {
                                ForEach(column.options, id: \.rowOptionId) { option in
                                    Text(option.label).tag(option.rowOptionId)
                                }
                            }
                            .labelsHidden()
                            .frame(width: 150)
                        }
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
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 940, height: 660),
                          styleMask: [.titled, .closable, .miniaturizable, .resizable,
                                      .fullSizeContentView],
                          backing: .buffered,
                          defer: false)
        super.init()
        window.title = "Speecher"
        // The page's own title heads the detail column, so repeating it in the
        // titlebar is the one thing System Settings does not do.
        window.titleVisibility = .hidden
        window.titlebarAppearsTransparent = true
        window.minSize = NSSize(width: 820, height: 560)
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
    // detail column is real. SwiftUI's ImageRenderer is not an alternative: it
    // refuses NavigationSplitView outright. For a composited shot, screencapture
    // with Screen Recording granted is the way.
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
