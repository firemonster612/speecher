import AppKit
import SwiftUI

// One stock control per row kind. Nothing here sets a size, a colour, a font or
// a spacing: a grouped form owns all four, and macOS 26 changed all four.

struct RowView: View {
    let row: SettingsRowModel
    @ObservedObject var model: AppModel
    /// False on a grouped row, whose group's GateNote above it explains the
    /// gate; it keeps its own description instead.
    var gateNote = true

    var body: some View {
        if row.enabled {
            control.help(row.tooltip)
        } else if row.kind == .collection {
            // A collection that is not enabled stays readable; its editor
            // stops the edits and says why itself.
            control
        } else if !gateNote || row.disabledAction.isEmpty {
            control.disabled(true).help(row.disabledHelp)
        } else {
            // The schema requires the explanation visible beside the disabled
            // control and its recovery action usable (SettingsSchema.h's
            // disabledHelp contract) — a tooltip alone hides both. The
            // explanation is the row's description; the action goes under it
            // in one container: RowView sits in Form sections and the setup
            // assistant, where sibling views would each become a form row
            // of their own.
            VStack(alignment: .leading) {
                control
                    .disabled(true)
                    .help(row.disabledHelp)
                Button(row.disabledActionLabel) { model.trigger(row.disabledAction) }
            }
        }
    }

    @ViewBuilder private var control: some View {
        switch row.kind {
        case .toggle:
            // A bare Toggle in a grouped form section is a switch, which is what
            // a single independent setting wants; a set of related flags under
            // one heading would be checkboxes inside a LabeledContent.
            Toggle(isOn: model.binding(row, read: Self.flag, write: { $0 as NSNumber })) { label }
        case .choice:
            picker
        case .number:
            LabeledContent {
                if row.units.isEmpty {
                    NumberField(row: row, model: model)
                } else {
                    UnitNumberField(row: row, model: model)
                }
            } label: { label }
        case .text:
            if row.multiline {
                // A paragraph does not fit beside its label, so the field
                // takes the row's width under it.
                VStack(alignment: .leading) {
                    label
                    TextRowField(row: row, model: model)
                }
            } else {
                LabeledContent {
                    if row.secret {
                        SecureTextRowField(row: row, model: model)
                    } else {
                        TextRowField(row: row, model: model)
                    }
                } label: { label }
            }
        case .info:
            LabeledContent { Text(Self.text(row.value)) } label: { label }
        case .action:
            // A caption that follows the state (a Local Runner's name) is the
            // row's value; without one the label names the row.
            LabeledContent { Button(row.actionLabel) { model.trigger(row.rowId) } } label: {
                Self.label(RowView.text(row.value).isEmpty ? row.label : RowView.text(row.value),
                           help: description)
            }
        case .collection:
            // The card's heading and footnote carry this row's label and help,
            // so the table is all there is to draw.
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
        } else if row.rowId == "whatsNewNotes" {
            releaseNotes
        } else if row.rowId == "localModelBrowser" {
            LocalModelBrowser(row: row, model: model)
        } else if row.rowId == "globalShortcut" {
            ShortcutRecorderRow(model: model)
        } else if let role = AppModel.sessionShortcutRoles.first(where: { SpeecherBridge.rowId(for: $0) == row.rowId }) {
            SessionShortcutRecorderRow(model: model, role: role)
        } else if row.rowId == "microphoneTest" {
            microphoneTest
        } else if row.rowId == "openAiAuth" {
            LabeledContent { CredentialField(model: model) } label: { label }
        } else if row.rowId == "anthropicAuth" {
            LabeledContent { CredentialStatusLabel(status: model.anthropicCredentialStatus) } label: { label }
        } else if row.options.isEmpty, row.value is String {
            LabeledContent {
                if row.secret {
                    SecureTextRowField(row: row, model: model)
                } else {
                    TextRowField(row: row, model: model)
                }
            } label: { label }
        } else {
            picker
        }
    }

