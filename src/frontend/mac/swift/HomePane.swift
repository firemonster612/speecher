import AppKit
import Charts
import SwiftUI
import UniformTypeIdentifiers

// Home: the dictation card and, below it, what the insights log says about
// the chosen period. Every number comes from the core's summary through the
// bridge; this file only words and draws it. See docs/insights-mockup.

/// What the activity heatmap colours its days by.
private enum HeatMeasure: Int, CaseIterable, Identifiable {
    // The bridge's order for the per-measure lists.
    case dictations
    case words
    case audio

    var id: Self { self }

    /// Its homeLabels key.
    var labelKey: String {
        switch self {
        case .dictations: return "measureDictations"
        case .words: return "measureWords"
        case .audio: return "measureAudio"
        }
    }

    func level(_ day: SpeecherInsightsDayModel) -> Int {
        switch self {
        case .dictations: return day.dictationsLevel
        case .words: return day.wordsLevel
        case .audio: return day.audioLevel
        }
    }
}

struct HomePane: View {
    @ObservedObject var model: AppModel
    @State private var measure = HeatMeasure.dictations
    /// What the Share button says for a moment after a choice, or nil.
    @State private var shareReport: String?

    private var insights: SpeecherInsightsModel { model.insights }

    /// Screenshot automation: SPEECHER_GRAB_SCROLL=bottom opens Home scrolled
    /// to its last card, so one grab can show the records and the footer.
    private static let opensAtBottom =
        ProcessInfo.processInfo.environment["SPEECHER_GRAB_SCROLL"] == "bottom"
    private static let bottomID = "homeBottom"

