import AppKit
import SwiftUI
import UniformTypeIdentifiers

// The five collection editors — application rules, paste rules, vocabulary,
// learned corrections, replacements — are one table driven by the descriptor
// behind whichever row asked for it.
//
// The shape is System Settings': a table, a +/− accessory bar under it, adding
// through a sheet so the record can be checked before it exists, and removing
// through a confirmation. A grouped form caps its content width, which is why
// the table is as wide as the card and not as wide as the window; that is the
// same width System Settings gives its own lists.

/// One record, with an identity of its own: records carry no unique key and a
/// table needs one.
struct CollectionRecord: Identifiable {
    let id = UUID()
    var values: [String: Any]
    /// Built-in records: readable, and neither editable nor deletable.
    let locked: Bool
}

@MainActor
final class CollectionEditor: ObservableObject {
    @Published private(set) var records: [CollectionRecord] = []
    @Published var selection = Set<UUID>()
    /// Why the records were refused, which is also why they were not saved.
    @Published private(set) var problems: [String] = []
    @Published var adding = false
    @Published var confirmingRemoval = false
    @Published var importing = false
    /// The record the add sheet is filling in.
    @Published var draft: [String: Any] = [:]

    /// What Delete took, newest last, so undo can put it back.
    private var deleted: [CollectionRecord] = []
    private var seeded = false

    let row: SettingsRowModel
    let model: AppModel

    var collection: CollectionModel { row.collection! }
    var canAdd: Bool { !collection.addLabel.isEmpty }
    var canRemove: Bool { records.contains { selection.contains($0.id) && !$0.locked } }
    var editableRecords: [[String: Any]] { records.filter { !$0.locked }.map(\.values) }

    init(row: SettingsRowModel, model: AppModel) {
        self.row = row
        self.model = model
    }

    /// The settings' records, taken once: from here on the editor's copy is the
    /// live one, so a redraw cannot undo an edit that has not been saved yet.
    func seed() {
        guard !seeded else { return }
        seeded = true
        let stored = row.value as? [[String: Any]] ?? []
        let locked = collection.lockedRecordCount
        records = stored.enumerated().map {
            CollectionRecord(values: $0.element, locked: $0.offset < locked)
        }
        draft = collection.blankRecord
    }

    func setValue(_ value: Any, column: String, record id: UUID) {
        guard let index = records.firstIndex(where: { $0.id == id }) else { return }
        records[index].values[column] = value
        save()
    }

    /// Empty once the draft was taken; otherwise why it was refused, so the
    /// sheet can stay open with the record still in it.
    func commitDraft() -> [String] {
        let proposed = editableRecords + [draft]
        let refusals = model.bridge.settingsSchema.problems(with: proposed, forRowId: row.rowId)
        guard refusals.isEmpty else { return refusals }
        records.append(CollectionRecord(values: draft, locked: false))
        draft = collection.blankRecord
        save()
        return []
    }

    func removeSelected() {
        let doomed = records.filter { selection.contains($0.id) && !$0.locked }
        guard !doomed.isEmpty else { return }
        deleted += doomed
        records.removeAll { record in doomed.contains { $0.id == record.id } }
        selection = []
        save()
    }

    func run(_ actionId: String) {
        if actionId == "undoDelete" {
            guard let restored = deleted.popLast() else { return }
            records.insert(restored, at: records.firstIndex { !$0.locked } ?? records.count)
        } else if actionId == "undoLatestLearn" {
            guard let index = records.firstIndex(where: { !$0.locked }) else { return }
            deleted.append(records.remove(at: index))
        }
        save()
    }

    func canRun(_ actionId: String) -> Bool {
        switch actionId {
        case "undoDelete": return !deleted.isEmpty
        case "undoLatestLearn": return records.contains { !$0.locked }
        default: return true
        }
    }

    var importContentTypes: [UTType] {
        collection.importFileExtensions.compactMap { UTType(filenameExtension: $0) }
    }

    func importRecords(from url: URL) {
        // The open dialog hands back a security-scoped URL, which has to be
        // claimed before it can be read and released afterwards.
        guard url.startAccessingSecurityScopedResource() else {
            problems = ["Speecher was not allowed to read \(url.lastPathComponent)."]
            return
        }
        defer { url.stopAccessingSecurityScopedResource() }
        guard let data = try? Data(contentsOf: url) else {
            problems = ["Could not read \(url.lastPathComponent)."]
            return
        }
        let result = model.bridge.settingsSchema.recordsImported(from: data,
                                                                into: editableRecords,
                                                                forRowId: row.rowId)
        guard let merged = result.records else {
            problems = [result.problem]
            return
        }
        records = records.filter(\.locked)
            + merged.map { CollectionRecord(values: $0, locked: false) }
        save()
    }

