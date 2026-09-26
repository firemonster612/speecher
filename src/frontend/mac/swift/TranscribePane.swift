import AppKit
import AVFoundation
import SwiftUI
import UniformTypeIdentifiers

// Transcribing audio files: pick files and choices, watch the batch run, then
// read, copy or export what it produced. The batch itself is the core's
// FileTranscriptionSession, reached through the bridge.

/// The Transcribe pane's state. Owned by AppModel rather than the view, so a
/// batch keeps its progress while another pane is on screen.
@MainActor
final class TranscriptionModel: ObservableObject {
    enum Stage { case setup, processing, results }

    /// A batch's choices, as the pane's controls edit them.
    struct Options {
        var speech = ""
        var vocabulary = true
        var refiner = "none"
        var cleanup = "none"
        var tone = "none"
        var profile = "other"
        var destination = SpeecherTranscriptDestination.besideInput
        var folder = ""
    }

    struct AudioFile: Identifiable {
        let path: String
        let bytes: Int64
        var id: String { path }
    }

    @Published private(set) var stage = Stage.setup
    @Published private(set) var files: [AudioFile] = []
    @Published var options = Options()
    @Published private(set) var startError = ""
    /// Lengths in milliseconds, from a probe when a file is added and from the
    /// decoder once the batch reads it.
    @Published private(set) var durations: [String: Int64] = [:]

    // The running batch.
    @Published private(set) var batch: [String] = []
    @Published private(set) var current = -1
    @Published private(set) var phase = SpeecherTranscribePhase.reading
    @Published private(set) var fraction = 0.0
    @Published private(set) var peaks: [Float] = []
    @Published private(set) var partial = ""

    // What it produced.
    @Published private(set) var results: [SpeecherTranscriptResult] = []
    /// Whether the batch refines its transcripts: how its progress is shared
    /// out, and whether its results offer Raw.
    @Published private(set) var refinedAvailable = false
    @Published var showRefined = true
    @Published var expanded: Set<String> = []
    @Published private(set) var copied = ""
    @Published private(set) var exportProblem = ""
    /// The result row a retry is running for.
    @Published private(set) var retrying: Int?
    /// The choices the last batch ran with, which its summary describes, and
    /// what the summary calls them, read when it started.
    private var batchOptions = Options()
    private var batchLabels: SpeecherTranscribeBatchLabels?
    private var cancelled = false
    /// When the current file entered its phase, which the open-ended waits
    /// creep from.
    private var phaseStarted = Date()
    /// Once the current file has finished: when, and where its playhead was.
    private var finishedFile: (at: Date, from: Double)?
    /// The session's events that arrived while a finished file's playhead
    /// glides to the end; nil when none is gliding.
    private var heldEvents: [@MainActor (TranscriptionModel) -> Void]?
    private static let finishGlide = 0.5

    /// The step indicator's names, in order.
    let stepLabels: [String]
    let speechProviders: [SpeecherProviderModel]
    let refinementProviders: [SpeecherProviderModel]
    let cleanupStrengths: [RowOptionModel]
    let tones: [RowOptionModel]
    let profiles: [RowOptionModel]
    private let bridge: SpeecherBridge

    /// Audio and video (whose sound track the decoder reads), plus the formats
    /// macOS may not declare a type for.
    private static let audioTypes: [UTType] = [UTType.audiovisualContent]
        + ["flac", "ogg", "oga", "opus", "webm"].compactMap { UTType(filenameExtension: $0) }

