import SwiftUI

// The settings window's panes, exactly as the schema arranges them. The schema
// supplies rows and values, and also which pane a row appears on and how the
// sidebar runs group, so macOS and Windows read one arrangement (see
// settingsPanes() in SettingsSchema.cpp). This file only renders it.

/// One card this file asks a pane for: a heading, a footnote, and the schema
/// rows it names. A row pattern ending in `*` takes every row whose id starts
/// with it.
struct PaneGroup: Identifiable {
    let title: String
    let help: String
    let rows: [String]

    var id: String { title + rows.joined() }

    init(_ model: SettingsPaneGroupModel) {
        title = model.title
        help = model.help
        rows = model.rows
    }
}

/// One card a pane actually shows: a group's rows, or a schema section no group
/// claimed. Both rendering and the sidebar's search read a pane as these, so a
/// row cannot be visible on a pane and missing from its search.
struct PaneCard: Identifiable {
    let title: String
    let help: String
    let rows: [SettingsRowModel]

    var id: String { title + rows.map(\.rowId).joined() }
}

/// What a pane's groups are to each other.
enum PaneLayout {
    /// Sections of one page, shown together.
    case sections
    /// Views of one idea, one at a time, chosen with a segmented picker.
    case alternatives
    /// The shortcut recorder, which has no schema rows behind it.
    case shortcut

    init(_ layout: SpeecherPaneLayout) {
        switch layout {
        case .alternatives: self = .alternatives
        case .shortcut: self = .shortcut
        case .sections: self = .sections
        @unknown default: self = .sections
        }
    }
}

struct Pane: Identifiable {
    let id: String
    let title: String
    let symbol: String
    /// Schema pages whose otherwise-unmapped rows fall back to this pane.
    let schemaPages: [String]
    let layout: PaneLayout
    let groups: [PaneGroup]

    init(_ model: SettingsPaneModel) {
        id = model.paneId
        title = model.title
        symbol = model.symbolName
        schemaPages = model.schemaPages
        layout = PaneLayout(model.layout)
        groups = model.groups.map(PaneGroup.init)
    }
}

/// One pane, as a grouped form. No paddings, fonts, widths or backgrounds: a
/// grouped form and the stock controls inside it are what System Settings looks
/// like when nobody dresses them up.
struct PaneView: View {
    let pane: Pane
    @ObservedObject var model: AppModel
    @State private var alternative = 0

    var body: some View {
        switch pane.layout {
        case .shortcut:
            ShortcutPane(model: model)
        case .sections:
            Form {
                ForEach(model.groupCards(for: pane)) { card($0) }
                unclaimedCards
            }
            .formStyle(.grouped)
        case .alternatives:
            Form {
                Section {
                    Picker("View", selection: $alternative) {
                        ForEach(Array(pane.groups.enumerated()), id: \.offset) { index, group in
                            Text(group.title).tag(index)
                        }
                    }
                    .pickerStyle(.segmented)
                    .labelsHidden()
                }
                let groups = model.groupCards(for: pane)
                if groups.indices.contains(alternative) {
                    card(groups[alternative], titled: false)
                }
                unclaimedCards
            }
            .formStyle(.grouped)
        }
    }

    @ViewBuilder private func card(_ card: PaneCard, titled: Bool = true) -> some View {
        if !card.rows.isEmpty {
            Section {
                ForEach(card.rows, id: \.rowId) { row in
                    RowView(row: row, model: model)
                }
            } header: {
                if titled { Text(card.title) }
            } footer: {
                if !card.help.isEmpty { Text(card.help) }
            }
        }
    }

    @ViewBuilder private var unclaimedCards: some View {
        ForEach(model.unclaimedCards(for: pane)) { card($0) }
    }
}
