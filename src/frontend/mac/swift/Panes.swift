import SwiftUI

// The settings window's panes, exactly as the schema arranges them. The schema
// supplies rows and values, and also which pane a row appears on and how the
// sidebar groups them, so every front end reads one arrangement (see
// settingsPanes() in SettingsSchema.cpp). This file only renders it.

/// One card this file asks a pane for: a heading, a footnote, and the schema
/// rows it names. An Alternatives pane's view is addressed as "pane:view".
struct PaneGroup: Identifiable {
    let view: String
    let title: String
    let help: String
    let rows: [String]

    var id: String { title + rows.joined() }

    init(_ model: SettingsPaneGroupModel) {
        view = model.view
        title = model.title
        help = model.help
        rows = model.rows
    }
}

/// One card a pane actually shows: a group's rows as the schema currently
/// offers them.
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
    /// Transcribing audio files, which has no schema rows behind it either.
    case transcribe
    /// The dictation card and insights, drawn from the insights summary.
    case home

    init(_ layout: SpeecherPaneLayout) {
        switch layout {
        case .alternatives: self = .alternatives
        case .transcribe: self = .transcribe
        case .home: self = .home
        case .sections: self = .sections
        @unknown default: self = .sections
        }
    }
}

struct Pane: Identifiable {
    let id: String
    let title: String
    let symbol: String
    let layout: PaneLayout
    let groups: [PaneGroup]

    init(_ model: SettingsPaneModel) {
        id = model.paneId
        title = model.title
        symbol = Self.symbol(forIconId: model.iconId)
        layout = PaneLayout(model.layout)
        groups = model.groups.map(PaneGroup.init)
    }

    /// A subpage, which is laid out as a pane of sections but has no sidebar
    /// entry and so no symbol.
    init(_ model: SettingsSubpageModel) {
        id = model.subpageId
        title = model.title
        symbol = ""
        layout = .sections
        groups = model.groups.map(PaneGroup.init)
    }

    /// SF Symbols for the schema's platform-neutral icon ids.
    private static func symbol(forIconId iconId: String) -> String {
        switch iconId {
        case "home": return "house"
        case "settings": return "gearshape"
        case "whatsNew": return "sparkles"
        case "microphone": return "mic"
        case "refinement": return "text.cursor"
        case "writingProfiles": return "textformat"
        case "localModels": return "cpu"
        case "transcribe": return "waveform"
        case "output": return "arrow.right.doc.on.clipboard"
        case "vocabulary": return "character.book.closed"
        case "accounts": return "person.badge.key"
        default: return "gearshape"
        }
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
        case .transcribe:
            TranscribePane(model: model.transcription)
        case .home:
            HomePane(model: model)
        case .sections:
            ScrollViewReader { proxy in
                Form {
                    ForEach(model.groupCards(for: pane)) { card($0) }
                }
                .formStyle(.grouped)
                .onChange(of: model.requestedRow, initial: true) { _, row in scroll(to: row, proxy) }
            }
        case .alternatives:
            // The views of one idea are chosen above the form rather than in a
            // card of their own: a box holding only the picker reads as one
            // more setting.
            VStack(spacing: 0) {
                Picker("View", selection: $alternative) {
                    ForEach(Array(pane.groups.enumerated()), id: \.offset) { index, group in
                        Text(group.title).tag(index)
                    }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                .fixedSize()
                .scenePadding([.top, .horizontal])
                ScrollViewReader { proxy in
                    Form {
                        let groups = model.groupCards(for: pane)
                        if groups.indices.contains(alternative) {
                            card(groups[alternative], titled: false)
                        }
                    }
                    .formStyle(.grouped)
                    .onChange(of: model.requestedRow, initial: true) { _, row in scroll(to: row, proxy) }
                }
            }
            // A page id can name one of these views, as Home's link to
            // vocabulary:corrections does.
            .onChange(of: model.requestedView, initial: true) { _, view in
                guard let view,
                      let index = pane.groups.firstIndex(where: { $0.view == view }) else { return }
                alternative = index
                model.requestedView = nil
            }
        }
    }

    /// Brings the row a search or Back led to into view, once the pane has
    /// laid out. Another page's row is left for that page, which may be about
    /// to replace this one, as the pane does a subpage on Back.
    private func scroll(to row: String?, _ proxy: ScrollViewProxy) {
        guard let row, pane.groups.contains(where: { $0.rows.contains(row) }) else { return }
        model.requestedRow = nil
        DispatchQueue.main.async {
            withAnimation { proxy.scrollTo(row, anchor: .top) }
        }
    }

    @ViewBuilder private func card(_ card: PaneCard, titled: Bool = true) -> some View {
        // A fallback list is the whole card, and core words its heading.
        if let list = card.rows.first?.fallbackList {
            FallbackSection(list: list, model: model)
        } else if !card.rows.isEmpty {
            Section {
                ForEach(Array(card.rows.enumerated()), id: \.element.rowId) { index, row in
                    // Rows of a group share one gate, so one note above the
                    // group explains it and every row keeps its own help, as
                    // on Linux and Windows.
                    let grouped = !row.groupId.isEmpty
                    let startsGroup = grouped
                        && (index == 0 || card.rows[index - 1].groupId != row.groupId)
                    if startsGroup, !row.enabled {
                        VStack(alignment: .leading) { GateNote(row: row, model: model) }
                    }
                    RowView(row: row, model: model, gateNote: !grouped)
                        .id(row.rowId)
                }
            } header: {
                if titled { Text(card.title) }
            } footer: {
                if !card.help.isEmpty { Text(card.help) }
            }
        }
    }

}
