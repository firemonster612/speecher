import Charts
import SwiftUI

// Home: the dictation card and, below it, what the insights log says about
// the chosen period. Every number comes from the core's summary through the
// bridge; this file only words and draws it. See docs/insights-mockup.

/// What the activity heatmap colours its days by.
private enum HeatMeasure: String, CaseIterable, Identifiable {
    case dictations = "Dictations"
    case words = "Words"
    case audio = "Minutes of audio"

    var id: Self { self }

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
            dictationCard
            if !model.insightsEnabled {
                notice(title: "Insights are off",
                       text: "Speecher isn't recording new dictation. History you already have "
                           + "stays on this computer until you clear it in Insights settings.",
                       action: "Insights settings…")
            } else if insights.recordCount == 0 {
                notice(title: "No insights yet",
                       text: "Your stats appear here after your next dictation. They're stored "
                           + "only on this computer and never sent to the cloud.")
            } else {
                tiles
                activity
                pair {
                    whenYouTalk
                    pace
                }
                pair {
                    apps
                    corrections
                }
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
                Button(model.stoppable ? "Stop Dictation" : "Start Dictation") {
                    model.bridge.toggle()
                }
                .disabled(model.busy)
            } label: {
                Label(statusLabel, systemImage: model.listening ? "mic.fill" : "mic")
                Text(model.shortcut.isEmpty
                     ? "Set a Global Shortcut to dictate from anywhere."
                     : "Press \(model.shortcut) anywhere to dictate into the app you're using.")
            }
            if !model.transcript.isEmpty {
                LabeledContent {
                    Button("Copy Transcript", systemImage: "doc.on.doc") { model.copyTranscript() }
                        .labelStyle(.iconOnly)
                        .help("Copy transcript")
                } label: {
                    Text(model.transcript).lineLimit(2)
                    Text(model.transcriptDetail)
                }
            }
        }
    }

    private var statusLabel: String {
        if model.listening { return "Listening…" }
        let state = model.status
        return state.isEmpty ? "Idle" : state.prefix(1).uppercased() + state.dropFirst()
    }

    /// The one card that stands in for the insights while there are none to
    /// show.
    private func notice(title: String, text: String, action: String? = nil) -> some View {
        Section {
            LabeledContent {
                if let action {
                    Button(action) { model.pane = "general" }
                }
            } label: {
                Text(title)
                Text(text)
            }
        }
    }

    // MARK: Stat tiles

    /// The four tiles as one grid on the section's own background: four
    /// across when they fit, two by two when they do not.
    private var tiles: some View {
        Section {
            ViewThatFits(in: .horizontal) {
                tileGrid(perRow: 4)
                tileGrid(perRow: 2)
            }
        } header: {
            HStack {
                Text("Your dictation")
                Spacer()
                Picker("Period", selection: $model.insightsRange) {
                    Text("Last 7 days").tag(SpeecherInsightsRange.last7Days)
                    Text("Last 30 days").tag(SpeecherInsightsRange.last30Days)
                    Text("This year").tag(SpeecherInsightsRange.thisYear)
                    Text("All time").tag(SpeecherInsightsRange.allTime)
                }
                .pickerStyle(.menu)
                .labelsHidden()
                .fixedSize()
            }
        }
    }

    private func tileGrid(perRow: Int) -> some View {
        let tiles = [wordsTile, streakTile, dictationsTile, audioTile]
        return Grid(alignment: .topLeading, horizontalSpacing: 24, verticalSpacing: 16) {
            ForEach(Array(stride(from: 0, to: tiles.count, by: perRow)), id: \.self) { start in
                GridRow {
                    ForEach(start..<min(start + perRow, tiles.count), id: \.self) { tiles[$0] }
                }
            }
        }
    }

    private var wordsTile: AnyView {
        tile("Words dictated", symbol: "text.alignleft", value: insights.words.formatted()) {
            Text(insights.bookComparison).help(insights.bookComparisonTip)
            line(insights.wordsDeltaText)
        }
    }

    private var streakTile: AnyView {
        tile("Streak", symbol: "flame", value: plural(insights.currentStreak, "day")) {
            line(insights.streakText)
            weekDots
        }
    }

    private var dictationsTile: AnyView {
        tile("Dictations", symbol: "mic", value: insights.dictations.formatted()) {
            Text(insights.dictations == 0
                 ? "Nothing yet"
                 : "\(insights.dictationsPerActiveDay.formatted(.number.precision(.fractionLength(1)))) a day when you dictate")
            line(insights.dictationsDeltaText)
        }
    }

    private var audioTile: AnyView {
        tile("Audio transcribed", symbol: "waveform", value: insights.audioTotalText) {
            Text(insights.averageDictationText)
        }
    }

    /// A tile: its name, the figure, and the lines under it, leading-aligned
    /// in one plain stack. The lines may wrap but never shrink below their
    /// own width, so none is clipped at its leading edge.
    private func tile<Detail: View>(_ title: String, symbol: String, value: String,
                                    @ViewBuilder detail: () -> Detail) -> AnyView {
        AnyView(
            VStack(alignment: .leading, spacing: 4) {
                Label(title, systemImage: symbol)
                    .foregroundStyle(.secondary)
                Text(value)
                    .font(.title2.weight(.semibold))
                    .monospacedDigit()
                VStack(alignment: .leading, spacing: 2) { detail() }
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .frame(minWidth: 150, maxWidth: .infinity, alignment: .leading)
        )
    }

    /// A tile line the core words, left out when it is empty.
    @ViewBuilder private func line(_ text: String) -> some View {
        if !text.isEmpty { Text(text) }
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
                            Circle().fill(heatColor(active ? 2 : 0, insights.heatStrengths))
                        }
                    }
                    .overlay {
                        if index == insights.todayIndex {
                            Circle().strokeBorder(Color.accentColor)
                        }
                    }
            }
        }
    }

    // MARK: Activity

    private var activity: some View {
        Section {
            ActivityHeatmap(days: insights.heatmap, monthLabels: insights.weekMonthLabels,
                            strengths: insights.heatStrengths, measure: measure)
        } header: {
            HStack {
                Text("Activity")
                Spacer()
                Picker("Measure", selection: $measure) {
                    ForEach(HeatMeasure.allCases) { Text($0.rawValue).tag($0) }
                }
                .pickerStyle(.menu)
                .labelsHidden()
                .fixedSize()
            }
        } footer: {
            HStack {
                Text("\(plural(insights.activeDaysLastYear, "day")) with dictation in the last year")
                Spacer()
                Text("Less")
                ForEach(0..<5) { level in
                    RoundedRectangle(cornerRadius: 2)
                        .fill(heatColor(level, insights.heatStrengths))
                        .frame(width: ActivityHeatmap.cell, height: ActivityHeatmap.cell)
                }
                Text("More")
            }
        }
    }

    // MARK: Card pairs

    /// Two cards side by side in one section, one above the other once the
    /// window is too narrow for both. The section is the only box.
    private func pair<Cards: View>(@ViewBuilder _ cards: () -> Cards) -> some View {
        Section {
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 260), spacing: 24, alignment: .top)],
                      alignment: .leading, spacing: 16) {
                cards()
            }
        }
    }

    private func card<Content: View>(_ title: String,
                                     @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading) {
            Text(title).font(.headline)
            content()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var whenYouTalk: some View {
        card("When you talk") {
            if insights.hasHourData {
                let counts = insights.hourCounts.map(\.intValue)
                let labels = insights.hourLabels
                let peak = insights.peakHour
                let verdict = Text("\(insights.persona).").bold()
                let detail = Text("You dictate most around \(labels[peak]), and \(insights.busiestWeekday)s are your busiest day.")
                    .foregroundStyle(.secondary)
                Text("\(verdict) \(detail)")
                Chart(0..<24, id: \.self) { hour in
                    BarMark(x: .value("Hour", labels[hour]),
                            y: .value("Dictations", counts.indices.contains(hour) ? counts[hour] : 0))
                        .foregroundStyle(Color.accentColor.opacity(hour == peak ? 1 : 0.42))
                }
                .chartXAxis {
                    AxisMarks(values: [0, 6, 12, 18].map { labels[$0] })
                }
                .chartYAxis(.hidden)
                .frame(height: 96)
            } else {
                Text("After a few days of dictation this shows the hours you talk most.")
            }
        }
    }

    private var pace: some View {
        card("Pace") {
            if insights.dictations == 0 {
                Text("No dictation in this period.")
            } else {
                let wpm = insights.wordsPerMinute
                let scale = max(wpm, 160)
                LabeledContent("Your speaking pace", value: "\(wpm) wpm")
                LabeledContent("Saved over typing", value: minutes(insights.minutesSavedVersusTyping))
                bar("You, speaking", value: wpm, total: scale, emphasised: true, caption: "\(wpm)")
                let typing = insights.typingWordsPerMinute
                bar("Typical typing", value: typing, total: scale, emphasised: false, caption: "\(typing)")
                Text(insights.speedupText)
                    .foregroundStyle(.secondary)
            }
        }
    }

    // MARK: Apps and corrections

    private var apps: some View {
        card("Where your words go") {
            if insights.apps.isEmpty {
                Text("No dictation in this period.")
            } else {
                let most = insights.apps.map(\.words).max() ?? 1
                ForEach(Array(insights.apps.enumerated()), id: \.offset) { index, app in
                    bar(app.name, detail: app.profileLabel, value: app.words, total: most,
                        emphasised: index == 0, caption: "\(app.percent)%")
                }
            }
        }
    }

    private var corrections: some View {
        card("Corrections") {
            Text("\(plural(model.learnedCorrectionCount, "correction")) learned")
                .font(.title2.weight(.semibold))
                .monospacedDigit()
            Text("Speecher learned these from edits you made after dictating.")
                .foregroundStyle(.secondary)
            Button("Review Corrections…") { model.showCorrections() }
        }
    }

    /// A labelled bar: the emphasised one in the accent colour, the rest in a
    /// lighter tint of it.
    private func bar(_ title: String, detail: String = "", value: Int, total: Int,
                     emphasised: Bool, caption: String) -> some View {
        LabeledContent {
            HStack {
                ProgressView(value: Double(value), total: Double(max(total, 1)))
                    // The system accent itself, not Color.accentColor: a tint
                    // defined in terms of the accent it replaces resolves
                    // recursively and overflows the stack.
                    .tint(Color(nsColor: .controlAccentColor).opacity(emphasised ? 1 : 0.42))
                Text(caption).monospacedDigit()
            }
        } label: {
            HStack(spacing: 6) {
                // An app name stays on one line; the badge beside it would
                // otherwise squeeze a two-word name onto two.
                Text(title).lineLimit(1).fixedSize()
                if !detail.isEmpty { ProfileBadge(label: detail).fixedSize() }
            }
        }
    }

    // MARK: Records

    private var records: some View {
        Section {
            milestone
            record("Longest streak",
                   insights.bestStreakEndsToday && insights.currentStreak > 0
                       ? "That's the one you're on"
                       : "Ended \(insights.bestStreakEnd)",
                   value: plural(insights.bestStreak, "day"))
            record("Longest dictation",
                   "\(insights.longestWords) words into \(insights.longestApp), \(insights.longestDay)",
                   value: insights.longestDuration)
            record("Busiest day", capitalised(insights.busiestDay),
                   value: plural(insights.busiestDayDictations, "dictation"))
            record("Wordiest day", capitalised(insights.wordiestDay),
                   value: plural(insights.wordiestDayWords, "word"))
            if let first = insights.firstDictation {
                record("First dictation",
                       first.formatted(.dateTime.month(.abbreviated).day().year()),
                       value: insights.firstDictationDaysAgo == 0
                           ? "Today"
                           : "\(plural(insights.firstDictationDaysAgo, "day")) ago")
            }
        } header: {
            Text("Records")
        } footer: {
            HStack {
                Label("Insights are stored only on this computer and are never sent to the cloud.",
                      systemImage: "lock")
                Button("Insights settings") { model.pane = "general" }
                    .buttonStyle(.link)
            }
            .id(Self.bottomID)
        }
    }

    @ViewBuilder private var milestone: some View {
        if insights.nextMilestone > 0 {
            let next = insights.nextMilestone
            LabeledContent {
                ProgressView(value: Double(insights.allTimeWords), total: Double(next))
                    .frame(width: 120)
            } label: {
                Text("Next milestone: \(next.formatted()) words")
                Text(insights.milestoneText)
            }
        } else {
            record("Every milestone passed", insights.milestoneText,
                   value: plural(insights.allTimeWords, "word"))
        }
    }

    private func record(_ title: String, _ detail: String, value: String) -> some View {
        LabeledContent {
            Text(value)
        } label: {
            Text(title)
            Text(detail)
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
    let strengths: [NSNumber]
    let measure: HeatMeasure
    @State private var width: CGFloat = 0

    static let cell: CGFloat = 11
    private static let rowLabels = ["Mon", "", "Wed", "", "Fri", "", ""]
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
                ForEach(Array(Self.rowLabels.enumerated()), id: \.offset) { _, text in
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
                                .frame(width: Self.cell, height: Self.cell)
                                .help(tooltip(day))
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
        let value: String
        if day.dictations == 0 {
            value = "No dictation"
        } else {
            switch measure {
            case .dictations:
                value = "\(plural(day.dictations, "dictation")), \(plural(day.words, "word"))"
            case .words:
                value = "\(plural(day.words, "word")) from \(plural(day.dictations, "dictation"))"
            case .audio:
                value = "\(duration(day.audioMs)) of audio"
            }
        }
        let date = day.date.formatted(.dateTime.weekday(.abbreviated).month(.abbreviated).day().year())
        return "\(value)\n\(date)"
    }
}

// MARK: Wording

/// Heat level 0..4: a faint wash of the text colour for no dictation, then the
/// accent at the core's strength for the level.
private func heatColor(_ level: Int, _ strengths: [NSNumber]) -> Color {
    guard level > 0, level < strengths.count else { return Color.primary.opacity(0.08) }
    return Color.accentColor.opacity(strengths[level].doubleValue)
}

private func plural(_ count: Int, _ noun: String) -> String {
    "\(count.formatted()) \(noun)\(count == 1 ? "" : "s")"
}

private func capitalised(_ text: String) -> String {
    text.prefix(1).uppercased() + text.dropFirst()
}

/// "45 min", "2 h 5 min".
private func minutes(_ total: Int) -> String {
    let hours = total / 60, rest = total % 60
    if hours == 0 { return "\(total) min" }
    return rest == 0 ? "\(hours) h" : "\(hours) h \(rest) min"
}

/// "40s", "12 min", "1 h 5 min".
private func duration(_ milliseconds: Int) -> String {
    let seconds = Double(milliseconds) / 1000
    if seconds < 60 { return "\(Int(seconds.rounded()))s" }
    return minutes(Int((seconds / 60).rounded()))
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