    init(bridge: SpeecherBridge) {
        self.bridge = bridge
        speechProviders = bridge.speechProviders
        refinementProviders = bridge.refinementProviders
        cleanupStrengths = bridge.cleanupStrengths
        tones = bridge.writingTones
        profiles = bridge.writingProfiles
        stepLabels = [SpeecherTranscribeStep.configure, .transcribe, .export].map { bridge.stepLabel($0) }
        seedOptions()
        bridge.transcriptionFileStarted = { [weak self] index, path in
            self?.deliver { $0.fileStarted(index, path: path) }
        }
        bridge.transcriptionFileDecoded = { [weak self] index, peaks, durationMs in
            self?.deliver { $0.fileDecoded(index, peaks: peaks, durationMs: durationMs) }
        }
        bridge.transcriptionFileProgress = { [weak self] _, fraction in
            self?.deliver { $0.fileProgressed(fraction) }
        }
        bridge.transcriptionFilePartial = { [weak self] _, text in
            self?.deliver { $0.partial = text }
        }
        bridge.transcriptionFileRefining = { [weak self] _ in
            self?.deliver { $0.enter(.refining) }
        }
        bridge.transcriptionFileFinished = { [weak self] _, result in
            self?.deliver { $0.fileFinished(result) }
        }
        bridge.transcriptionBatchFinished = { [weak self] results, cancelled in
            self?.deliver { $0.batchFinished(results, cancelled: cancelled) }
        }
    }

    /// Where the pane is in its three steps.
    var step: SpeecherTranscribeStep {
        switch stage {
        case .setup: return .configure
        case .processing: return .transcribe
        case .results: return .export
        }
    }

    // MARK: Setup

    /// The user's own choices, as dictation would make them; the pane's edits
    /// are this batch's alone.
    func seedOptions() {
        let seeded = bridge.transcribeOptions(writingProfile: nil)
        var fresh = Options()
        fresh.speech = seeded.speechProviderId
        fresh.vocabulary = seeded.applyVocabulary
        fresh.refiner = refinementProviders.contains { $0.providerId == seeded.refinementProviderId }
            ? seeded.refinementProviderId : "none"
        fresh.cleanup = seeded.cleanupStrength
        fresh.tone = seeded.tone
        fresh.profile = seeded.writingProfile
        fresh.folder = options.folder
        options = fresh
        startError = ""
    }

    /// Picking a profile brings its cleanup and tone, as it does for
    /// dictation; both stay adjustable afterwards.
    var profile: String {
        get { options.profile }
        set {
            let chosen = bridge.transcribeOptions(writingProfile: newValue)
            options.profile = newValue
            options.cleanup = chosen.cleanupStrength
            options.tone = chosen.tone
        }
    }

    /// Choosing "One folder…" with none picked yet asks for one, and stays
    /// where it was if the chooser is cancelled.
    var destination: SpeecherTranscriptDestination {
        get { options.destination }
        set {
            if newValue == .folder, options.folder.isEmpty {
                guard let folder = Self.chooseFolder(title: "Save transcripts in", from: "") else {
                    objectWillChange.send()
                    return
                }
                options.folder = folder
            }
            options.destination = newValue
        }
    }

    var speechSummary: String {
        speechProviders.first { $0.providerId == options.speech }?.summary ?? ""
    }

    var refinementModel: String { bridge.refinementModel(provider: options.refiner) }

    var startCaption: String { files.count > 1 ? "Transcribe \(files.count) files" : "Transcribe" }

    func detail(for file: AudioFile) -> String {
        bridge.audioFileDetail(bytes: file.bytes, durationMs: durations[file.path] ?? -1)
    }

    /// Adds the audio among these paths, once each. A finished batch's results
    /// give way to the setup that the new files are for; during a batch they
    /// wait in the setup list it returns to.
    func add(_ paths: [String]) {
        guard !paths.isEmpty else { return }
        if stage == .results { transcribeMore() }
        var known = Set(files.map(\.path))
        for path in bridge.audioFiles(among: paths) {
            guard known.insert(path).inserted else { continue }
            let size = (try? URL(fileURLWithPath: path).resourceValues(forKeys: [.fileSizeKey]))?.fileSize
            files.append(AudioFile(path: path, bytes: Int64(size ?? 0)))
            probeDuration(path)
        }
    }

    func remove(_ path: String) {
        files.removeAll { $0.path == path }
    }

    func chooseFiles() {
        let panel = NSOpenPanel()
        panel.title = "Choose audio files"
        panel.allowsMultipleSelection = true
        panel.canChooseDirectories = false
        panel.allowedContentTypes = Self.audioTypes
        guard panel.runModal() == .OK else { return }
        add(panel.urls.map(\.path))
    }

