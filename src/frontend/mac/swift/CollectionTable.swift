import AppKit
import SwiftUI
import UniformTypeIdentifiers

// The five collection editors — application rules, paste rules, vocabulary,
// learned corrections, replacements — are one table driven by the descriptor
// behind whichever row asked for it, the way the Qt front end's single
// CollectionEditor serves all five.

/// The records an editor is showing, which are the settings' until someone
/// changes them and the store takes the change.
@MainActor
final class CollectionEditor: ObservableObject {
    @Published private(set) var records: [[String: Any]] = []
    @Published var selectedRows = IndexSet()
    /// Why the records were refused, which is also why they were not saved.
    @Published private(set) var problems: [String] = []
    /// Bumped when the rows themselves changed, so the table reloads then and
    /// not on every unrelated redraw — a reload cancels an edit in progress.
    @Published private(set) var generation = 0

    /// What Delete took, newest last, so undo can put it back.
    private var deleted: [[String: Any]] = []
    private var seeded = false

    let row: SettingsRowModel
    let model: SettingsModel

    var collection: CollectionModel { row.collection! }
    var lockedCount: Int { collection.lockedRecordCount }
    var editableRecords: [[String: Any]] { Array(records.dropFirst(lockedCount)) }
    var canDelete: Bool { selectedRows.contains { $0 >= lockedCount } }

    init(row: SettingsRowModel, model: SettingsModel) {
        self.row = row
        self.model = model
    }

    /// The settings' records, taken once: from here on the editor's copy is the
    /// live one, so a redraw cannot undo an edit that has not been saved yet.
    func seed(from row: SettingsRowModel) {
        guard !seeded else { return }
        seeded = true
        show(row.value as? [[String: Any]] ?? [])
    }

    func setValue(_ value: Any, column: String, record index: Int) {
        guard records.indices.contains(index) else { return }
        records[index][column] = value
        save()
    }

    func add() {
        records.append(collection.blankRecord)
        show(records)
    }

    func deleteSelected() {
        let doomed = selectedRows.filter { $0 >= lockedCount }.sorted(by: >)
        guard !doomed.isEmpty else { return }
        for index in doomed {
            deleted.append(records[index])
            records.remove(at: index)
        }
        selectedRows = IndexSet()
        show(records)
    }

    func run(_ actionId: String) {
        if actionId == "undoDelete" {
            guard let restored = deleted.popLast() else { return }
            records.insert(restored, at: lockedCount)
        } else if actionId == "undoLatestLearn" {
            guard records.count > lockedCount else { return }
            deleted.append(records.remove(at: lockedCount))
        }
        show(records)
    }

    func canRun(_ actionId: String) -> Bool {
        switch actionId {
        case "undoDelete": return !deleted.isEmpty
        case "undoLatestLearn": return records.count > lockedCount
        default: return true
        }
    }

    func importRecords() {
        let panel = NSOpenPanel()
        panel.title = collection.importLabel
        panel.allowedContentTypes = collection.importFileExtensions.compactMap {
            UTType(filenameExtension: $0)
        }
        panel.allowsOtherFileTypes = true
        guard panel.runModal() == .OK, let url = panel.url else { return }
        guard let data = try? Data(contentsOf: url) else {
            problems = ["Could not read \(url.path)."]
            return
        }
        let result = model.bridge.settingsSchema.recordsImported(from: data,
                                                                 into: editableRecords,
                                                                 forRowId: row.rowId)
        guard let merged = result.records else {
            problems = [result.problem]
            return
        }
        show(Array(records.prefix(lockedCount)) + merged)
    }

    func tooltip(_ columnId: String, record index: Int) -> String {
        guard records.indices.contains(index) else { return "" }
        return model.bridge.settingsSchema.tooltip(forColumn: columnId,
                                                   inRowId: row.rowId,
                                                   record: records[index])
    }

    private func show(_ records: [[String: Any]]) {
        self.records = records
        generation += 1
        save()
    }

    private func save() {
        problems = model.save(records: editableRecords, for: row.rowId)
    }
}

