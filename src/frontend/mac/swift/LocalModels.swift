import SwiftUI

// The Local models pane's list and detail ("localModelBrowser"): the catalog in
// a list, the selected model's facts and buttons beside it. The row's value is
// the model dictation uses, empty while another provider transcribes, and
// writing it switches dictation to that model. Everything else on the pane is
// ordinary schema rows.

struct LocalModelBrowser: View {
    let row: SettingsRowModel
    @ObservedObject var model: AppModel
    /// The person's pick, which sticks through every refresh a download
    /// causes. Until there is one the list opens on the model in use, else on
    /// the suggestion.
    @State private var picked: String?

    private var inUse: String { RowView.text(row.value) }
    private var local: LocalSetupState { model.local }

    private var selection: Binding<String?> {
        Binding(get: { picked ?? (inUse.isEmpty ? local.suggestedModel?.modelId : inUse) },
                set: { picked = $0 })
    }

    var body: some View {
        VStack(alignment: .leading) {
            Text(local.hardwareLine)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            HStack(alignment: .top, spacing: 16) {
                List(local.models, id: \.modelId, selection: selection) { entry in
                    listRow(entry)
                }
                .listStyle(.bordered)
                // Wide enough for the longest name with a "Not recommended"
                // badge, so no row truncates; the detail takes the rest.
                .frame(width: 300, height: CGFloat(local.models.count) * 44 + 8)
                if let id = selection.wrappedValue, let entry = local.model(id) {
                    LocalModelDetail(entry: entry, model: model) {
                        model.setValue(entry.modelId, for: row.rowId)
                    }
                }
            }
        }
    }

    private func listRow(_ entry: LocalModelInfo) -> some View {
        Label {
            VStack(alignment: .leading, spacing: 2) {
                HStack {
                    Text(entry.name)
                    RatingBadge(entry: entry)
                }
                // Size and error rate only, so the list stays narrow; the
                // fit is in the facts.
                Text("\(entry.sizeText) · \(LocalModelText.wer(entry.librispeechWer)) WER")
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
        } icon: {
            Image(systemName: entry.downloaded ? "checkmark.circle" : "arrow.down.circle")
        }
    }
}

/// The selected model: what it is, how it does here, and what can be done
/// with it.
private struct LocalModelDetail: View {
    let entry: LocalModelInfo
    @ObservedObject var model: AppModel
    let use: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            VStack(alignment: .leading, spacing: 2) {
                HStack {
                    Text(entry.name).font(.headline)
                    RatingBadge(entry: entry)
                }
                Text(entry.suggested ? "Suggested for this computer" : entry.fileName)
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
            VStack(alignment: .leading, spacing: 4) {
                ForEach(facts, id: \.name) { fact in
                    HStack(alignment: .firstTextBaseline) {
                        // Every name sits in a column as wide as the widest,
                        // so the values line up. Not a Grid: it measures
                        // wrapping text by its unwrapped height, and the form
                        // row then clips what does not fit.
                        ZStack(alignment: .leading) {
                            ForEach(facts, id: \.name) { Text($0.name).hidden() }
                            Text(fact.name)
                        }
                        .foregroundStyle(.secondary)
                        Text(fact.value).fixedSize(horizontal: false, vertical: true)
                            .help(fact.name == "Word error rate" ? model.local.wordErrorRateSources : "")
                    }
                }
            }
            Divider()
            Text((entry.pros.map { "+ \($0)" } + entry.cons.map { "− \($0)" }).joined(separator: "\n"))
                .font(.callout)
                .fixedSize(horizontal: false, vertical: true)
            if !entry.problem.isEmpty {
                Label(entry.problem, systemImage: "exclamationmark.triangle")
                    .foregroundStyle(.orange)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Divider()
            actions
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .textSelection(.enabled)
    }

    private var facts: [(name: String, value: String)] {
        [("Best for", entry.bestFor),
         ("Download", "\(entry.sizeText) · \(entry.fitLabel)"),
         ("Speed here", entry.speedDetail),
         ("Word error rate", "\(LocalModelText.wer(entry.librispeechWer)) clear speech\n"
            + "\(LocalModelText.wer(entry.fleursWer)) everyday speech"),
         ("Text shows", entry.streams ? "As you speak" : "After you stop"),
         ("Language", "English"),
         ("Licence", entry.licence)]
    }

    @ViewBuilder private var actions: some View {
        HStack {
            if entry.downloading {
                Text(entry.progressText).foregroundStyle(.secondary)
                ProgressView(value: entry.downloadFraction).frame(width: 120)
                Button("Cancel") { model.bridge.cancelLocalModelDownload(entry.modelId) }
            } else if entry.downloaded {
                if entry.inUse {
                    Text("In use").foregroundStyle(.secondary)
                } else {
                    Button("Use This Model", action: use)
                }
                Button("Test Speed") { model.bridge.testLocalModelSpeed(entry.modelId) }
                    .disabled(entry.speedTestRunning)
                Button("Delete") { model.bridge.deleteLocalModel(entry.modelId) }
            } else {
                Button(entry.tooLarge ? "Too Large for This Computer" : "Download \(entry.sizeText)") {
                    model.bridge.downloadLocalModel(entry.modelId)
                }
                .disabled(entry.tooLarge)
            }
        }
    }
}

/// A model's rating as a capsule beside its name, as the Home pane's Writing
/// Profile badges are: Recommended in the accent, Not recommended in red, the
/// rest in grey.
struct RatingBadge: View {
    let entry: LocalModelInfo

    var body: some View {
        Text(entry.ratingLabel)
            .font(.caption)
            .fixedSize()
            .padding(.horizontal, 6)
            .padding(.vertical, 1)
            .background(Capsule().fill(tint.opacity(0.3)))
            .accessibilityLabel(entry.ratingLabel)
    }

    private var tint: Color {
        switch entry.rating {
        case .recommended: Color(nsColor: .controlAccentColor)
        case .notRecommended: Color(nsColor: .systemRed)
        default: Color(nsColor: .systemGray)
        }
    }
}

/// The words the Local models pane and the setup assistant share.
enum LocalModelText {
    /// "2.54%": word error rates to two places, as the catalog quotes them.
    static func wer(_ percent: Double) -> String {
        String(format: "%.2f%%", percent)
    }
}

/// Tables and lists key a model by its catalog id.
extension LocalModelInfo: Identifiable {
    public var id: String { modelId }
}

extension LocalSetupState {
    func model(_ id: String) -> LocalModelInfo? { models.first { $0.modelId == id } }
    var suggestedModel: LocalModelInfo? { models.first(where: \.suggested) }
}