    func changeFolder() {
        if let folder = Self.chooseFolder(title: "Save transcripts in", from: options.folder) {
            options.folder = folder
        }
    }

    private func probeDuration(_ path: String) {
        Task { [weak self] in
            let asset = AVURLAsset(url: URL(fileURLWithPath: path))
            guard let duration = try? await asset.load(.duration), duration.isNumeric else { return }
            self?.durations[path] = Int64(duration.seconds * 1000)
        }
    }

    private static func chooseFolder(title: String, from start: String) -> String? {
        let panel = NSOpenPanel()
        panel.title = title
        panel.prompt = "Choose"
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.canCreateDirectories = true
        if !start.isEmpty { panel.directoryURL = URL(fileURLWithPath: start) }
        return panel.runModal() == .OK ? panel.url?.path : nil
    }

    // MARK: Processing

    func start() {
        guard !files.isEmpty else { return }
        batch = files.map(\.path)
        batchOptions = options
        batchLabels = bridge.batchLabels(for: bridged(options))
        refinedAvailable = bridge.refinesTranscripts(bridged(options))
        results = []
        cancelled = false
        current = -1
        startError = ""
        // Before the call: the session announces its first file from inside it.
        stage = .processing
        if let refusal = bridge.startTranscribing(files: batch, options: bridged(options)) {
            startError = refusal
            stage = .setup
        }
    }

    func cancel() { bridge.cancelTranscription() }

    var processingTitle: String { bridge.processingTitle(batch: batch, current: current) }

    var phaseLabel: String { bridge.phaseLabel(phase) }

    /// How far the current file is through all of its work, from 0 to 1, in
    /// the core's spans: reading 0–5%, sending 5–75%, waiting for the final
    /// text 75–80%, refining 80–97%; without refinement sending runs to 90%
    /// and the wait to 97%. The open-ended phases ease toward the top of their
    /// span without reaching it. 1 comes only once the file has finished,
    /// refined and saved.
    // TODO(round2): switch to core overallFileProgress once bridged
    func overallFileProgress(at now: Date) -> Double {
        if finishedFile != nil { return 1 }
        let span: (from: Double, to: Double)
        switch phase {
        case .reading: span = (0, 0.05)
        case .transcribing: span = (0.05, refinedAvailable ? 0.75 : 0.90)
        case .finishing: span = refinedAvailable ? (from: 0.75, to: 0.80) : (from: 0.90, to: 0.97)
        case .refining: span = (0.80, 0.97)
        @unknown default: span = (0, 0)
        }
        let within = phase == .transcribing
            ? min(1, max(0, fraction))
            : 1 - exp(-max(0, now.timeIntervalSince(phaseStarted)) / 4)
        return span.from + (span.to - span.from) * within
    }

    /// Where the loom draws its playhead: the file's progress, gliding from
    /// where it was to the end once the file has finished.
    func playhead(at now: Date) -> Double {
        guard let finished = finishedFile else { return overallFileProgress(at: now) }
        let t = min(1, now.timeIntervalSince(finished.at) / Self.finishGlide)
        return finished.from + (1 - finished.from) * (1 - pow(1 - t, 3))
    }

    /// What the queue says about one file of the batch, and its symbol.
    func queueState(_ index: Int) -> (text: String, symbol: String, waiting: Bool) {
        let state = bridge.queueState(at: index, current: current, finished: results)
        let symbol: String
        switch state {
        case .current: symbol = "play.circle"
        case .done: symbol = "checkmark.circle"
        case .failed: symbol = "exclamationmark.circle"
        default: symbol = "circle"
        }
        return (bridge.queueStateLabel(state, phase: phaseLabel), symbol, state == .waiting)
    }

    /// Runs a session event now, or once a finished file's playhead has
    /// reached the end, so the next file never replaces it mid-glide.
    private func deliver(_ event: @escaping @MainActor (TranscriptionModel) -> Void) {
        if heldEvents != nil {
            heldEvents?.append(event)
        } else {
            event(self)
        }
    }