    func tooltip(_ columnId: String, record id: UUID) -> String {
        guard let record = records.first(where: { $0.id == id }) else { return "" }
        return model.bridge.settingsSchema.tooltip(forColumn: columnId,
                                                  inRowId: row.rowId,
                                                  record: record.values)
    }

    private func save() {
        problems = model.save(records: editableRecords, for: row.rowId)
    }
}

/// The whole editor as one form row: the table, the accessory bar under it, and
/// whatever the validator refused.
struct CollectionRow: View {
    let row: SettingsRowModel
    @ObservedObject var model: AppModel
    @StateObject private var editor: CollectionEditor

    init(row: SettingsRowModel, model: AppModel) {
        self.row = row
        self.model = model
        _editor = StateObject(wrappedValue: CollectionEditor(row: row, model: model))
    }

    var body: some View {
        VStack(spacing: 0) {
            table
                // Derive the requested row count from the current system font.
                // Left alone, a table takes the whole pane and pushes out what
                // follows it.
                .frame(height: minimumTableHeight)
            accessoryBar
        }
        .onAppear { editor.seed() }
        ForEach(editor.problems, id: \.self) { problem in
            Text(problem)
        }
    }

    private var minimumTableHeight: CGFloat {
        let lineHeight = NSFont.preferredFont(forTextStyle: .body).boundingRectForFont.height
        // SwiftUI Table adds about one line of vertical padding around each row.
        return CGFloat(max(1, editor.collection.minimumVisibleRows)) * lineHeight * 2
    }

    private var table: some View {
        Table(editor.records, selection: $editor.selection) {
            TableColumnForEach(editor.collection.columns, id: \.columnId) { column in
                TableColumn(column.title) { record in
                    RecordCell(editor: editor, column: column, record: record)
                        .help(editor.tooltip(column.columnId, record: record.id))
                }
                .width(min: Self.width(column).min,
                       ideal: Self.width(column).ideal,
                       max: Self.width(column).max)
            }
        }
        .overlay {
            if editor.records.isEmpty {
                ContentUnavailableView {
                    Label("No \(row.label.isEmpty ? "Records" : row.label)", systemImage: "tray")
                } description: {
                    Text(editor.canAdd
                         ? "Records you add will appear here."
                         : "Speecher fills this in as it learns.")
                } actions: {
                    if editor.canAdd {
                        Button(editor.collection.addLabel) { editor.adding = true }
                    }
                }
            }
        }
    }

    /// How much of the table's width a column asks for. The descriptor names the
    /// one that takes the leftover; a flag needs no more than its checkbox, and
    /// the rest size to the values they hold. Columns stay resizable either way.
    private static func width(_ column: CollectionColumnModel)
        -> (min: CGFloat, ideal: CGFloat, max: CGFloat?) {
        switch (column.kind, column.stretch) {
        case (.toggle, _):
            return (44, 48, 56)
        case (_, true):
            return (100, 132, nil)
        default:
            // Keep every column present at the default settings-window width;
            // people can widen the ones whose values need more room.
            return (52, 72, nil)
        }
    }

    /// Add and remove are the square buttons a list has beneath it on macOS.
    /// They belong here rather than in the window's toolbar, which a settings
    /// window reserves for moving between panes.
    private var accessoryBar: some View {
        HStack {
            if editor.canAdd {
                Button("Add", systemImage: "plus") { editor.adding = true }
                    .help(editor.collection.addLabel)
            }
            Button("Remove", systemImage: "minus") { editor.confirmingRemoval = true }
                .disabled(!editor.canRemove)
            Spacer()
            if !editor.collection.importLabel.isEmpty {
                Button(editor.collection.importLabel) { editor.importing = true }
                    .labelStyle(.titleOnly)
            }
            ForEach(editor.collection.actions, id: \.rowOptionId) { action in
                Button(action.label) { editor.run(action.rowOptionId) }
                    .disabled(!editor.canRun(action.rowOptionId))
                    .labelStyle(.titleOnly)
            }
        }
        .buttonStyle(.accessoryBar)
        .labelStyle(.iconOnly)
        .sheet(isPresented: $editor.adding) {
            AddRecordSheet(editor: editor)
        }
        .confirmationDialog("Remove the selected records?",
                            isPresented: $editor.confirmingRemoval) {
            Button("Remove", role: .destructive) { editor.removeSelected() }
        }
        .fileImporter(isPresented: $editor.importing,
                      allowedContentTypes: editor.importContentTypes) { result in
            if case let .success(url) = result {
                editor.importRecords(from: url)
            }
        }
    }
}