/// The whole editor as a settings row: the table, the buttons under it, and
/// whatever the validator refused.
struct CollectionRow: View {
    let row: SettingsRowModel
    @ObservedObject var model: SettingsModel
    @StateObject private var editor: CollectionEditor

    init(row: SettingsRowModel, model: SettingsModel) {
        self.row = row
        self.model = model
        _editor = StateObject(wrappedValue: CollectionEditor(row: row, model: model))
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            if !row.label.isEmpty {
                Text(row.label)
            }
            if !row.help.isEmpty {
                Text(row.help).font(.caption).foregroundStyle(.secondary)
            }
            RecordTable(editor: editor)
                .frame(minHeight: CGFloat(max(row.collection?.minimumHeight ?? 0, 160)))
                .clipShape(RoundedRectangle(cornerRadius: 6))
                .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(.separator))
            buttons
            ForEach(editor.problems, id: \.self) { problem in
                Text(problem).font(.caption).foregroundStyle(.red)
            }
        }
        .onAppear { editor.seed(from: row) }
    }

    private var buttons: some View {
        HStack {
            if !editor.collection.importLabel.isEmpty {
                Button(editor.collection.importLabel) { editor.importRecords() }
            }
            Spacer()
            ForEach(editor.collection.actions, id: \.rowOptionId) { action in
                Button(action.label) { editor.run(action.rowOptionId) }
                    .disabled(!editor.canRun(action.rowOptionId))
            }
            Button("Delete selected") { editor.deleteSelected() }
                .disabled(!editor.canDelete)
            if !editor.collection.addLabel.isEmpty {
                Button(editor.collection.addLabel) { editor.add() }
            }
        }
    }
}

/// The AppKit island. SwiftUI has no table with editable typed cells, and this
/// one is a plain view-based NSTableView built from the described columns.
struct RecordTable: NSViewRepresentable {
    @ObservedObject var editor: CollectionEditor

    func makeCoordinator() -> Coordinator { Coordinator(editor: editor) }

    func makeNSView(context: Context) -> NSScrollView {
        let table = NSTableView()
        table.style = .inset
        table.usesAlternatingRowBackgroundColors = true
        table.allowsMultipleSelection = true
        table.rowHeight = 22
        table.dataSource = context.coordinator
        table.delegate = context.coordinator
        // Only the columns the descriptor calls stretchy take the leftover
        // width; the rest size to what they hold.
        table.columnAutoresizingStyle = .uniformColumnAutoresizingStyle
        for column in editor.collection.columns {
            let tableColumn = NSTableColumn(identifier: .init(column.columnId))
            tableColumn.title = column.title
            tableColumn.resizingMask = column.stretch ? [.autoresizingMask, .userResizingMask]
                                                      : .userResizingMask
            tableColumn.minWidth = column.stretch ? 120 : 56
            tableColumn.width = column.stretch ? 180 : 88
            table.addTableColumn(tableColumn)
        }
        let scroll = NSScrollView()
        scroll.documentView = table
        scroll.hasVerticalScroller = true
        // Narrow enough columns can still add up to more than the card is wide,
        // and a column a reader cannot reach is a column that may as well not
        // hold anything.
        scroll.hasHorizontalScroller = true
        scroll.borderType = .noBorder
        scroll.drawsBackground = false
        context.coordinator.table = table
        return scroll
    }

    func updateNSView(_ scroll: NSScrollView, context: Context) {
        context.coordinator.editor = editor
        guard context.coordinator.shownGeneration != editor.generation else { return }
        context.coordinator.shownGeneration = editor.generation
        context.coordinator.table?.reloadData()
    }

    @MainActor
    final class Coordinator: NSObject, NSTableViewDataSource, NSTableViewDelegate {
        var editor: CollectionEditor
        var table: NSTableView?
        var shownGeneration = -1

        init(editor: CollectionEditor) {
            self.editor = editor
        }

        private func column(_ tableColumn: NSTableColumn?) -> CollectionColumnModel? {
            guard let id = tableColumn?.identifier.rawValue else { return nil }
            return editor.collection.columns.first { $0.columnId == id }
        }

        func numberOfRows(in tableView: NSTableView) -> Int {
            editor.records.count
        }