    private func releaseHeldEvents() {
        guard var events = heldEvents else { return }
        heldEvents = nil
        while !events.isEmpty, heldEvents == nil {
            let event = events.removeFirst()
            event(self)
        }
        // Another file finished among them; the rest wait for its glide.
        if heldEvents != nil { heldEvents = events }
    }

    private func enter(_ next: SpeecherTranscribePhase) {
        phase = next
        phaseStarted = Date()
    }

    private func fileStarted(_ index: Int, path: String) {
        current = index
        currentPath = path
        fraction = 0
        peaks = []
        partial = ""
        finishedFile = nil
        enter(.reading)
    }

    private func fileDecoded(_ index: Int, peaks levels: [NSNumber], durationMs: Int64) {
        peaks = levels.map(\.floatValue)
        durations[currentPath] = durationMs
        enter(.transcribing)
    }

    private func fileProgressed(_ value: Double) {
        fraction = value
        if value >= 1, phase == .transcribing { enter(.finishing) }
    }

    private func fileFinished(_ result: SpeecherTranscriptResult) {
        // A retry's row takes its result from batchFinished, and the results
        // page it runs on has no playhead to wait for.
        guard retrying == nil else { return }
        results.append(result)
        let now = Date()
        let reached = playhead(at: now)
        finishedFile = (at: now, from: reached)
        heldEvents = []
        Task { [weak self] in
            try? await Task.sleep(for: .seconds(Self.finishGlide))
            self?.releaseHeldEvents()
        }
    }

    private func batchFinished(_ finished: [SpeecherTranscriptResult], cancelled: Bool) {
        current = -1
        if let row = retrying {
            if let result = finished.first { results[row] = result }
            retrying = nil
            return
        }
        // A batch cancelled before any file finished has nothing to show.
        guard !finished.isEmpty else {
            stage = .setup
            return
        }
        self.cancelled = cancelled
        results = finished
        showRefined = true
        exportProblem = ""
        expanded = Set(finished.prefix(1).filter { !$0.failed }.map(\.path))
        stage = .results
    }

    /// The file being read, which a retry's index does not locate in batch.
    private var currentPath = ""

    /// Runs one failed file again with the batch's choices; its row takes the
    /// new result.
    func retry(_ index: Int) {
        retrying = index
        if let refusal = bridge.startTranscribing(files: [results[index].path], options: bridged(batchOptions)) {
            retrying = nil
            exportProblem = refusal
        }
    }

    private func bridged(_ options: Options) -> SpeecherTranscribeOptions {
        let bridged = SpeecherTranscribeOptions()
        bridged.speechProviderId = options.speech
        bridged.applyVocabulary = options.vocabulary
        bridged.refinementProviderId = options.refiner
        bridged.cleanupStrength = options.cleanup
        bridged.tone = options.tone
        bridged.writingProfile = options.profile
        bridged.destination = options.destination
        bridged.folder = options.folder
        return bridged
    }

    // MARK: Results

    private var showingRaw: Bool { refinedAvailable && !showRefined }

    func shownText(_ result: SpeecherTranscriptResult) -> String {
        bridge.shownTranscript(result, raw: showingRaw)
    }

    func meta(_ result: SpeecherTranscriptResult) -> String {
        bridge.resultMeta(result, durationMs: durations[result.path] ?? -1, raw: showingRaw)
    }

    var summary: String {
        guard let labels = batchLabels else { return "" }
        return bridge.batchSummary(results: results, batchSize: batch.count, cancelled: cancelled,
                                   durations: durations.mapValues { NSNumber(value: $0) },
                                   options: bridged(batchOptions), labels: labels)
    }

    func expansion(of path: String) -> Binding<Bool> {
        Binding(get: { [weak self] in self?.expanded.contains(path) ?? false },
                set: { [weak self] open in
                    if open { self?.expanded.insert(path) } else { self?.expanded.remove(path) }
                })
    }

    func copy(_ result: SpeecherTranscriptResult) {
        putOnPasteboard(shownText(result))
        flashCopied(result.path)
    }

    func copyAll() {
        putOnPasteboard(bridge.allTranscripts(results, raw: showingRaw))
        flashCopied("all")
    }

