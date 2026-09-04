import SwiftUI

// The settings window's seven regular panes and contextual What's New page.
// The schema decides which rows a page has and how they group into cards; this
// file only names the handwritten Dictation pane and the sidebar order.

/// One card a pane shows: a schema section's title, footnote and rows. Both
/// rendering and the sidebar's search read a pane as these, so a row cannot be
/// visible on a pane and missing from its search.
struct PaneCard: Identifiable {
    let title: String
    let help: String
    let rows: [SettingsRowModel]

    var id: String { title + rows.map(\.rowId).joined() }
}

/// What a pane draws.
enum PaneLayout {
    /// The schema page's sections, one card each.
    case sections
    /// Live dictation controls and the shortcut recorder, which have no schema
    /// rows behind them.
    case dictation
}

struct Pane: Identifiable {
    let id: String
    /// The schema page whose sections this pane shows.
    let schemaPages: [String]
    let layout: PaneLayout

    init(_ id: String,
         schemaPages: [String] = [],
         layout: PaneLayout = .sections) {
        self.id = id
        self.schemaPages = schemaPages
        self.layout = layout
    }
}

extension Pane {
    static let all: [Pane] = [
        Pane("dictation", layout: .dictation),
        Pane("general", schemaPages: ["general"]),
        Pane("audio", schemaPages: ["audio"]),
        Pane("output", schemaPages: ["output"]),
        Pane("accounts", schemaPages: ["accounts"]),
        Pane("refinement", schemaPages: ["refinement"]),
        Pane("vocabulary", schemaPages: ["vocabulary"]),
        Pane("whatsNew", schemaPages: ["whatsNew"]),
    ]

    /// The sidebar's runs, in order. System Settings' own sidebar separates its
    /// groups with a gap and titles none of them. What's New appears only while
    /// selected, so the seven regular panes need no second level of naming.
    static let sidebarRuns = [
        ["dictation", "general", "audio", "output"],
        ["accounts", "refinement", "vocabulary"],
    ]

    static func with(id: String) -> Pane? {
        all.first { $0.id == id }
    }
}

/// One pane, as a grouped form. No paddings, fonts, widths or backgrounds: a
/// grouped form and the stock controls inside it are what System Settings looks
/// like when nobody dresses them up.
struct PaneView: View {
    let pane: Pane
    @ObservedObject var model: AppModel

    var body: some View {
        switch pane.layout {
        case .dictation:
            DictationPane(model: model)
        case .sections:
            Form {
                ForEach(model.cards(for: pane)) { card($0) }
            }
            .formStyle(.grouped)
        }
    }

    @ViewBuilder private func card(_ card: PaneCard) -> some View {
        if !card.rows.isEmpty {
            Section {
                ForEach(card.rows, id: \.rowId) { row in
                    RowView(row: row, model: model)
                }
            } header: {
                Text(card.title)
            } footer: {
                if !card.help.isEmpty { Text(card.help) }
            }
        }
    }
}