/// Adding a record is a scoped task with its own fields and its own validation,
/// which is what a sheet is for — and it means the record is checked before it
/// exists rather than after a blank row has already been saved.
struct AddRecordSheet: View {
    @ObservedObject var editor: CollectionEditor
    @Environment(\.dismiss) private var dismiss
    @State private var refusals: [String] = []

    private var columns: [CollectionColumnModel] {
        editor.collection.columns.filter { $0.kind != .readOnly }
    }

    var body: some View {
        VStack {
            Form {
                Section {
                    ForEach(columns, id: \.columnId) { column in
                        LabeledContent(column.title) {
                            RecordField(column: column, value: draft(column.columnId))
                        }
                    }
                } header: {
                    Text(editor.collection.addLabel)
                } footer: {
                    ForEach(refusals, id: \.self) { refusal in
                        Text(refusal)
                    }
                }
            }
            .formStyle(.grouped)
            // A macOS sheet has no toolbar to put these in, and Done without
            // Cancel would imply finishing is the only way out.
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }
                Button("Add") {
                    refusals = editor.commitDraft()
                    if refusals.isEmpty { dismiss() }
                }
                .keyboardShortcut(.defaultAction)
            }
            .scenePadding()
        }
        // A sheet does not resize itself to its content the way a window does,
        // so it is told the room its fields need.
        .frame(minWidth: 420, minHeight: 180)
    }

    private func draft(_ columnId: String) -> Binding<Any?> {
        Binding(get: { editor.draft[columnId] },
                set: { editor.draft[columnId] = $0 })
    }
}

/// One table cell. A locked or read-only column is text; anything else is the
/// control its column kind names, editable in place.
struct RecordCell: View {
    @ObservedObject var editor: CollectionEditor
    let column: CollectionColumnModel
    let record: CollectionRecord

    var body: some View {
        if record.locked || column.kind == .readOnly {
            Text(RecordField.display(column, record.values[column.columnId]))
        } else {
            RecordField(column: column, value: value)
        }
    }

    private var value: Binding<Any?> {
        Binding(get: { record.values[column.columnId] },
                set: { newValue in
                    guard let newValue else { return }
                    editor.setValue(newValue, column: column.columnId, record: record.id)
                })
    }
}

/// The control a column's kind asks for, over a record's untyped value. Shared
/// by the table's cells and the add sheet's fields so the two cannot disagree
/// about what a column accepts.
struct RecordField: View {
    let column: CollectionColumnModel
    @Binding var value: Any?

    var body: some View {
        switch column.kind {
        case .toggle:
            Toggle("", isOn: Binding(get: { flag }, set: { value = $0 as NSNumber }))
                .labelsHidden()
        case .choice:
            Picker("", selection: Binding(get: { text }, set: { value = $0 })) {
                ForEach(column.options, id: \.rowOptionId) { option in
                    Text(option.label)
                        .tag(option.rowOptionId)
                        .disabled(!option.enabled)
                }
            }
            .labelsHidden()
        default:
            CellField(text: text) { value = $0 }
        }
    }

    private var flag: Bool { (value as? NSNumber)?.boolValue ?? false }
    private var text: String { Self.string(value) }

    static func string(_ value: Any?) -> String {
        if let text = value as? String { return text }
        if let number = value as? NSNumber { return number.stringValue }
        return ""
    }

    /// What a column that nobody may edit says: a choice shows the label behind
    /// the stored id, and a flag reads as a word rather than an empty checkbox.
    static func display(_ column: CollectionColumnModel, _ value: Any?) -> String {
        let stored = string(value)
        switch column.kind {
        case .choice:
            return column.options.first { $0.rowOptionId == stored }?.label ?? stored
        case .toggle:
            return (value as? NSNumber)?.boolValue == true ? "Yes" : "No"
        default:
            return stored
        }
    }
}

/// Text in a record, saved when the field is done rather than on every
/// keystroke: the collections normalise on save, and a term that momentarily
/// duplicates another one has to survive long enough to be finished.
struct CellField: View {
    let text: String
    let commit: (String) -> Void
    @State private var edited = ""
    @FocusState private var editing: Bool

    var body: some View {
        TextField("", text: $edited)
            .labelsHidden()
            .focused($editing)
            .onSubmit { commit(edited) }
            .onAppear { edited = text }
            .onChange(of: editing) { if !editing { commit(edited) } }
            .onChange(of: text) { _, stored in
                if !editing { edited = stored }
            }
    }
}
