import SwiftUI

// The settings window's nine panes. The schema supplies rows and values; which
// pane a row appears on, and under which heading, is this file's decision — see
// .scratch/macos-port/mac-ia.md.

/// One card this file asks a pane for: a heading, a footnote, and the schema
/// rows it names.
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
}

struct Pane: Identifiable {
    let id: String
    let title: String
    let symbol: String
    /// Schema pages whose otherwise-unmapped rows fall back to this pane.
    let schemaPages: [String]
    let layout: PaneLayout
    let groups: [PaneGroup]

    init(_ id: String,
         _ title: String,
         _ symbol: String,
         schemaPages: [String] = [],
         layout: PaneLayout = .sections,
         _ groups: [PaneGroup] = []) {
        self.id = id
        self.title = title
        self.symbol = symbol
        self.schemaPages = schemaPages
        self.layout = layout
        self.groups = groups
    }
}

extension Pane {
    static let all: [Pane] = [
        Pane("general", "General", "gearshape", schemaPages: ["general"], [
            PaneGroup("Appearance", ["themeControl", "pauseMedia", "soundsEnabled", "previewWords"]),
            PaneGroup("System", ["launchAtLogin", "clipboardOutputStatus"]),
            PaneGroup("Maintenance", ["runSetup"]),
            PaneGroup("Updates", ["updateChannel",
                                  "autoCheckUpdates",
                                  "autoInstallUpdates",
                                  "checkForUpdates",
                                  "currentVersion",
                                  "whatsNew"]),
        ]),
        Pane("whatsNew", "What's New", "sparkles", schemaPages: ["whatsNew"]),
        Pane("dictation", "Dictation", "mic", schemaPages: ["audio"], [
            PaneGroup("Transcription", ["speechProvider"]),
            PaneGroup("Microphone", ["audioDevice", "captureMode"]),
            PaneGroup("Timing", ["preRollMs", "postRollMs", "readinessTimeoutMs"]),
            PaneGroup("Silence", ["vadEnabled", "vadThresholdPercent"]),
        ]),
        Pane("shortcut", "Shortcut", "command", layout: .shortcut),
        Pane("text", "Text", "text.cursor", schemaPages: ["refinement"], [
            PaneGroup("Refinement", ["refinementProvider",
                                     "defaultWritingProfile",
                                     "targetContextControl",
                                     "includeScreenshotContext"]),
            PaneGroup("Profile Behavior", ["writingProfileBehavior"]),
        ]),
        Pane("delivery", "Delivery", "arrow.right.doc.on.clipboard", schemaPages: ["output"], [
            PaneGroup("Delivery", ["outputMethod",
                                   "outputFormat",
                                   "completionStatusDuration",
                                   "restoreClipboardAfterTyping"]),
            PaneGroup("Paste Behavior", ["globalPasteRule", "categoryPasteRule_*"]),
            // Only a build that can set up a virtual keyboard has this row, and
            // macOS is not one; an empty group draws nothing.
            PaneGroup("Advanced", ["virtualKeyboard"]),
        ]),
        Pane("apps", "Apps", "square.grid.2x2", schemaPages: ["applications"], layout: .alternatives, [
            PaneGroup("Application Recognition", ["appRecognitionRules"]),
            PaneGroup("App-Specific Paste Rules", ["applicationPasteRules"]),
        ]),
        Pane("vocabulary", "Vocabulary", "character.book.closed",
             schemaPages: ["vocabulary", "corrections", "bindings"], layout: .alternatives, [
            PaneGroup("Terms", ["vocabularyEntries", "vocabularyLimit"]),
            PaneGroup("Corrections", ["correctionLearningControl", "learnedCorrections"]),
            PaneGroup("Replacements", ["bindingRules"]),
        ]),
        Pane("accounts", "Accounts", "person.badge.key", schemaPages: ["providers"], [
            PaneGroup("OpenAI", ["openAiModel",
                                 "openAiEffort",
                                 "openAiFastMode",
                                 "openAiAuthMode",
                                 "openAiCliproxyAccount",
                                 "openAiAuth"]),
            PaneGroup("Anthropic", ["anthropicModel",
                                    "anthropicModelCaution",
                                    "anthropicEffort",
                                    "anthropicFastMode",
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
