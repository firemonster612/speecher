import SwiftUI
import UniformTypeIdentifiers

// The five collection editors — application rules, paste rules, vocabulary,
// learned corrections, replacements — are one table driven by the descriptor
// behind whichever row asked for it.
//
// The shape is System Settings': a table, a +/− accessory bar under it, and
// adding through a sheet so the record can be checked before it exists. A
// collection that names an edit command opens a record in the same sheet.
// Removing asks nothing, because Undo delete puts it back, as on Linux and
// Windows. A grouped form caps its content width, which is why
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
    /// Set when an import is what was refused.
    @Published private(set) var problemsTitle = ""
    @Published var recordSheetShown = false
    @Published var importing = false
    /// The record the sheet is filling in: a blank one, or a copy of the one
    /// being edited.
    @Published var draft: [String: Any] = [:]
    /// The record the sheet edits; nil while it adds one.
    @Published private(set) var editing: UUID?

    /// What Delete took and where it stood, newest last, so undo can put it
    /// back in its place.
    private var deleted: [(index: Int, record: CollectionRecord)] = []
    private var seeded = false
    private var savedRecords: [[String: Any]] = []

    let row: SettingsRowModel
    let model: AppModel

    var collection: CollectionModel { row.collection! }
    var canAdd: Bool { !collection.addLabel.isEmpty }
    var canRemove: Bool { records.contains { selection.contains($0.id) && !$0.locked } }
    var canEdit: Bool { !collection.editLabel.isEmpty }
    /// The selected record when it is the only one and may be edited.
    var selectedEditable: UUID? {
        guard selection.count == 1, let id = selection.first,
              records.contains(where: { $0.id == id && !$0.locked }) else { return nil }
        return id
    }
    var editableRecords: [[String: Any]] { records.filter { !$0.locked }.map(\.values) }
    /// A dialog-only column stays out of the table only where the sheet can
    /// reopen a record; elsewhere its cell is the one place to change it.
    var tableColumns: [CollectionColumnModel] {
        collection.columns.filter { !($0.dialogOnly && canEdit) }
    }

    init(row: SettingsRowModel, model: AppModel) {
        self.row = row
        self.model = model
    }

    /// The settings' records, taken once: from here on the editor's copy is the
    /// live one, so a redraw cannot undo an edit that has not been saved yet.
    func seed() {
        guard !seeded else { return }
        seeded = true
        load(from: row)
    }

    /// A settings reopen reloaded the draft. A clean editor replaces its
    /// records and its merge baseline together from the fresh snapshot, so
    /// records learned or changed while the window was closed appear. A dirty
    /// editor — a refused save, or the sheet mid-record — keeps its state:
    /// reloading would silently discard pending work, and the next successful
    /// save re-syncs both.
    func reload(from freshRow: SettingsRowModel) {
        guard seeded, problems.isEmpty, !recordSheetShown else { return }
        load(from: freshRow)
        // The reloaded records carry new identities, so the old selection
        // points at nothing.
        selection = []
    }

    private func load(from row: SettingsRowModel) {
        let stored = row.value as? [[String: Any]] ?? []
        let locked = collection.lockedRecordCount
        records = stored.enumerated().map {
            CollectionRecord(values: $0.element, locked: $0.offset < locked)
        }
        savedRecords = editableRecords
    }

    func setValue(_ value: Any, column: String, record id: UUID) {
        guard let index = records.firstIndex(where: { $0.id == id }) else { return }
        records[index].values[column] = value
        save()
    }

    func add() {
        editing = nil
        draft = collection.blankRecord
        recordSheetShown = true
    }

    func edit(_ id: UUID) {
        guard canEdit, let record = records.first(where: { $0.id == id }), !record.locked else { return }
        editing = id
        draft = record.values
        recordSheetShown = true
    }

    /// The add sheet's title, or the edited record's name: its first text value.
    var sheetTitle: String {
        guard let record = records.first(where: { $0.id == editing }),
              let name = collection.columns.first(where: { $0.kind == .text }) else {
            return collection.addDialogTitle
        }
        return RecordField.string(record.values[name.columnId])
    }

    /// Empty once the sheet's record was added or replaced the edited one;
    /// otherwise why it was refused, so the sheet can stay open with the
    /// record still in it.
    func commit(_ record: [String: Any]) -> [String] {
        var proposed = records
        if let index = proposed.firstIndex(where: { $0.id == editing }) {
            proposed[index].values = record
        } else {
            proposed.append(CollectionRecord(values: record, locked: false))
        }
        let refusals = model.bridge.settingsSchema.problems(
            with: proposed.filter { !$0.locked }.map(\.values), forRowId: row.rowId)
        guard refusals.isEmpty else { return refusals }
        records = proposed
        save()
        return []
    }

    func removeSelected() {
        // Last first, so each index still holds when undo walks them back.
        let doomed = records.indices.reversed().filter {
            selection.contains(records[$0].id) && !records[$0].locked
        }
        guard !doomed.isEmpty else { return }
        for index in doomed {
            deleted.append((index, records.remove(at: index)))
        }
        selection = []
        save()
    }

    func run(_ actionId: String) {
        if actionId == "undoDelete" {
            guard let restored = deleted.popLast() else { return }
            records.insert(restored.record, at: min(restored.index, records.count))
        } else if actionId == "undoLatestLearn" {
            guard let index = records.firstIndex(where: { !$0.locked }) else { return }
            deleted.append((index, records.remove(at: index)))
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
            refuseImport("Speecher was not allowed to read \(url.lastPathComponent).")
            return
        }
        defer { url.stopAccessingSecurityScopedResource() }
        guard let data = try? Data(contentsOf: url) else {
            refuseImport("Could not read \(url.lastPathComponent).")
            return
        }
        let result = model.bridge.settingsSchema.recordsImported(from: data,
                                                                into: editableRecords,
                                                                forRowId: row.rowId)
        guard let merged = result.records else {
            refuseImport(result.problem)
            return
        }
        records = records.filter(\.locked)
            + merged.map { CollectionRecord(values: $0, locked: false) }
        save()
    }

    /// Each Icon column's cells by record, keyed by column, re-derived from
    /// the records as they now stand whenever they change.
    var iconCells: [String: [UUID: CollectionIconCell]] {
        let values = records.map(\.values)
        var cells: [String: [UUID: CollectionIconCell]] = [:]
        for column in tableColumns where column.kind == .icon {
            let icons = model.bridge.settingsSchema.icons(forColumn: column.columnId,
                                                          inRowId: row.rowId,
                                                          records: values)
            cells[column.columnId] = Dictionary(uniqueKeysWithValues: zip(records.map(\.id), icons))
        }
        return cells
    }

    func choiceSetText(_ column: CollectionColumnModel, record: CollectionRecord) -> String {
        model.bridge.settingsSchema.choiceSetText(forColumn: column.columnId,
                                                  inRowId: row.rowId,
                                                  ids: record.values[column.columnId] as? [String] ?? [])
    }

    func tooltip(_ columnId: String, record id: UUID) -> String {
        guard let record = records.first(where: { $0.id == id }) else { return "" }
        return model.bridge.settingsSchema.tooltip(forColumn: columnId,
                                                  inRowId: row.rowId,
                                                  record: record.values)
    }

    private func refuseImport(_ problem: String) {
        problems = [problem]
        problemsTitle = collection.importFailureTitle
    }

    private func save() {
        let submitted = editableRecords
        problems = model.save(records: submitted, previous: savedRecords, for: row.rowId)
        problemsTitle = ""
        if problems.isEmpty { savedRecords = submitted }
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
                // The descriptor says how tall this table is worth being. Left
                // to itself in a form it takes every point the pane has and
                // pushes whatever follows it off the bottom.
                .frame(height: CGFloat(editor.collection.minimumHeight))
            accessoryBar
        }
        .onAppear { editor.seed() }
        // The window and this view survive close/reopen, so a reopened draft
        // arrives as a generation bump rather than a fresh onAppear.
        .onChange(of: model.draftGeneration) { editor.reload(from: row) }
        if !row.enabled {
            GateNote(row: row, model: model)
        }
        if !editor.problems.isEmpty {
            // Refusals are errors: the records were not saved.
            RefusalLabel(title: editor.problemsTitle, problems: editor.problems)
        }
    }

    private var table: some View {
        let iconCells = editor.iconCells
        return Table(editor.records, selection: $editor.selection) {
            TableColumnForEach(editor.tableColumns, id: \.columnId) { column in
                TableColumn(Self.header(column)) { record in
                    if column.kind == .icon {
                        RecordIcon(iconId: column.iconId, cell: iconCells[column.columnId]?[record.id])
                    } else {
                        VStack(alignment: .leading) {
                            RecordCell(editor: editor, column: column, record: record,
                                       editable: row.enabled)
                                .help(editor.tooltip(column.columnId, record: record.id))
                            if !column.detailColumn.isEmpty {
                                RecordDetail(text: RecordField.string(record.values[column.detailColumn]))
                            }
                        }
                    }
                }
                .width(min: Self.width(column).min,
                       ideal: Self.width(column).ideal,
                       max: Self.width(column).max)
                .alignment(column.kind == .icon ? .center : .automatic)
            }
        }
        // Double-click or Return opens a record, as the Edit command does.
        .contextMenu(forSelectionType: CollectionRecord.ID.self) { _ in
            EmptyView()
        } primaryAction: { ids in
            if row.enabled, ids.count == 1, let id = ids.first {
                editor.edit(id)
            }
        }
        .overlay {
            if editor.records.isEmpty && !editor.collection.emptyTitle.isEmpty {
                ContentUnavailableView {
                    Label(editor.collection.emptyTitle, systemImage: "tray")
                } description: {
                    Text(editor.collection.emptyHelp)
                } actions: {
                    if editor.canAdd {
                        Button(editor.collection.addLabel) { editor.add() }
                            .disabled(!row.enabled)
                    }
                }
            }
        }
    }

    /// A column's header: its title, or an Icon column's icon alone. A table
    /// header takes only Text, which keeps neither a tooltip nor an
    /// accessibility label, so the icon's cells carry the meaning.
    private static func header(_ column: CollectionColumnModel) -> Text {
        guard column.kind == .icon else { return Text(column.title) }
        return Text(Image(systemName: RecordIcon.symbol(column.iconId)))
    }

    /// How much of the table's width a column asks for. The descriptor names the
    /// one that takes the leftover; a flag needs no more than its checkbox, an
    /// icon no more than itself, and the rest size to the values they hold.
    /// Columns other than an icon's stay resizable.
    private static func width(_ column: CollectionColumnModel)
        -> (min: CGFloat, ideal: CGFloat, max: CGFloat?) {
        switch (column.kind, column.stretch) {
        case (.toggle, _):
            return (44, 48, 56)
        case (.icon, _):
            return (28, 28, 28)
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
                Button("Add", systemImage: "plus") { editor.add() }
                    .help(editor.collection.addLabel)
            }
            Button(editor.collection.deleteLabel, systemImage: "minus") { editor.removeSelected() }
                .help(editor.collection.deleteLabel)
                .disabled(!editor.canRemove)
            if editor.canEdit {
                Button(editor.collection.editLabel) {
                    if let id = editor.selectedEditable { editor.edit(id) }
                }
                .disabled(editor.selectedEditable == nil)
            }
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
        // Readable while disabled: only the edits stop.
        .disabled(!row.enabled)
        .buttonStyle(.accessoryBar)
        .labelStyle(.iconOnly)
        .sheet(isPresented: $editor.recordSheetShown) {
            RecordSheet(editor: editor)
        }
        .fileImporter(isPresented: $editor.importing,
                      allowedContentTypes: editor.importContentTypes) { result in
            if case let .success(url) = result {
                editor.importRecords(from: url)
            }
        }
    }
}