        func tableView(_ tableView: NSTableView,
                       viewFor tableColumn: NSTableColumn?,
                       row: Int) -> NSView? {
            guard let column = column(tableColumn) else { return nil }
            let record = editor.records[row]
            let value = record[column.columnId]
            let locked = row < editor.lockedCount
            let cell: NSView
            if locked || column.kind == .readOnly {
                cell = label(column, value)
            } else {
                switch column.kind {
                case .toggle:
                    cell = checkbox(column, value, row)
                case .choice:
                    cell = popUp(column, value, row)
                default:
                    cell = textField(column, value, row)
                }
            }
            cell.toolTip = editor.tooltip(column.columnId, record: row)
            return cell
        }

        func tableViewSelectionDidChange(_ notification: Notification) {
            guard let table = notification.object as? NSTableView else { return }
            editor.selectedRows = table.selectedRowIndexes
        }

        private func optionLabel(_ column: CollectionColumnModel, _ value: Any?) -> String {
            let id = string(value)
            return column.options.first { $0.rowOptionId == id }?.label ?? id
        }

        private func string(_ value: Any?) -> String {
            if let text = value as? String { return text }
            if let number = value as? NSNumber { return number.stringValue }
            return ""
        }

        private func label(_ column: CollectionColumnModel, _ value: Any?) -> NSView {
            let field = NSTextField(labelWithString: column.kind == .choice
                                    ? optionLabel(column, value)
                                    : string(value))
            field.lineBreakMode = .byTruncatingTail
            if column.kind == .toggle {
                field.stringValue = (value as? NSNumber)?.boolValue == true ? "Yes" : "No"
            }
            return field
        }

        private func textField(_ column: CollectionColumnModel, _ value: Any?, _ row: Int) -> NSView {
            let field = RecordTextField(string: string(value))
            field.isBordered = false
            field.drawsBackground = false
            field.recordRow = row
            field.columnId = column.columnId
            field.target = self
            field.action = #selector(textEdited(_:))
            return field
        }

        private func checkbox(_ column: CollectionColumnModel, _ value: Any?, _ row: Int) -> NSView {
            let box = RecordCheckbox(checkboxWithTitle: "", target: self, action: #selector(flagToggled(_:)))
            box.state = (value as? NSNumber)?.boolValue == true ? .on : .off
            box.recordRow = row
            box.columnId = column.columnId
            return box
        }

        private func popUp(_ column: CollectionColumnModel, _ value: Any?, _ row: Int) -> NSView {
            let button = RecordPopUp()
            button.isBordered = false
            button.recordRow = row
            button.columnId = column.columnId
            button.optionIds = column.options.map(\.rowOptionId)
            for option in column.options {
                button.addItem(withTitle: option.label)
                button.lastItem?.isEnabled = option.enabled
                button.lastItem?.toolTip = option.help
            }
            button.selectItem(at: button.optionIds.firstIndex(of: string(value)) ?? 0)
            button.target = self
            button.action = #selector(choicePicked(_:))
            return button
        }

        @objc private func textEdited(_ sender: RecordTextField) {
            editor.setValue(sender.stringValue, column: sender.columnId, record: sender.recordRow)
        }

        @objc private func flagToggled(_ sender: RecordCheckbox) {
            editor.setValue(sender.state == .on, column: sender.columnId, record: sender.recordRow)
        }

        @objc private func choicePicked(_ sender: RecordPopUp) {
            let index = sender.indexOfSelectedItem
            guard sender.optionIds.indices.contains(index) else { return }
            editor.setValue(sender.optionIds[index], column: sender.columnId, record: sender.recordRow)
        }
    }
}

// Which cell a control belongs to, so an edit knows what it changed. Table
// views hand their cells back for reuse, so this travels with the view rather
// than being captured when it is made.
private final class RecordTextField: NSTextField {
    var recordRow = 0
    var columnId = ""
}

private final class RecordCheckbox: NSButton {
    var recordRow = 0
    var columnId = ""
}

private final class RecordPopUp: NSPopUpButton {
    var recordRow = 0
    var columnId = ""
    var optionIds: [String] = []
}
