import AppKit
import SwiftUI

// One stock control per row kind. Nothing here sets a size, a colour, a font or
// a spacing: a grouped form owns all four, and macOS 26 changed all four.

struct RowView: View {
    let row: SettingsRowModel
    @ObservedObject var model: AppModel

    var body: some View {
        if row.enabled {
            control.help(row.tooltip)
        } else {
            // The schema requires the explanation visible beside the disabled
            // control and its recovery action usable (SettingsSchema.h's
            // disabledHelp contract) — a tooltip alone hides both. One
            // container: RowView sits in Form sections and the setup
            // assistant, where sibling views would each become a form row
            // of their own.
            VStack(alignment: .leading) {
                control
                    .disabled(true)
                    .help(row.disabledHelp)
                if !row.disabledHelp.isEmpty {
                    Text(row.disabledHelp)
                }
                if !row.disabledAction.isEmpty {
                    Button(row.disabledActionLabel) { model.trigger(row.disabledAction) }
                }
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
            LabeledContent { NumberField(row: row, model: model) } label: { label }
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
                           help: row.enabled ? row.help : "")
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
        } else if row.rowId == "openAiAuth" {
            LabeledContent { CredentialField(model: model) } label: { label }
        } else if row.rowId == "anthropicAuthMode" {
            VStack(alignment: .leading) {
                picker
                if !model.anthropicCredentialStatus.isEmpty {
                    Text(model.anthropicCredentialStatus)
                }
            }
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

    private var releaseNotes: some View {
        VStack(alignment: .leading, spacing: 8) {
            ForEach(Array(Self.text(row.value)
                    .components(separatedBy: "\n\n").enumerated()), id: \.offset) { _, block in
                if block == "---" {
                    Divider()
                } else {
                    let heading = block.hasPrefix("#")
                    let text = block.components(separatedBy: "\n")
                        .map(Self.releaseNoteLine)
                        .joined(separator: "\n")
                    Text(Self.inlineMarkdown(text))
                        .fontWeight(heading ? .bold : nil)
                }
            }
        }
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

    // The gate note in the row body replaces the description while the row is
    // disabled, matching the Qt and Windows front ends; showing both would give
    // a gated row two competing descriptions.
    private var label: some View {
        Self.label(row.label, help: row.enabled ? row.help : "")
    }

    /// The name of a setting and, under it, what it does. Two Texts in a stock
    /// label is how a settings row says that; SwiftUI sizes and colours the
    /// second one, which is why there is no font or colour here. A builder
    /// rather than a view, so the form still sees two Texts.
    @ViewBuilder static func label(_ title: String, help: String) -> some View {
        Text(title)
        if !help.isEmpty {
            Text(help)
        }
    }

    static func flag(_ value: Any?) -> Bool { (value as? NSNumber)?.boolValue ?? false }
    static func number(_ value: Any?) -> Int { (value as? NSNumber)?.intValue ?? 0 }
    static func text(_ value: Any?) -> String { value as? String ?? "" }

    private static func releaseNoteLine(_ line: String) -> String {
        if line.hasPrefix("#") {
            return String(line.drop(while: { $0 == "#" || $0 == " " }))
        }
        if line.hasPrefix("- ") {
            return "• " + String(line.dropFirst(2))
        }
        return line
    }

    private static func inlineMarkdown(_ text: String) -> AttributedString {
        let options = AttributedString.MarkdownParsingOptions(
            interpretedSyntax: .inlineOnlyPreservingWhitespace)
        return (try? AttributedString(markdown: text, options: options))
            ?? AttributedString(text)
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
            TextField("", text: $text, axis: row.multiline ? .vertical : .horizontal)
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
        SecureField("", text: $text)
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
        } else {
            // Cached on the model: resolving it live can enter the keyring,
            // which must not happen inside a SwiftUI body.
            Text(model.credentialStatus)
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
                        ForEach(choices, id: \.columnId) { column in
                            Picker("", selection: choice(index, column.columnId)) {
                                ForEach(column.options, id: \.rowOptionId) { option in
                                    Text(option.label).tag(option.rowOptionId)
                                }
                            }
                            .labelsHidden()
                        }
                        if isCustom(index) {
                            Button("Delete") { delete(index) }
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
        .confirmationDialog("Delete profile",
                            isPresented: Binding(get: { deleting != nil },
                                                 set: { if !$0 { deleting = nil } }),
                            titleVisibility: .visible) {
            Button("Delete", role: .destructive) {
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