    var body: some View {
        ScrollViewReader { proxy in
            form.onAppear {
                // "Today" may have moved on while the window stayed open.
                model.refreshInsights()
                guard Self.opensAtBottom else { return }
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                    proxy.scrollTo(Self.bottomID, anchor: .bottom)
                }
            }
        }
    }

    private var form: some View {
        Form {
            if model.accessibilitySupported && !model.accessibilityEnabled {
                Section { AccessibilityNotice(model: model) }
            }
            dictationCard
            if !model.insightsEnabled {
                notice(title: model.homeLabel("insightsOffTitle"),
                       text: model.homeLabel("insightsOffBody"),
                       action: model.homeLabel("insightsSettings"))
            } else if insights.recordCount == 0 {
                notice(title: model.homeLabel("noInsightsTitle"), text: model.homeLabel("noInsightsBody"))
            } else {
                tiles
                activity
                whenYouTalk
                pace
                apps
                corrections
                records
            }
        }
        .formStyle(.grouped)
    }

    // MARK: Dictation

    @ViewBuilder private var dictationCard: some View {
        Section {
            LabeledContent {
                // Labelled and enabled by what toggle() would do, as in the
                // menu bar panel.
                Button(model.toggleLabel) {
                    model.bridge.toggle()
                }
                .disabled(!model.toggleEnabled)
            } label: {
                Label(model.status, systemImage: model.listening ? "mic.fill" : "mic")
                Text(model.bridge.dictationShortcutHint(model.shortcut))
            }
            // With no shortcut the hint asks for one, and this is the way there.
            if model.shortcut.isEmpty {
                let openDictation = SpeecherErrorAction(fix: .settingsPage, pageId: "dictation")
                Button(openDictation.label) { model.showPage(openDictation.pageId) }
            }
            // The popup shows a failure only for a while, so the reason, and
            // what fixes it, also stay here until the next session starts.
            if !model.failureNote.isEmpty {
                LabeledContent {
                    if let fix = model.failureFix {
                        Button(fix.label) { model.perform(fix) }
                    }
                } label: {
                    Text(model.failureNote)
                        .textSelection(.enabled)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            if !model.transcript.isEmpty {
                LabeledContent {
                    if model.transcriptCopied {
                        Button(model.bridge.copiedCaption, systemImage: "checkmark") { model.copyTranscript() }
                    } else {
                        Button(model.bridge.copyTranscriptCaption, systemImage: "doc.on.doc") {
                            model.copyTranscript()
                        }
                        .labelStyle(.iconOnly)
                        .help(model.bridge.copyTranscriptCaption)
                    }
                } label: {
                    Text(model.transcript).lineLimit(2)
                    Text(model.transcriptDetail)
                }
            }
        }
    }

    /// The one card that stands in for the insights while there are none to
    /// show.
    private func notice(title: String, text: String, action: String? = nil) -> some View {
        Section {
            LabeledContent {
                if let action {
                    Button(action) { model.showPage("general") }
                }
            } label: {
                Text(title)
                Text(text)
            }
        }
    }

    // MARK: Stat tiles

    /// The four tiles as one grid on the section's own background: four
    /// across when every tile's lines fit unwrapped, two by two when they do
    /// not. Two by two keeps the narrower floor it always had and lets lines
    /// wrap, so Home does not widen the settings window's minimum.
    private var tiles: some View {
        Section {
            ViewThatFits(in: .horizontal) {
                // A spacing's worth of slack, so four across gives way to two
                // by two before a title meets the card's edge.
                tileGrid(perRow: 4).padding(.trailing, 24)
                tileGrid(perRow: 2)
            }
        } header: {
            HStack {
                Text(model.homeLabel("yourDictation"))
                Spacer()
                Picker(model.homeLabel("period"), selection: $model.insightsRange) {
                    ForEach([SpeecherInsightsRange.last7Days, .last30Days, .thisYear, .allTime], id: \.self) {
                        Text(model.bridge.insightsRangeLabel($0)).tag($0)
                    }
                }
                .pickerStyle(.menu)
                .labelsHidden()
                .fixedSize()
                shareMenu
            }
        }
    }

    /// Share: the stats as text on the clipboard, or saved as JSON. The
    /// image the Linux Home also copies is left out here.
    private var shareMenu: some View {
        Menu(shareReport ?? model.homeLabel("share"), systemImage: "square.and.arrow.up") {
            Button(model.homeLabel("copyText"), systemImage: "doc.on.doc") {
                NSPasteboard.general.clearContents()
                NSPasteboard.general.setString(insights.shareText, forType: .string)
                report(model.homeLabel("copied"))
            }
            Divider()
            Button(model.homeLabel("saveJson"), systemImage: "square.and.arrow.down") { saveJson() }
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
    }

    private func report(_ text: String) {
        shareReport = text
        DispatchQueue.main.asyncAfter(deadline: .now() + SpeecherBridge.copiedFeedbackSeconds) {
            shareReport = nil
        }
    }

    private func saveJson() {
        let panel = NSSavePanel()
        panel.title = model.homeLabel("saveTitle")
        panel.nameFieldStringValue = insights.jsonFileName
        panel.allowedContentTypes = [.json]
        panel.directoryURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first
        let json = insights.json
        panel.begin { response in
            guard response == .OK, let url = panel.url else { return }
            do {
                try json.write(to: url, options: .atomic)
                report(model.homeLabel("saved"))
            } catch {
                report(model.homeLabel("saveFailed"))
            }
        }
    }

    private func tileGrid(perRow: Int) -> some View {
        let tiles = insights.tiles.map { tile($0, wraps: perRow != 4) }
        return Grid(alignment: .topLeading, horizontalSpacing: 24, verticalSpacing: 16) {
            ForEach(Array(stride(from: 0, to: tiles.count, by: perRow)), id: \.self) { start in
                GridRow {
                    ForEach(start..<min(start + perRow, tiles.count), id: \.self) { tiles[$0] }
                }
            }
        }
    }

    /// A tile: its name, the figure, and the lines under it, leading-aligned
    /// in one plain stack. Unless it wraps, each line keeps its ideal width,
    /// which is what ViewThatFits then measures; wrapped lines never shrink
    /// below their own width, so none is clipped at its leading edge.
    private func tile(_ text: SpeecherInsightTileModel, wraps: Bool) -> AnyView {
        AnyView(
            VStack(alignment: .leading, spacing: 4) {
                Label(text.title, systemImage: Self.symbol(forIconId: text.iconId))
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .fixedSize(horizontal: !wraps, vertical: false)
                Text(text.unit.isEmpty ? text.value : "\(text.value) \(text.unit)")
                    .font(.title2.weight(.semibold))
                    .monospacedDigit()
                VStack(alignment: .leading, spacing: 2) {
                    ForEach(Array(text.lines.enumerated()), id: \.offset) { index, line in
                        Text(line).help(index == 0 ? text.firstLineTip : "")
                    }
                    if text.showsWeek { weekDots }
                }
                .font(.caption)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: !wraps, vertical: true)
            }
            .frame(minWidth: wraps ? 150 : nil, maxWidth: .infinity, alignment: .leading)
        )
    }

    private static func symbol(forIconId iconId: String) -> String {
        switch iconId {
        case "text": return "text.alignleft"
        case "flame": return "flame"
        case "microphone": return "mic"
        default: return "waveform"
        }
    }

    /// This week, Monday first: a filled dot for a day with dictation, a ring
    /// around today, and bare letters for the days still to come.
    private var weekDots: some View {
        HStack(spacing: 4) {
            ForEach(Array(insights.weekLetters.enumerated()), id: \.offset) { index, letter in
                let active = insights.weekActivity.indices.contains(index)
                    && insights.weekActivity[index].boolValue
                Text(letter)
                    .font(.caption2)
                    .frame(width: 16, height: 16)
                    .background {
                        if index <= insights.todayIndex {
                            Circle().fill(heatColor(active ? 4 : 0, insights.heatStrengths))
                        }
                    }
                    .overlay {
                        if index == insights.todayIndex {
                            Circle().strokeBorder(Color.accentColor)
                        }
                    }
            }
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(insights.weekDescription)
    }

    // MARK: Activity

    private var activity: some View {
        Section {
            ActivityHeatmap(days: insights.heatmap, monthLabels: insights.weekMonthLabels,
                            rowLabels: insights.heatmapRowLabels,
                            strengths: insights.heatStrengths, measure: measure)
                // The days as one sentence, which VoiceOver reads instead of
                // hundreds of unlabelled squares.
                .accessibilityElement(children: .ignore)
                .accessibilityLabel(insights.heatmapDescriptions.indices.contains(measure.rawValue)
                                    ? insights.heatmapDescriptions[measure.rawValue] : "")
        } header: {
            HStack {
                Text(model.homeLabel("activity"))
                Spacer()
                Picker(model.homeLabel("measure"), selection: $measure) {
                    ForEach(HeatMeasure.allCases) { Text(model.homeLabel($0.labelKey)).tag($0) }
                }
                .pickerStyle(.menu)
                .labelsHidden()
                .fixedSize()
            }
        } footer: {
            HStack {
                Text(insights.activeDaysLastYearText)
                Spacer()
                Text(model.homeLabel("legendLess"))
                ForEach(0..<5) { level in
                    RoundedRectangle(cornerRadius: 2)
                        .fill(heatColor(level, insights.heatStrengths))
                        .frame(width: ActivityHeatmap.cell, height: ActivityHeatmap.cell)
                }
                Text(model.homeLabel("legendMore"))
            }
        }
    }

    // MARK: Cards

    /// One card under its own section header, as every other card on Home is.
    private func card<Content: View>(_ title: String,
                                     @ViewBuilder content: () -> Content) -> some View {
        Section {
            VStack(alignment: .leading) {
                content()
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        } header: {
            Text(title)
        }
    }

    private var whenYouTalk: some View {
        card(model.homeLabel("whenYouTalk")) {
            if insights.hasHourData {
                let counts = insights.hourCounts.map(\.intValue)
                let labels = insights.hourLabels
                let peak = insights.peakHour
                Text(insights.personaText).bold()
                Text(insights.peakText).foregroundStyle(.secondary)
                HourChart(counts: counts, labels: labels, tips: insights.hourTips, peak: peak)
                    .accessibilityElement(children: .ignore)
                    .accessibilityLabel(insights.hourChartDescription)
            } else {
                Text(model.homeLabel("noHourData"))
            }
        }
    }

    private var pace: some View {
        card(model.homeLabel("pace")) {
            if insights.dictations == 0 {
                Text(model.homeLabel("noDictationInPeriod"))
            } else {
                let wpm = insights.wordsPerMinute
                let scale = max(wpm, 160)
                HStack(alignment: .top, spacing: 32) {
                    figure("\(wpm.formatted()) wpm", caption: model.homeLabel("speakingPace"))
                    figure(insights.minutesSavedText, caption: model.homeLabel("savedOverTyping"))
                }
                let typing = insights.typingWordsPerMinute
                barGrid {
                    bar(model.homeLabel("youSpeaking"), value: wpm, total: scale, emphasised: true, caption: wpm.formatted())
                    bar(model.homeLabel("typicalTyping"), value: typing, total: scale, emphasised: false,
                        caption: typing.formatted())
                }
                Text(insights.speedupText)
                    .foregroundStyle(.secondary)
            }
        }
    }

    // MARK: Apps and corrections

    private var apps: some View {
        card(model.homeLabel("whereYourWordsGo")) {
            if insights.apps.isEmpty {
                Text(model.homeLabel("noDictationInPeriod"))
            } else {
                let most = insights.apps.map(\.words).max() ?? 1
                barGrid {
                    ForEach(Array(insights.apps.enumerated()), id: \.offset) { index, app in
                        bar(app.name, detail: app.profileLabel, value: app.words, total: most,
                            emphasised: index == 0, caption: app.percent.formatted(.percent))
                    }
                }
            }
        }
    }

    private var corrections: some View {
        card(model.homeLabel("correctionsTitle")) {
            let learned = model.learnedCorrectionCount
            figure(learned.formatted(), caption: model.bridge.learnedCorrectionsCaption(learned))
            Text(model.bridge.learnedCorrectionsNote)
                .foregroundStyle(.secondary)
            let action = model.bridge.learnedCorrectionsAction
            if !action.isEmpty {
                Button(action) { model.showPage("vocabulary:corrections") }
            }
        }
    }

    /// A large figure over its caption, as the Linux and Windows Pace and
    /// learned corrections cards show them.
    private func figure(_ value: String, caption: String) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(value)
                .font(.title2.weight(.semibold))
                .monospacedDigit()
            Text(caption)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }

    /// Name, bar and caption per row in one grid, so the bars line up and
    /// start after the widest name, as on Linux and Windows.
    private func barGrid<Rows: View>(@ViewBuilder rows: () -> Rows) -> some View {
        Grid(alignment: .leading, horizontalSpacing: 12) { rows() }
    }

    /// A labelled bar: the emphasised one in the accent colour, the rest in a
    /// lighter tint of it.
    private func bar(_ title: String, detail: String = "", value: Int, total: Int,
                     emphasised: Bool, caption: String) -> some View {
        GridRow {
            HStack(spacing: 6) {
                // An app name stays on one line; the badge beside it would
                // otherwise squeeze a two-word name onto two. The name takes
                // its width before the bar does, and truncates only when the
                // card is too narrow for it.
                Text(title).lineLimit(1)
                if !detail.isEmpty { ProfileBadge(label: detail).fixedSize() }
            }
            .layoutPriority(1)
            ProgressView(value: Double(value), total: Double(max(total, 1)))
                // The system accent itself, not Color.accentColor: a tint
                // defined in terms of the accent it replaces resolves
                // recursively and overflows the stack.
                .tint(Color(nsColor: .controlAccentColor).opacity(emphasised ? 1 : 0.42))
                // Enough bar to read even beside a long name, which
                // truncates first.
                .frame(minWidth: 60)
                .accessibilityLabel(title)
            Text(caption).monospacedDigit().gridColumnAlignment(.trailing)
        }
    }

    // MARK: Records

    private var records: some View {
        Section {
            ForEach(Array(insights.records.enumerated()), id: \.offset) { _, record in
                LabeledContent {
                    if record.milestoneBar {
                        ProgressView(value: Double(insights.allTimeWords), total: Double(insights.nextMilestone))
                            .frame(width: 120)
                    } else {
                        Text(record.value)
                    }
                } label: {
                    Text(record.title)
                    Text(record.detail)
                }
            }
        } header: {
            Text(model.homeLabel("records"))
        } footer: {
            HStack {
                Label(model.homeLabel("privacyNote"), systemImage: "lock")
                Button(model.homeLabel("insightsSettings")) { model.showPage("general") }
                    .buttonStyle(.link)
            }
            .id(Self.bottomID)
        }
    }

}

/// The last 53 weeks, one column per week with Monday on top, in GitHub's
/// layout. Cells keep their size, so a narrow window shows only the latest
/// weeks that fit whole.
private struct ActivityHeatmap: View {
    let days: [SpeecherInsightsDayModel]
    /// One per week, from the core; the weeks shown take its tail.
    let monthLabels: [String]
    /// Seven, Monday first, from the core; empty rows have no label.
    let rowLabels: [String]
    let strengths: [NSNumber]
    let measure: HeatMeasure
    @State private var width: CGFloat = 0
    /// The day under the pointer, outlined and described at once rather than
    /// after the system's tooltip delay.
    @State private var hovered: Date?

    static let cell: CGFloat = 11
    private static let labelWidth: CGFloat = 28
    private let gap: CGFloat = 3

    private var weeks: [[SpeecherInsightsDayModel]] {
        stride(from: 0, to: days.count, by: 7).map { Array(days[$0..<min($0 + 7, days.count)]) }
    }

    var body: some View {
        // Whole weeks only: the gap follows every column but the last.
        let fitting = Int((width - Self.labelWidth + gap) / (Self.cell + gap))
        let columns = Array(weeks.suffix(max(fitting, 1)))
        let months = Array(monthLabels.suffix(columns.count))
        HStack(alignment: .top, spacing: 0) {
            VStack(alignment: .leading, spacing: gap) {
                label("")
                ForEach(Array(rowLabels.enumerated()), id: \.offset) { _, text in
                    label(text).frame(height: Self.cell)
                }
            }
            .frame(width: Self.labelWidth, alignment: .leading)
            HStack(alignment: .top, spacing: gap) {
                ForEach(columns.indices, id: \.self) { index in
                    VStack(alignment: .leading, spacing: gap) {
                        label(months.indices.contains(index) ? months[index] : "")
                            .fixedSize()
                            .frame(width: Self.cell, alignment: .leading)
                        ForEach(columns[index], id: \.date) { day in
                            RoundedRectangle(cornerRadius: 2)
                                .fill(heatColor(measure.level(day), strengths))
                                .overlay {
                                    if hovered == day.date {
                                        RoundedRectangle(cornerRadius: 2)
                                            .strokeBorder(Color.primary, lineWidth: 1.5)
                                    }
                                }
                                .frame(width: Self.cell, height: Self.cell)
                                // Half the gap on each side is this day's, so
                                // a pointer between two cells still hovers one.
                                .padding(gap / 2)
                                .contentShape(Rectangle())
                                .onHover { inside in
                                    if inside {
                                        hovered = day.date
                                    } else if hovered == day.date {
                                        hovered = nil
                                    }
                                }
                                .popover(isPresented: Binding(
                                    get: { hovered == day.date },
                                    set: { if !$0, hovered == day.date { hovered = nil } }),
                                         arrowEdge: .top) {
                                    Text(tooltip(day))
                                        .font(.caption)
                                        .padding(8)
                                }
                                .padding(-gap / 2)
                        }
                    }
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .onGeometryChange(for: CGFloat.self) { $0.size.width } action: { width = $0 }
    }

    private func label(_ text: String) -> some View {
        Text(text.isEmpty ? " " : text)
            .font(.caption2)
            .foregroundStyle(.secondary)
    }

    private func tooltip(_ day: SpeecherInsightsDayModel) -> String {
        day.tips.indices.contains(measure.rawValue) ? day.tips[measure.rawValue] : ""
    }
}

// MARK: Wording

/// Heat level 0..4: a faint wash of the text colour for no dictation, then the
/// accent at the core's strength for the level.
private func heatColor(_ level: Int, _ strengths: [NSNumber]) -> Color {
    guard level > 0, level < strengths.count else { return Color.primary.opacity(0.08) }
    return Color.accentColor.opacity(strengths[level].doubleValue)
}

/// A Writing Profile as a badge: its label on a capsule of the accent at the
/// heatmap's lightest level, as the Linux and Windows badges are.
private struct ProfileBadge: View {
    let label: String

    var body: some View {
        Text(label)
            .font(.caption)
            .padding(.horizontal, 6)
            .padding(.vertical, 1)
            .background(Capsule().fill(Color(nsColor: .controlAccentColor).opacity(0.3)))
    }
}

/// Dictations by hour. The bar under the pointer takes the full accent, as the
/// peak does, and its count shows above the chart at once.
private struct HourChart: View {
    let counts: [Int]
    let labels: [String]
    /// Each hour's tip from the core, its two lines as one.
    let tips: [String]
    let peak: Int
    @State private var hovered: String?

    var body: some View {
        let hour = hovered.flatMap { labels.firstIndex(of: $0) }
        Chart(0..<24, id: \.self) { index in
            BarMark(x: .value("Hour", labels[index]),
                    y: .value("Dictations", counts.indices.contains(index) ? counts[index] : 0))
                .foregroundStyle(Color.accentColor.opacity(index == peak || index == hour ? 1 : 0.42))
                .annotation(position: .top, alignment: .center) {
                    if index == hour {
                        Text(tips.indices.contains(index)
                             ? tips[index].replacingOccurrences(of: "\n", with: ": ") : "")
                            .font(.caption)
                            .padding(.horizontal, 6)
                            .padding(.vertical, 2)
                            .background(.regularMaterial, in: Capsule())
                            .fixedSize()
                    }
                }
        }
        .chartXSelection(value: $hovered)
        .chartXAxis {
            AxisMarks(values: [0, 6, 12, 18].map { labels[$0] })
        }
        .chartYAxis(.hidden)
        .frame(height: 96)
    }
}
