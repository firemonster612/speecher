import AppKit
import SwiftUI

// One stock control per row kind. Nothing here sets a size, a colour, a font or
// a spacing: a grouped form owns all four, and macOS 26 changed all four.

struct RowView: View {
    let row: SettingsRowModel
    @ObservedObject var model: AppModel

    var body: some View {
        control
            .disabled(!row.enabled)
            .help(row.enabled ? row.tooltip : row.disabledHelp)
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
            LabeledContent { TextRowField(row: row, model: model) } label: { label }
        case .info:
            LabeledContent { Text(Self.text(row.value)) } label: { label }
        case .action:
            LabeledContent { Button(row.actionLabel) { model.trigger(row.rowId) } } label: { label }
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
        } else if row.rowId == "openAiAuth" {
            LabeledContent { CredentialField(model: model) } label: { label }
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
/// is the control for text input paired with a list of choices.
struct TextRowField: View {
    let row: SettingsRowModel
    @ObservedObject var model: AppModel
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
            Text(model.bridge.credentialStatus)
        }
    }
}

/// The cleanup strength and optional tone of each writing profile. The profiles
/// are fixed, so this is a run of ordinary settings rows rather than an editable
/// table — one row per profile, each with its pop-up buttons.
struct WritingProfileRows: View {
    let row: SettingsRowModel
    @ObservedObject var model: AppModel

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