    func export(_ result: SpeecherTranscriptResult) {
        let audio = URL(fileURLWithPath: result.path)
        let panel = NSSavePanel()
        panel.title = "Export transcript"
        panel.allowedContentTypes = [.plainText]
        panel.directoryURL = audio.deletingLastPathComponent()
        panel.nameFieldStringValue = audio.deletingPathExtension().lastPathComponent + "-transcribed.txt"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            try (shownText(result) + "\n").write(to: url, atomically: true, encoding: .utf8)
            exportProblem = ""
        } catch {
            exportProblem = "Could not save \(url.path): \(error.localizedDescription)"
        }
    }

    /// Every finished transcript into one folder, numbered rather than
    /// overwriting what is there.
    func exportAll() {
        guard let folder = Self.chooseFolder(title: "Export transcripts to", from: "") else { return }
        exportProblem = results.filter { !$0.failed }
            .compactMap { bridge.saveTranscript(shownText($0), forAudioFile: $0.path, inFolder: folder) }
            .joined(separator: "\n")
    }

    /// Back to setup with fresh choices. The finished files leave the list;
    /// files added while the batch ran, and any a cancel skipped, stay.
    func transcribeMore() {
        let finished = Set(results.map(\.path))
        files.removeAll { finished.contains($0.path) }
        seedOptions()
        stage = .setup
    }

    private func putOnPasteboard(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }

    private func flashCopied(_ key: String) {
        copied = key
        Task { [weak self] in
            try? await Task.sleep(for: .seconds(1.5))
            if self?.copied == key { self?.copied = "" }
        }
    }
}

private func fileName(_ path: String) -> String {
    (path as NSString).lastPathComponent
}

struct TranscribePane: View {
    @ObservedObject var model: TranscriptionModel

    var body: some View {
        VStack(spacing: 0) {
            steps
            switch model.stage {
            case .setup: setup
            case .processing: processing
            case .results: results
            }
        }
        // Settings changed on another pane show up here, as on the Qt page.
        .onAppear { if model.stage == .setup { model.seedOptions() } }
    }

    // MARK: Steps

