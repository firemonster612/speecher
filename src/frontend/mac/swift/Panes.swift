import SwiftUI

// The settings window's seven regular panes and contextual What's New page.
// The schema decides which rows a page has and how they group into cards; this
// file only names the panes and the order the sidebar lists them in.

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
    /// The shortcut recorder, which has no schema rows behind it.
    case shortcut
}

struct Pane: Identifiable {
    let id: String
    let title: String
    let symbol: String
    /// The schema page whose sections this pane shows.
    let schemaPages: [String]
    let layout: PaneLayout

    init(_ id: String,
         _ title: String,
         _ symbol: String,
         schemaPages: [String] = [],
         layout: PaneLayout = .sections) {
        self.id = id
        self.title = title
        self.symbol = symbol
        self.schemaPages = schemaPages
        self.layout = layout
    }
}

extension Pane {
    static let all: [Pane] = [
        Pane("dictation", "Dictation", "mic", layout: .shortcut),
        Pane("general", "General", "gearshape", schemaPages: ["general"]),
        Pane("audio", "Audio", "waveform", schemaPages: ["audio"]),
        Pane("output", "Output", "arrow.right.doc.on.clipboard", schemaPages: ["output"]),
        Pane("accounts", "Accounts", "person.badge.key", schemaPages: ["accounts"]),
        Pane("refinement", "Refinement", "wand.and.sparkles", schemaPages: ["refinement"]),
        Pane("vocabulary", "Vocabulary", "character.book.closed", schemaPages: ["vocabulary"]),
        Pane("whatsNew", "What's New", "sparkles", schemaPages: ["whatsNew"]),
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
        case .shortcut:
            ShortcutPane(model: model)
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
