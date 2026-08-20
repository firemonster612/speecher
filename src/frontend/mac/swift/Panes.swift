import SwiftUI

// The settings window's eight panes. The schema supplies rows and values; which
// pane a row appears on, and under which heading, is this file's decision — see
// .scratch/macos-port/mac-ia.md.

/// One card of a pane: a heading, a footnote, and the rows it holds.
struct PaneGroup: Identifiable {
    let title: String
    let help: String
    /// Schema row ids, in the order they should read. A pattern ending in `*`
    /// takes every row whose id starts with it.
    let rows: [String]

    var id: String { title + rows.joined() }

    init(_ title: String, _ rows: [String], help: String = "") {
        self.title = title
        self.help = help
        self.rows = rows
    }
}

/// What a pane's groups are to each other.
enum PaneLayout {
    /// Sections of one page, shown together.
    case sections
    /// Views of one idea, one at a time, chosen with a segmented picker.
    case alternatives
    /// The shortcut recorder, which has no schema rows behind it.
    case shortcut
}

struct Pane: Identifiable {
    let id: String
    let title: String
    let symbol: String
    let layout: PaneLayout
    let groups: [PaneGroup]

    init(_ id: String,
         _ title: String,
         _ symbol: String,
         layout: PaneLayout = .sections,
         _ groups: [PaneGroup] = []) {
        self.id = id
        self.title = title
        self.symbol = symbol
        self.layout = layout
        self.groups = groups
    }
}

extension Pane {
    static let all: [Pane] = [
        Pane("general", "General", "gearshape", [
            PaneGroup("Appearance", ["themeControl", "pauseMedia", "soundsEnabled", "previewWords"]),
            PaneGroup("System", ["clipboardOutputStatus"]),
            PaneGroup("Maintenance", ["runSetup", "openReleases"]),
        ]),
        Pane("dictation", "Dictation", "mic", [
            PaneGroup("Transcription", ["speechProvider"]),
            PaneGroup("Microphone", ["audioDevice", "captureMode"]),
            PaneGroup("Timing", ["preRollMs", "postRollMs", "readinessTimeoutMs"]),
            PaneGroup("Silence", ["vadEnabled", "vadThresholdPercent"]),
        ]),
        Pane("shortcut", "Shortcut", "command", layout: .shortcut),
        Pane("text", "Text", "text.cursor", [
            PaneGroup("Refinement", ["refinementProvider",
                                     "defaultWritingProfile",
                                     "targetContextControl",
                                     "includeScreenshotContext"]),
            PaneGroup("Profile Behavior", ["writingProfileBehavior"]),
        ]),
        Pane("delivery", "Delivery", "arrow.right.doc.on.clipboard", [
            PaneGroup("Delivery", ["outputMethod",
                                   "outputFormat",
                                   "completionStatusDuration",
                                   "restoreClipboardAfterTyping"]),
            PaneGroup("Paste Behavior", ["globalPasteRule", "categoryPasteRule_*"]),
            // Only a build that can set up a virtual keyboard has this row, and
            // macOS is not one; an empty group draws nothing.
            PaneGroup("Advanced", ["virtualKeyboard"]),
        ]),
        Pane("apps", "Apps", "square.grid.2x2", [
            PaneGroup("Application Recognition", ["appRecognitionRules"]),
            PaneGroup("App-Specific Paste Rules", ["applicationPasteRules"]),
        ]),
        Pane("vocabulary", "Vocabulary", "character.book.closed", layout: .alternatives, [
            PaneGroup("Terms", ["vocabularyEntries", "vocabularyLimit"]),
            PaneGroup("Corrections", ["correctionLearningControl", "learnedCorrections"]),
            PaneGroup("Replacements", ["bindingRules"]),
        ]),
        Pane("accounts", "Accounts", "person.badge.key", [
            PaneGroup("OpenAI", ["openAiModel",
                                 "openAiEffort",
                                 "openAiAuthMode",
                                 "openAiCliproxyAccount",
                                 "openAiAuth"]),
            PaneGroup("Anthropic", ["anthropicModel",
                                    "anthropicModelCaution",
                                    "anthropicEffort",
                                    "anthropicAuthMode",
                                    "anthropicCliproxyAccount"]),
        ]),
    ]

    /// The sidebar's runs, in order. System Settings' own sidebar separates its
    /// groups with a gap and titles none of them, and eight panes in four runs
    /// need no second level of naming either.
    static let sidebarRuns = [
        ["general"],
        ["dictation", "shortcut", "text"],
        ["delivery", "apps"],
        ["vocabulary", "accounts"],
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
    @State private var alternative = 0

    var body: some View {
        switch pane.layout {
        case .shortcut:
            ShortcutPane(model: model)
        case .sections:
            Form {
                ForEach(pane.groups) { group in
                    card(group)
                }
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
                if pane.groups.indices.contains(alternative) {
                    card(pane.groups[alternative], titled: false)
                }
            }
            .formStyle(.grouped)
        }
    }

    @ViewBuilder private func card(_ group: PaneGroup, titled: Bool = true) -> some View {
        let rows = model.rows(matching: group.rows)
        if !rows.isEmpty {
            Section {
                ForEach(rows, id: \.rowId) { row in
                    RowView(row: row, model: model)
                }
            } header: {
                if titled { Text(group.title) }
            } footer: {
                let note = footnote(group, rows)
                if !note.isEmpty { Text(note) }
            }
        }
    }

    /// What a group says about itself, or failing that what the schema says
    /// under the section these rows came from, or the help of a row that fills
    /// the whole card and so has nowhere else to put it.
    private func footnote(_ group: PaneGroup, _ rows: [SettingsRowModel]) -> String {
        if !group.help.isEmpty { return group.help }
        let ids = Set(rows.map(\.rowId))
        for page in model.pages {
            for section in page.sections where !section.help.isEmpty {
                if section.rows.contains(where: { ids.contains($0.rowId) }) {
                    return section.help
                }
            }
        }
        return rows.first { $0.collection != nil }?.help ?? ""
    }
}