/// Adding or editing a record is a scoped task with its own fields and its own
/// validation, which is what a sheet is for — and it means the record is
/// checked before it exists rather than after a blank row has already been
/// saved.
struct RecordSheet: View {
    @ObservedObject var editor: CollectionEditor
    @Environment(\.dismiss) private var dismiss
    @State private var refusals: [String] = []
    /// Which ChoiceSet fields are on their second choice, by column. Apart from
    /// the ticks, so switching to every option and back keeps them.
    @State private var limited: [String: Bool]

    init(editor: CollectionEditor) {
        _editor = ObservedObject(wrappedValue: editor)
        var limited: [String: Bool] = [:]
        for column in editor.collection.columns where column.kind == .choiceSet {
            limited[column.columnId] = !Self.ticked(column, in: editor.draft).isEmpty
        }
        _limited = State(initialValue: limited)
    }

    /// Read-only and icon columns hold nothing to fill in.
    private var columns: [CollectionColumnModel] {
        editor.collection.columns.filter { $0.kind != .readOnly && $0.kind != .icon }
    }

    var body: some View {
        VStack {
            Form {
                Section {
                    ForEach(columns, id: \.columnId) { column in
                        row(column)
                    }
                } header: {
                    Text(editor.sheetTitle)
                } footer: {
                    if !refusals.isEmpty {
                        RefusalLabel(title: "", problems: refusals)
                    }
                }
            }
            .formStyle(.grouped)
            // A macOS sheet has no toolbar to put these in, and Done without
            // Cancel would imply finishing is the only way out.
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }
                Button(editor.editing == nil ? "Add" : "OK") {
                    refusals = editor.commit(record)
                    if refusals.isEmpty { dismiss() }
                }
                .keyboardShortcut(.defaultAction)
                .disabled(limitedToNothing)
            }
            .scenePadding()
        }
        // A grouped form scrolls, so it gives a sheet no height to fit to; the
        // sheet is told the room its fields need, and can be resized.
        .frame(minWidth: 420, minHeight: 180, idealHeight: idealHeight)
        .presentationSizing(.fitted)
    }

    /// A line per field, more for a multi-line field, a ChoiceSet's options and
    /// help under a field, plus the header, the grouped form's margins and the
    /// buttons. Short of it, the form scrolls inside a sheet with room to spare.
    private var idealHeight: CGFloat {
        let lines = columns.reduce(0) { lines, column in
            let field = column.kind == .choiceSet ? 2 + column.options.count : column.multiline ? 4 : 1
            return lines + field + (column.help.isEmpty ? 0 : 2)
        }
        return CGFloat(lines) * 24 + 150
    }

    @ViewBuilder private func row(_ column: CollectionColumnModel) -> some View {
        if column.kind == .toggle {
            // The box, its name beside it and, under the name, what it does,
            // in a settings row's two Texts.
            Toggle(isOn: flag(column.columnId)) {
                RowView.label(column.title, help: column.help)
            }
            .toggleStyle(.checkbox)
            .disabled(!enabled(column))
        } else {
            LabeledContent(column.title) {
                VStack(alignment: .leading) {
                    field(column)
                    if !column.help.isEmpty {
                        Text(column.help)
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
            }
        }
    }

    @ViewBuilder private func field(_ column: CollectionColumnModel) -> some View {
        if column.kind == .choiceSet {
            ChoiceSetField(column: column,
                           ids: Binding(get: { Self.ticked(column, in: editor.draft) },
                                        set: { editor.draft[column.columnId] = $0 }),
                           limited: Binding(get: { limited[column.columnId] ?? false },
                                            set: { limited[column.columnId] = $0 }))
        } else {
            RecordField(column: column, value: draft(column.columnId),
                        commitsImmediately: true, placeholder: column.placeholder)
        }
    }

    /// A toggle stays off-limits while the toggle it depends on is off.
    private func enabled(_ column: CollectionColumnModel) -> Bool {
        column.enabledBy.isEmpty || (editor.draft[column.enabledBy] as? NSNumber)?.boolValue == true
    }

    /// Only some options with none of them ticked cannot be kept.
    private var limitedToNothing: Bool {
        columns.contains { limited[$0.columnId] == true && Self.ticked($0, in: editor.draft).isEmpty }
    }

    /// The draft as the sheet keeps it: a ChoiceSet on every option holds no ids.
    private var record: [String: Any] {
        var record = editor.draft
        for column in columns where column.kind == .choiceSet {
            let ids: [String] = limited[column.columnId] == true ? Self.ticked(column, in: record) : []
            record[column.columnId] = ids
        }
        return record
    }

    /// A ChoiceSet's ids that are options it offers, in the options' order.
    private static func ticked(_ column: CollectionColumnModel, in record: [String: Any]) -> [String] {
        let ids = record[column.columnId] as? [String] ?? []
        return column.options.map(\.rowOptionId).filter(ids.contains)
    }

    private func flag(_ columnId: String) -> Binding<Bool> {
        Binding(get: { RowView.flag(editor.draft[columnId]) },
                set: { editor.draft[columnId] = $0 as NSNumber })
    }

    private func draft(_ columnId: String) -> Binding<Any?> {
        Binding(get: { editor.draft[columnId] },
                set: { editor.draft[columnId] = $0 })
    }
}

/// Every option, or only the ticked ones: two radio buttons, with a checkbox
/// per option under the second that only it enables.
struct ChoiceSetField: View {
    let column: CollectionColumnModel
    @Binding var ids: [String]
    @Binding var limited: Bool

    var body: some View {
        VStack(alignment: .leading) {
            Picker("", selection: $limited) {
                Text(column.everyChoice).tag(false)
                Text(column.someChoice).tag(true)
            }
            .pickerStyle(.radioGroup)
            .labelsHidden()
            // Named for themselves: the row's LabeledContent would otherwise
            // give each control its title, so VoiceOver read "Profiles" for all.
            .accessibilityLabel(column.title)
            VStack(alignment: .leading) {
                ForEach(column.options, id: \.rowOptionId) { option in
                    Toggle(option.label, isOn: tick(option.rowOptionId))
                        .toggleStyle(.checkbox)
                        .accessibilityLabel(option.label)
                }
            }
            .padding(.leading)
            .disabled(!limited)
        }
    }

    private func tick(_ id: String) -> Binding<Bool> {
        Binding(get: { ids.contains(id) },
                set: { ticked in ids = ticked ? ids + [id] : ids.filter { $0 != id } })
    }
}

/// Why records were refused, the same in the table, the sheet and an
/// import: the records were not saved, so it reads as an error.
struct RefusalLabel: View {
    let title: String
    let problems: [String]

    var body: some View {
        Label {
            VStack(alignment: .leading) {
                if !title.isEmpty {
                    Text(title).bold()
                }
                ForEach(problems, id: \.self) { Text($0) }
            }
        } icon: {
            Image(systemName: "exclamationmark.octagon.fill")
        }
        .foregroundStyle(.red)
    }
}

/// What a record's detail column holds, as one muted line under its cell's own.
struct RecordDetail: View {
    let text: String

    var body: some View {
        // One line: a multi-line detail reads as one.
        let line = text.split(whereSeparator: \.isWhitespace).joined(separator: " ")
        if !line.isEmpty {
            Text(line)
                .foregroundStyle(.secondary)
                .lineLimit(1)
                .truncationMode(.tail)
                .help(text)
        }
    }
}

/// An Icon column's cell: the column's symbol in the text colour, in the
/// secondary style when faint, or nothing. The tooltip says which either way.
struct RecordIcon: View {
    let iconId: String
    let cell: CollectionIconCell?

    var body: some View {
        icon
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .contentShape(Rectangle())
            .help(cell?.tooltip ?? "")
    }

    @ViewBuilder private var icon: some View {
        switch cell?.state {
        case .shown?:
            image
        case .faint?:
            image.foregroundStyle(.secondary)
        default:
            Color.clear
        }
    }

    /// Read by VoiceOver as the tooltip, which says what the icon means.
    private var image: some View {
        Image(systemName: Self.symbol(iconId)).accessibilityLabel(cell?.tooltip ?? "")
    }

    /// SF Symbols for the schema's icon ids, filled as Mail marks a flagged message.
    static func symbol(_ iconId: String) -> String {
        switch iconId {
        case "microphone": return "mic.fill"
        case "star": return "star.fill"
        default: return "circle.fill"
        }
    }
}

/// One table cell. A locked or read-only column is text; anything else is the
/// control its column kind names, editable in place.
struct RecordCell: View {
    @ObservedObject var editor: CollectionEditor
    let column: CollectionColumnModel
    let record: CollectionRecord
    var editable = true

    var body: some View {
        if column.kind == .choiceSet {
            // Picked in the sheet; the cell names the options.
            let text = editor.choiceSetText(column, record: record)
            Text(text).help(text)
        } else if record.locked || column.kind == .readOnly || !editable {
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
/// by the table's cells and the sheet's fields so the two cannot disagree
/// about what a column accepts.
struct RecordField: View {
    let column: CollectionColumnModel
    @Binding var value: Any?
    /// The sheet's fields write through on every keystroke: clicking the
    /// sheet's Add button does not move focus on macOS, so a field that only
    /// commits on focus-out would hand commit(_:) a draft missing the text
    /// still sitting in the field. Table cells keep committing on focus-out,
    /// which is what lets a term momentarily duplicate another while typed.
    var commitsImmediately = false
    /// The sheet's example in an empty field; table cells show none.
    var placeholder = ""

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
            CellField(text: text, multiline: column.multiline,
                      commitsImmediately: commitsImmediately, placeholder: placeholder) { value = $0 }
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
    /// A snippet's lines: the field grows downward to show them.
    var multiline = false
    var commitsImmediately = false
    var placeholder = ""
    let commit: (String) -> Void
    @State private var edited = ""
    @FocusState private var editing: Bool

    var body: some View {
        // Option-Return starts a new line, as in every multi-line field. The
        // sheet shows room for four; a table cell grows to at most four.
        TextField("", text: $edited, prompt: placeholder.isEmpty ? nil : Text(placeholder),
                  axis: multiline ? .vertical : .horizontal)
            .lineLimit(4, reservesSpace: multiline && commitsImmediately)
            .labelsHidden()
            .focused($editing)
            .onSubmit { commit(edited) }
            .onAppear { edited = text }
            .onChange(of: edited) {
                if commitsImmediately { commit(edited) }
            }
            // No focus-out commit in immediate mode: every keystroke has
            // already landed.
            .onChange(of: editing) {
                if !editing && !commitsImmediately { commit(edited) }
            }
            .onChange(of: text) { _, stored in
                if !editing { edited = stored }
            }
    }
}