    /// "1 Configure · 2 Transcribe · 3 Export": the step the pane is on in
    /// bold, the ones behind it checked, the ones ahead dimmed.
    private var steps: some View {
        VStack(spacing: 4) {
            HStack(spacing: 6) {
                ForEach(Array(model.stepLabels.enumerated()), id: \.offset) { index, label in
                    if index > 0 { Text("·").foregroundStyle(.tertiary) }
                    stepItem(label, at: index)
                }
            }
            if model.step == .configure {
                Text("Check these options, then press Transcribe.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
        }
        .frame(maxWidth: .infinity)
        .scenePadding([.top, .horizontal])
    }

    @ViewBuilder private func stepItem(_ label: String, at index: Int) -> some View {
        let current = model.step.rawValue
        if index < current {
            Label(label, systemImage: "checkmark.circle.fill")
                .foregroundStyle(.secondary)
        } else if index == current {
            Label(label, systemImage: "\(index + 1).circle.fill")
                .fontWeight(.semibold)
        } else {
            Label(label, systemImage: "\(index + 1).circle")
                .foregroundStyle(.tertiary)
        }
    }

    // MARK: Setup

    private var setup: some View {
        VStack(spacing: 0) {
            Form {
                Section {
                    ForEach(model.files) { file in
                        LabeledContent {
                            Button { model.remove(file.path) } label: {
                                Image(systemName: "minus.circle")
                            }
                            .buttonStyle(.borderless)
                            .help("Remove")
                        } label: {
                            Text(fileName(file.path))
                            Text(model.detail(for: file))
                        }
                    }
                    Button(model.files.isEmpty ? "Choose audio files…" : "Add more files…") {
                        model.chooseFiles()
                    }
                } header: {
                    Text("Audio files")
                } footer: {
                    if model.files.isEmpty {
                        Text("Drop files here, or choose them · wav, mp3, m4a, flac, ogg")
                    }
                }
                Section("Transcription") {
                    Picker(selection: $model.options.speech) {
                        ForEach(model.speechProviders, id: \.providerId) { provider in
                            Text(provider.label).tag(provider.providerId)
                        }
                    } label: {
                        Text("Model")
                        if !model.speechSummary.isEmpty { Text(model.speechSummary) }
                    }
                    Toggle(isOn: $model.options.vocabulary) {
                        Text("Apply vocabulary")
                        Text("Use your custom vocabulary and corrections on the result")
                    }
                }
                refinement
                output
                if !model.startError.isEmpty {
                    Section {
                        Label(model.startError, systemImage: "exclamationmark.triangle.fill")
                    }
                }
            }
            .formStyle(.grouped)
            actionBar {
                Button(model.startCaption) { model.start() }
                    .keyboardShortcut(.defaultAction)
                    .disabled(model.files.isEmpty)
            }
        }
        .dropDestination(for: URL.self) { urls, _ in
            model.add(urls.map(\.path))
            return true
        }
    }

    @ViewBuilder private var refinement: some View {
        Section("Refinement") {
            Picker(selection: $model.options.refiner) {
                Text("None").tag("none")
                ForEach(model.refinementProviders, id: \.providerId) { provider in
                    Text(provider.label).tag(provider.providerId)
                }
            } label: {
                Text("Provider")
                Text("Clean up the raw transcripts with a language model")
            }
            Group {
                if !model.refinementModel.isEmpty {
                    LabeledContent {
                        Text(model.refinementModel)
                    } label: {
                        Text("Model")
                        Text("Change it in Accounts")
                    }
                }
                Picker(selection: $model.options.cleanup) {
                    options(model.cleanupStrengths)
                } label: {
                    Text("Cleanup")
                    Text("How much the model may rewrite")
                }
                .pickerStyle(.segmented)
                Picker(selection: $model.profile) {
                    options(model.profiles)
                } label: {
                    Text("Writing profile")
                    Text("Sets cleanup and tone; you can still adjust them here")
                }
                Picker(selection: $model.options.tone) {
                    options(model.tones)
                } label: {
                    Text("Tone")
                    Text("Optional override on top of the profile")
                }
            }
            .disabled(model.options.refiner == "none")
        }
    }

    @ViewBuilder private var output: some View {
        Section("Output") {
            Picker(selection: $model.destination) {
                Text("Next to each audio file").tag(SpeecherTranscriptDestination.besideInput)
                Text("One folder…").tag(SpeecherTranscriptDestination.folder)
                Text("Just show them here").tag(SpeecherTranscriptDestination.nowhere)
            } label: {
                Text("Save transcripts")
                Text(model.options.destination == .nowhere
                     ? "Copy or export from the results afterwards"
                     : "Each transcript is saved as ⟨name⟩-transcribed.txt")
            }
            if model.options.destination == .folder {
                LabeledContent {
                    Button("Change…") { model.changeFolder() }
                } label: {
                    Text("Folder")
                    Text((model.options.folder as NSString).abbreviatingWithTildeInPath)
                }
            }
        }
    }

    private func options(_ choices: [RowOptionModel]) -> some View {
        ForEach(choices, id: \.rowOptionId) { choice in
            Text(choice.label).tag(choice.rowOptionId)
        }
    }

    // MARK: Processing

    private var processing: some View {
        VStack(spacing: 0) {
            Form {
                Section {
                    TranscribeLoom(peaks: model.peaks, playhead: model.playhead(at:), text: model.partial)
                    TimelineView(.periodic(from: .now, by: 0.25)) { timeline in
                        LabeledContent(model.phaseLabel) {
                            Text(percent(model.overallFileProgress(at: timeline.date)))
                        }
                    }
                    if model.batch.count > 1 {
                        ForEach(Array(model.batch.enumerated()), id: \.offset) { index, path in
                            let state = model.queueState(index)
                            LabeledContent {
                                Text(state.text)
                            } label: {
                                Label(fileName(path), systemImage: state.symbol)
                            }
                            .foregroundStyle(state.waiting ? HierarchicalShapeStyle.secondary : .primary)
                        }
                    }
                } header: {
                    Text(model.processingTitle)
                }
            }
            .formStyle(.grouped)
            actionBar {
                Button("Cancel") { model.cancel() }
                    .keyboardShortcut(.cancelAction)
            }
        }
    }

    // MARK: Results

    private var results: some View {
        VStack(spacing: 0) {
            Form {
                Section {
                    HStack {
                        if model.refinedAvailable {
                            Picker("Version", selection: $model.showRefined) {
                                Text("Refined").tag(true)
                                Text("Raw").tag(false)
                            }
                            .pickerStyle(.segmented)
                            .labelsHidden()
                            .fixedSize()
                        }
                        Spacer()
                        Button(model.copied == "all" ? "Copied" : "Copy all") { model.copyAll() }
                        Button("Export all…") { model.exportAll() }
                    }
                    Text(model.summary)
                        .foregroundStyle(.secondary)
                    if !model.exportProblem.isEmpty {
                        Label(model.exportProblem, systemImage: "exclamationmark.triangle.fill")
                    }
                } header: {
                    Text(model.results.count > 1 ? "Transcripts" : "Transcript")
                }
                Section {
                    ForEach(Array(model.results.enumerated()), id: \.element.path) { index, item in
                        result(item, at: index)
                    }
                }
            }
            .formStyle(.grouped)
            actionBar {
                Button("Transcribe more files") { model.transcribeMore() }
            }
        }
    }

    private func result(_ result: SpeecherTranscriptResult, at index: Int) -> some View {
        DisclosureGroup(isExpanded: model.expansion(of: result.path)) {
            Text(model.shownText(result))
                .textSelection(.enabled)
                .frame(maxWidth: .infinity, alignment: .leading)
        } label: {
            LabeledContent {
                HStack {
                    if !result.savedPath.isEmpty {
                        Text("Saved")
                            .foregroundStyle(.green)
                            .help(result.savedPath)
                    }
                    if result.failed {
                        Button(model.retrying == index ? "Retrying…" : "Retry") { model.retry(index) }
                            .disabled(model.retrying != nil)
                    } else {
                        Button(model.copied == result.path ? "Copied" : "Copy") { model.copy(result) }
                        Button("Export…") { model.export(result) }
                    }
                }
            } label: {
                Text(fileName(result.path))
                Text(model.meta(result))
                    .foregroundStyle(result.failed ? Color.red : Color.secondary)
                // A transcript that came through with a problem on the way
                // (refinement fell back to the raw text, saving failed).
                if !result.failed && !result.error.isEmpty {
                    Label(result.error, systemImage: "exclamationmark.triangle.fill")
                        .foregroundStyle(.orange)
                }
            }
        }
    }

    private func percent(_ progress: Double) -> String {
        "\(Int((progress * 100).rounded(.down)))%"
    }

    private func actionBar<Content: View>(@ViewBuilder _ content: () -> Content) -> some View {
        HStack {
            Spacer()
            content()
            Spacer()
        }
        .scenePadding([.horizontal, .bottom])
    }
}

/// The file being read, drawn as it is consumed: its waveform across the top,
/// a playhead at the share of audio sent, and behind the playhead bars that
/// shrink away as motes of sound fall toward the words written so far.
struct TranscribeLoom: View {
    let peaks: [Float]
    /// The playhead's share of the file at a moment, which moves between the
    /// session's events while the file waits on its provider.
    let playhead: @MainActor (Date) -> Double
    let text: String
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            // Reduce Motion stills the bars and motes but not the playhead,
            // which is the progress; a slower clock is enough for it.
            TimelineView(.animation(minimumInterval: reduceMotion ? 0.5 : nil)) { timeline in
                Canvas { context, size in
                    draw(in: &context, size: size, progress: playhead(timeline.date),
                         time: timeline.date.timeIntervalSinceReferenceDate)
                }
            }
            .frame(height: 96)
            Text(text.isEmpty ? "The transcript appears here as it is heard." : text)
                .foregroundStyle(text.isEmpty ? HierarchicalShapeStyle.secondary : .primary)
                .lineLimit(4, reservesSpace: true)
                .truncationMode(.head)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("Transcription progress")
        .accessibilityValue("\(Int((playhead(Date()) * 100).rounded(.down))) percent")
    }

    private func draw(in context: inout GraphicsContext, size: CGSize, progress: Double,
                      time: TimeInterval) {
        // A flat line of short bars until the decoder has read the file.
        let levels = peaks.isEmpty ? Array(repeating: Float(0.04), count: 120) : peaks
        let waveHeight = size.height * 0.7
        let middle = waveHeight / 2
        let step = size.width / CGFloat(max(1, levels.count - 1))
        let head = size.width * CGFloat(min(1, max(0, progress)))
        let accent = GraphicsContext.Shading.color(.accentColor)
        let unread = GraphicsContext.Shading.style(HierarchicalShapeStyle.secondary)

        for (index, level) in levels.enumerated() {
            let x = CGFloat(index) * step
            var height = max(2, CGFloat(level) * waveHeight)
            let consumed = x <= head
            if consumed {
                // Sent audio gives up its sound: the bar fades and shrinks.
                let fade = Double(max(0, 1 - (head - x) / 60))
                height *= CGFloat(0.25 + 0.75 * fade)
                context.opacity = 0.25 + 0.75 * fade
            } else {
                if !reduceMotion { height *= CGFloat(0.9 + 0.1 * sin(time * 3.3 + Double(index) * 0.7)) }
                context.opacity = 0.5
            }
            let bar = CGRect(x: x - 0.8, y: middle - height / 2, width: 1.6, height: height)
            context.fill(Path(roundedRect: bar, cornerRadius: 0.8), with: consumed ? accent : unread)
        }

        context.opacity = 1
        let playhead = CGRect(x: head - 1, y: 0, width: 2, height: waveHeight)
        context.fill(Path(playhead),
                     with: .linearGradient(Gradient(colors: [.accentColor.opacity(0), .accentColor,
                                                             .accentColor.opacity(0)]),
                                           startPoint: CGPoint(x: head, y: 0),
                                           endPoint: CGPoint(x: head, y: waveHeight)))

        guard !reduceMotion, !peaks.isEmpty, progress < 1 else { return }
        // Motes leave the playhead and fall to the text below, each on its own
        // phase so the stream never pulses in step.
        for mote in 0..<10 {
            let seed = (Double(mote) * 0.618_034).truncatingRemainder(dividingBy: 1)
            let life = (time * 0.9 + seed).truncatingRemainder(dividingBy: 1)
            let startY = Double(middle) + (seed - 0.5) * Double(waveHeight) * 0.6
            let x = CGFloat(Double(head) + seed * 30 * life + sin(seed * 40 + life * 6) * 5 * (1 - life))
            let y = CGFloat(startY + (Double(size.height) - startY) * life * life)
            let radius = CGFloat(2.4 * (1 - life * 0.5))
            context.opacity = 0.9 * (1 - life)
            context.fill(Path(ellipseIn: CGRect(x: x - radius, y: y - radius,
                                                width: radius * 2, height: radius * 2)),
                         with: accent)
        }
    }
}

/// The Transcribe pane on its own, for audio opened from Finder: the same view
/// over the same model as the settings pane, so a batch shows in either.
@MainActor
final class SpeecherTranscribeWindow {
    private let window: NSWindow

    init(model: TranscriptionModel) {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 560, height: 680),
                          styleMask: [.titled, .closable, .miniaturizable, .resizable],
                          backing: .buffered,
                          defer: false)
        // Kept and reopened, as the settings window is.
        window.isReleasedWhenClosed = false
        window.title = "Transcribe — Speecher"
        let hosting = NSHostingController(rootView: TranscribePane(model: model))
        // The window keeps its size as the pane moves between stages.
        hosting.sizingOptions = []
        window.contentViewController = hosting
        window.setContentSize(NSSize(width: 560, height: 680))
        window.minSize = NSSize(width: 460, height: 480)
        window.center()
        window.setFrameAutosaveName("SpeecherTranscribe")
    }

    func show() {
        window.makeKeyAndOrderFront(nil)
    }

    func capture(toPath path: String) -> Bool {
        window.captureBackingStore(toPath: path)
    }
}
