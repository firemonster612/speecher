import SwiftUI

/// The page the window opens on: what dictation is doing right now, and the
/// four settings worth checking before starting, each with its way there.
struct DictationView: View {
    @ObservedObject var model: SettingsModel
    /// Where a "Change…" button sends the sidebar.
    let select: (String) -> Void

    private static let activeStates = ["starting", "listening"]
    private static let knownStates = [
        "idle", "starting", "listening", "stopping", "refining", "delivering", "error",
    ]

    private var active: Bool { Self.activeStates.contains(model.status.lowercased()) }

    private var statusLabel: String {
        let state = model.status.lowercased()
        return Self.knownStates.contains(state) ? state.capitalized : model.status
    }

    var body: some View {
        Form {
            if model.accessibilitySupported && !model.accessibilityEnabled {
                Section { accessibilityNotice }
            }
            Section {
                LabeledContent("Status") {
                    VStack(alignment: .leading, spacing: 6) {
                        Text(statusLabel)
                        if active {
                            ProgressView(value: Double(min(max(model.level, 0), 1)))
                                .progressViewStyle(.linear)
                                .frame(width: 180)
                        }
                    }
                }
            } header: {
                Button(active ? "Stop Dictation" : "Start Dictation") {
                    model.bridge.toggle()
                }
                .controlSize(.large)
                .buttonStyle(.borderedProminent)
                .frame(maxWidth: .infinity, alignment: .center)
                .padding(.bottom, 8)
            }
            Section("Setup at a glance") {
                summaryRow("Refinement", model.summary.refinement, "refinement")
                summaryRow("Microphone", model.summary.microphone, "audio")
                summaryRow("Output", model.summary.output, "output")
                summaryRow("Theme", model.summary.theme, "general")
            }
        }
        .formStyle(.grouped)
    }

    private func summaryRow(_ label: String, _ value: String, _ pageId: String) -> some View {
        LabeledContent(label) {
            HStack(spacing: 8) {
                Text(value).foregroundStyle(.secondary).lineLimit(1).truncationMode(.tail)
                Button("Change…") { select(pageId) }
            }
        }
    }

    // macOS grants are permanent once given, so "off" is the only state to show.
    private var accessibilityNotice: some View {
        HStack(alignment: .top, spacing: 12) {
            Image(systemName: "exclamationmark.triangle.fill").foregroundStyle(.orange)
            VStack(alignment: .leading, spacing: 4) {
                Text("Accessibility is off, so Speecher can only leave your dictation on "
                     + "the clipboard. Allow Speecher under Privacy & Security, then restart it.")
                if !model.accessibilityProblem.isEmpty {
                    Text(model.accessibilityProblem).font(.caption).foregroundStyle(.red)
                }
            }
            Spacer(minLength: 0)
            Button("Open settings") { model.requestAccessibility() }
        }
    }
}