    /// The input device's live level in a stock progress bar, beside the
    /// button that starts and stops the test. A failure to open the device
    /// takes the description's place.
    private var microphoneTest: some View {
        LabeledContent {
            HStack {
                ProgressView(value: min(max(model.microphoneTestLevel, 0), 1))
                    .accessibilityLabel(model.bridge.inputLevelLabel)
                Button(model.microphoneTestCaption) { model.bridge.toggleMicrophoneTest() }
                    .disabled(!model.microphoneTestEnabled)
            }
        } label: {
            Self.label(row.label, help: model.microphoneTestProblem.isEmpty ? description
                                                                              : model.microphoneTestProblem)
        }
    }

    /// A release's title, its headings, and bullets that wrap under their own
    /// first word rather than under the bullet.
    private var releaseNotes: some View {
        VStack(alignment: .leading, spacing: 8) {
            ForEach(Array(ReleaseNoteLine.parse(Self.text(row.value)).enumerated()), id: \.offset) { _, line in
                switch line {
                case .rule:
                    Divider()
                case .title(let text):
                    Text(Self.inlineMarkdown(text)).font(.title3.weight(.semibold))
                case .heading(let text):
                    Text(Self.inlineMarkdown(text)).font(.headline)
                case .bullet(let text):
                    HStack(alignment: .firstTextBaseline, spacing: 6) {
                        Text(verbatim: "•")
                        Text(Self.inlineMarkdown(text))
                            .fixedSize(horizontal: false, vertical: true)
                    }
                case .text(let text):
                    Text(Self.inlineMarkdown(text))
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .textSelection(.enabled)
    }

    /// A pop-up button, which is what a Picker in a form row already is, and
    /// what the HIG asks for on a flat list of mutually exclusive options.
    private var picker: some View {
        Picker(selection: model.binding(row, read: Self.text, write: { $0 })) {
            ForEach(row.options, id: \.rowOptionId) { option in
                Text(option.label)
                    .tag(option.rowOptionId)
                    .help(option.help)
                    .disabled(!option.enabled)
            }
        } label: {
            label
        }
        .disabled(row.options.allSatisfy { !$0.enabled })
    }

    private var label: some View {
        Self.label(row.label, help: description)
    }

    // Why a lone disabled row is disabled replaces its description, matching
    // the Qt and Windows front ends; showing both would give a gated row two
    // competing descriptions. A grouped row keeps its own, under the group's
    // note.
    private var description: String {
        row.enabled || !gateNote || row.disabledHelp.isEmpty ? row.help : row.disabledHelp
    }

    /// The name of a setting and, under it, what it does. Two Texts in a stock
    /// label is how a settings row says that; SwiftUI sizes and colours the
    /// second one, which is why there is no font or colour here. A builder
    /// rather than a view, so the form still sees two Texts. A row whose card
    /// heading names it has no title of its own.
    @ViewBuilder static func label(_ title: String, help: String) -> some View {
        if !title.isEmpty {
            Text(title)
        }
        if !help.isEmpty {
            Text(help)
        }
    }

    static func flag(_ value: Any?) -> Bool { (value as? NSNumber)?.boolValue ?? false }
    static func number(_ value: Any?) -> Int { (value as? NSNumber)?.intValue ?? 0 }
    static func text(_ value: Any?) -> String { value as? String ?? "" }

    private static func inlineMarkdown(_ text: String) -> AttributedString {
        let options = AttributedString.MarkdownParsingOptions(
            interpretedSyntax: .inlineOnlyPreservingWhitespace)
        return (try? AttributedString(markdown: text, options: options))
            ?? AttributedString(text)
    }
}

/// One line of What's New as it is drawn: core joins a bullet's wrapped lines,
/// and a paragraph's are joined here.
enum ReleaseNoteLine {
    case title(String)
    case heading(String)
    case bullet(String)
    case text(String)
    case rule

    static func parse(_ markdown: String) -> [ReleaseNoteLine] {
        var lines: [ReleaseNoteLine] = []
        for block in markdown.components(separatedBy: "\n\n") {
            var paragraph: [String] = []
            for line in block.components(separatedBy: "\n") where !line.isEmpty {
                let special: ReleaseNoteLine?
                if line == "---" {
                    special = .rule
                } else if line.hasPrefix("# ") {
                    special = .title(String(line.dropFirst(2)))
                } else if line.hasPrefix("#") {
                    special = .heading(String(line.drop(while: { $0 == "#" || $0 == " " })))
                } else if line.hasPrefix("- ") {
                    special = .bullet(String(line.dropFirst(2)))
                } else {
                    special = nil
                }
                guard let special else {
                    paragraph.append(line)
                    continue
                }
                if !paragraph.isEmpty { lines.append(.text(paragraph.joined(separator: " "))) }
                paragraph = []
                lines.append(special)
            }
            if !paragraph.isEmpty { lines.append(.text(paragraph.joined(separator: " "))) }
        }
        return lines
    }
}

/// A sign-in's status: what it says, marked as working or as a problem.
struct CredentialStatusLabel: View {
    let status: SpeecherCredentialStatus

    var body: some View {
        if status.ready {
            Label(status.text, systemImage: "checkmark.circle")
        } else {
            Label(status.text, systemImage: "exclamationmark.triangle")
                .foregroundStyle(.orange)
        }
    }
}

/// Why a row is disabled and, where there is one, the action that lifts the
/// gate: its own form row above a group, or under a collection's table.
struct GateNote: View {
    let row: SettingsRowModel
    @ObservedObject var model: AppModel

    var body: some View {
        if !row.disabledHelp.isEmpty {
            Text(row.disabledHelp)
        }
        if !row.disabledAction.isEmpty {
            Button(row.disabledActionLabel) { model.trigger(row.disabledAction) }
        }
    }
}

/// A number with its range and its unit: the system's numeric field and stepper,
/// at the size they come out at. Typing into the field is the way to cross a
/// wide range that a stepper alone would take all day to walk.
struct NumberField: View {
    let row: SettingsRowModel
    @ObservedObject var model: AppModel
    @State private var value = 0
    @FocusState private var editing: Bool

    var body: some View {
        // A setting is a count, not an amount: 5000 ms, never "5,000". The
        // suffix is spaced from the number only when the schema spaces it,
        // so " ms" reads "500 ms" and "%" reads "5%".
        HStack {
            HStack(spacing: row.suffix.hasPrefix(" ") ? nil : 2) {
                TextField("", value: $value, format: .number.grouping(.never))
                    .labelsHidden()
                    .multilineTextAlignment(.trailing)
                    .fixedSize()
                    .focused($editing)
                    .onSubmit { commit() }
                if !row.suffix.isEmpty {
                    Text(row.suffix.trimmingCharacters(in: .whitespaces))
                }
            }
            Stepper("", value: $value, in: lowerBound...upperBound, step: row.step)
                .labelsHidden()
        }
        .onAppear { value = RowView.number(row.value) }
        .onChange(of: value) { commit() }
        .onChange(of: RowView.number(row.value)) { _, stored in
            if !editing { value = stored }
        }
    }

    private func commit() {
        let clamped = min(max(value, lowerBound), upperBound)
        if clamped != value { value = clamped }
        if clamped != RowView.number(row.value) {
            model.setValue(clamped as NSNumber, for: row.rowId)
        }
    }

    private var lowerBound: Int { min(row.minimum, row.maximum) }
    private var upperBound: Int { max(row.minimum, row.maximum) }
}

/// A number and the unit it is given in: the numeric field and stepper, then a
/// menu of units. The number keeps to the chosen unit's range, so picking a
/// smaller unit can raise it to that unit's minimum.
struct UnitNumberField: View {
    let row: SettingsRowModel
    @ObservedObject var model: AppModel
    @State private var value = 0
    @FocusState private var editing: Bool

    var body: some View {
        HStack {
            TextField("", value: $value, format: .number.grouping(.never))
                .labelsHidden()
                .multilineTextAlignment(.trailing)
                .fixedSize()
                .focused($editing)
                .onSubmit { commit(unit: storedUnit) }
            Stepper("", value: $value, in: range(of: storedUnit))
                .labelsHidden()
            Picker("", selection: Binding(get: { storedUnit }, set: { commit(unit: $0) })) {
                ForEach(row.units, id: \.unitId) { unit in
                    Text(unit.label).tag(unit.unitId)
                }
            }
            .labelsHidden()
            .fixedSize()
        }
        .onAppear { value = storedNumber }
        .onChange(of: value) { commit(unit: storedUnit) }
        .onChange(of: storedNumber) { _, stored in
            if !editing { value = stored }
        }
    }

    private var interval: [String: Any] { row.value as? [String: Any] ?? [:] }
    private var storedNumber: Int { (interval["number"] as? NSNumber)?.intValue ?? 0 }
    private var storedUnit: String { interval["unit"] as? String ?? row.units.first?.unitId ?? "" }

    private func range(of unitId: String) -> ClosedRange<Int> {
        guard let unit = row.units.first(where: { $0.unitId == unitId }) else { return 0...0 }
        return min(unit.minimum, unit.maximum)...max(unit.minimum, unit.maximum)
    }

    private func commit(unit: String) {
        let bounds = range(of: unit)
        let clamped = min(max(value, bounds.lowerBound), bounds.upperBound)
        if clamped != value { value = clamped }
        if clamped != storedNumber || unit != storedUnit {
            model.setValue(["number": clamped, "unit": unit] as NSDictionary, for: row.rowId)
        }
    }
}

/// Free text, saved when the field is done rather than on every keystroke. A row
/// that names values worth offering gets the system's editable combo box, which
/// is the control for text input paired with a list of choices.
struct TextRowField: View {
    let row: SettingsRowModel
    @ObservedObject var model: AppModel
    @State private var text = ""
    @FocusState private var editing: Bool

    var body: some View {
        if !row.suggests {
            // A multi-line field grows downward and shows four lines before
            // it scrolls.
            TextField("", text: $text, prompt: Text(row.placeholder), axis: row.multiline ? .vertical : .horizontal)
                .lineLimit(row.multiline ? 4 : 1, reservesSpace: row.multiline)
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

struct SecureTextRowField: View {
    let row: SettingsRowModel
    @ObservedObject var model: AppModel
    @State private var text = ""
    @FocusState private var editing: Bool

    var body: some View {
        SecureField("", text: $text, prompt: Text(row.placeholder))
            .labelsHidden()
            .focused($editing)
            .onSubmit { commit() }
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

/// An NSComboBox: text a person can type, with the values worth offering behind
/// the same control. SwiftUI has no combo box, and the HIG's own developer
/// reference for the control is AppKit's.
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
        // The list can arrive after the field, as a server's models do once
        // its connection is tested.
        if box.objectValues as? [String] != suggestions {
            box.removeAllItems()
            box.addItems(withObjectValues: suggestions)
        }
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
    @ObservedObject var model: AppModel
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
                    Text(model.credentialProblem)
                }
            }
        } else if let status = model.credentialStatus {
            // Cached on the model: resolving it live can enter the keyring,
            // which must not happen inside a SwiftUI body.
            CredentialStatusLabel(status: status)
        } else {
            Text(SpeecherBridge.checkingCredentialsStatus)
        }
    }
}

/// The cleanup strength, optional tone and instructions of each writing
/// profile. This is a run of ordinary settings rows rather than an editable
/// table — one row per profile, each with its pop-up buttons and its
/// instructions under them. A custom profile adds its name and Delete, and
/// Add profile follows the rows.
struct WritingProfileRows: View {
    let row: SettingsRowModel
    @ObservedObject var model: AppModel
    @State private var deleting: Int?

    private var records: [[String: Any]] { row.value as? [[String: Any]] ?? [] }

    private var choices: [CollectionColumnModel] {
        row.collection?.columns.filter { $0.kind == .choice } ?? []
    }

    private var texts: [CollectionColumnModel] {
        row.collection?.columns.filter { $0.kind == .text } ?? []
    }

    private func isCustom(_ index: Int) -> Bool {
        index >= (row.collection?.lockedRecordCount ?? 0)
    }

    var body: some View {
        ForEach(Array(records.enumerated()), id: \.offset) { index, record in
            VStack(alignment: .leading) {
                LabeledContent(record["profile"] as? String ?? "") {
                    HStack {
                        // Each picker says which column it is, as the Qt
                        // and Windows grids title theirs.
                        ForEach(choices, id: \.columnId) { column in
                            Picker(column.title, selection: choice(index, column.columnId)) {
                                ForEach(column.options, id: \.rowOptionId) { option in
                                    Text(option.label).tag(option.rowOptionId)
                                }
                            }
                            .fixedSize()
                        }
                        if isCustom(index) {
                            Button(row.collection?.deleteLabel ?? "") { delete(index) }
                        }
                    }
                }
                if isCustom(index) {
                    LabeledContent("Name") {
                        CellField(text: RecordField.string(record["profile"])) { edited in
                            let field = choice(index, "profile")
                            if edited != field.wrappedValue { field.wrappedValue = edited }
                        }
                    }
                }
                ForEach(texts, id: \.columnId) { column in
                    LabeledContent(column.title) {
                        CellField(text: RecordField.string(record[column.columnId]),
                                  multiline: column.multiline) { edited in
                            let field = choice(index, column.columnId)
                            if edited != field.wrappedValue { field.wrappedValue = edited }
                        }
                    }
                }
            }
        }
        HStack {
            Spacer()
            Button(row.collection?.addLabel ?? "") {
                guard let blank = row.collection?.blankRecord else { return }
                _ = model.save(records: records + [blank], previous: records, for: row.rowId)
            }
        }
        .confirmationDialog(model.bridge.settingsSchema.writingProfileDeletionTitle,
                            isPresented: Binding(get: { deleting != nil },
                                                 set: { if !$0 { deleting = nil } }),
                            titleVisibility: .visible) {
            Button(row.collection?.deleteLabel ?? "", role: .destructive) {
                if let index = deleting { remove(index) }
            }
        } message: {
            Text(deletionNotice)
        }
    }

    private var deletionNotice: String {
        guard let index = deleting, records.indices.contains(index) else { return "" }
        return notice(index)
    }

    private func notice(_ index: Int) -> String {
        model.bridge.settingsSchema.writingProfileDeletionNotice(records[index]["profileId"] as? String ?? "")
    }

    /// Asks first when a rule or the fallback points at the profile.
    private func delete(_ index: Int) {
        if notice(index).isEmpty {
            remove(index)
        } else {
            deleting = index
        }
    }

    private func remove(_ index: Int) {
        var edited = records
        guard edited.indices.contains(index) else { return }
        edited.remove(at: index)
        _ = model.save(records: edited, previous: records, for: row.rowId)
    }

    private func choice(_ index: Int, _ columnId: String) -> Binding<String> {
        Binding(get: {
                    guard records.indices.contains(index) else { return "" }
                    return records[index][columnId] as? String ?? ""
                },
                set: { newValue in
                    var edited = records
                    guard edited.indices.contains(index) else { return }
                    edited[index][columnId] = newValue
                    _ = model.save(records: edited, previous: records, for: row.rowId)
                })
    }
}
