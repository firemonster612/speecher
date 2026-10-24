pragma Singleton
import QtQuick
import Speecher.Launch
import Speecher.Theme

// Mock of a Transcribe files batch (core's FileTranscriptionSession and
// TranscribePresentation): the files, the batch's options, progress through
// them and the results. Starting takes a snapshot of the files and options,
// so the run and its results describe the batch that was started. Start it
// in a state with --transcribe-state=empty|ready|running|finished; ready is
// the default. --transcribe-advance=<seconds> moves the batch on a step that
// often, so a recording can show the steps change.
QtObject {
    id: tx

    // empty, ready (files listed, not started), running, finished.
    property string state: Launch.arg("transcribe-state", "ready")
    readonly property bool running: state === "running"
    readonly property bool finished: state === "finished"

    // Wording (core's transcribeText and friends).
    readonly property var text: ({
        audioFiles: "Audio files",
        transcription: "Transcription",
        refinement: "Refinement",
        output: "Output",
        dropToAdd: "Drop audio or video files here, or choose them",
        dropMore: "Drop more files here",
        mediaHint: "MP3, M4A, WAV, FLAC, OGG, Opus, MP4, MOV and WebM",
        chooseFiles: "Choose audio files…",
        addMore: "Add more files…",
        remove: "Remove",
        service: "Service",
        serviceHelp: "The speech-to-text service that hears the files.",
        vocabulary: "Apply vocabulary",
        vocabularyHelp: "Spell your names and terms the way Vocabulary has them.",
        refiner: "Provider",
        refinerHelp: "Cleans up the raw transcripts with a language model.",
        model: "Model",
        modelHint: "Change it in Refinement settings.",
        cleanup: "Cleanup level",
        cleanupHelp: "How much the model may rewrite.",
        profile: "Writing Profile",
        profileHelp: "Sets cleanup and tone for these files.",
        tone: "Tone",
        toneHelp: "Overrides the profile's tone, if set.",
        needsRefiner: "Choose a provider above to use this.",
        saveTranscripts: "Save transcripts",
        folder: "Folder",
        changeFolder: "Change…",
        noFilesYet: "Add at least one audio file to start.",
        partialName: "Transcript so far",
        partialPlaceholder: "The transcript appears here as it is heard.",
        partialHelp: "Each part appears as soon as it is heard.",
        refined: "Refined",
        raw: "Raw",
        copyAll: "Copy all",
        copy: "Copy",
        exportAll: "Export all",
        export_: "Export",
        saved: "Saved",
        retry: "Retry",
        retrying: "Retrying…",
        transcribeMore: "Transcribe more files",
        showAll: "Show all",
        showLess: "Show less"
    })

    readonly property var steps: [
        { id: "configure", label: "Configure" },
        { id: "transcribe", label: "Transcribe" },
        { id: "export", label: "Export" }
    ]
    // Read from state alone: through running and finished it would pass
    // through 0 for a moment as state moves from one to the other.
    readonly property int step: state === "finished" ? 2 : state === "running" ? 1 : 0

    readonly property real _advanceEvery: Number(Launch.arg("transcribe-advance", "0"))
    property var _advance: Timer {
        interval: Theme.ms(tx._advanceEvery * 1000)
        repeat: true
        running: tx._advanceEvery > 0 && !tx.finished
        onTriggered: tx.running ? tx._finish() : tx.start()
    }

    // Files --------------------------------------------------------------

    readonly property var _sampleFiles: [
        { name: "Weekly product sync.mp4", kind: "video", durationMs: 2472000, bytes: 212400000, seed: 3 },
        { name: "Customer interview, Dana.m4a", kind: "audio", durationMs: 1385000, bytes: 21100000, seed: 8 },
        { name: "Voice memo 21 Oct.wav", kind: "audio", durationMs: 168000, bytes: 29600000, seed: 5 }
    ]
    property var files: state === "empty" ? [] : _sampleFiles

    function addFiles() { files = files.concat(_sampleFiles.filter(f => !files.some(g => g.name === f.name))) }
    function removeFile(index) { files = files.filter((f, i) => i !== index) }

    function durationLabel(ms) {
        const s = Math.round(ms / 1000)
        return s < 60 ? s + " s" : Math.floor(s / 60) + " min " + (s % 60) + " s"
    }
    function sizeLabel(bytes) { return bytes >= 1e9 ? (bytes / 1e9).toFixed(1) + " GB" : (bytes / 1e6).toFixed(1) + " MB" }
    function fileDetail(f) { return durationLabel(f.durationMs) + ", " + sizeLabel(f.bytes) }
    // "67 min of audio"
    function _audioLength(list) {
        const min = Math.round(list.reduce((t, f) => t + f.durationMs, 0) / 60000)
        return (min >= 60 ? Math.floor(min / 60) + " h " + (min % 60) + " min" : min + " min") + " of audio"
    }
    readonly property string startCaption: files.length > 1 ? "Transcribe " + files.length + " files" : "Transcribe"
    // The files list's caption: what will be transcribed, and by whom.
    readonly property string filesCaption: files.length ? Words.count(files.length, "file", "files") + ", " + _audioLength(files) + ". " + Library.pipelineCaption(options.service, options.refiner) + "." : text.noFilesYet

    // Options ------------------------------------------------------------

    // Start from the settings dictation uses, until one is changed here.
    property var options: ({
        service: Settings.values.speechProvider || "claude",
        vocabulary: true,
        refiner: Settings.values.refinementProvider || "none",
        cleanup: "balanced",
        profile: Settings.values.defaultWritingProfile || "other",
        tone: "none",
        destination: "beside",
        folder: "~/Documents/Transcripts"
    })
    function setOption(id, value) {
        const next = Object.assign({}, options)
        next[id] = value
        options = next
    }
    // Picks the folder transcripts are saved in; the mock picks one.
    function chooseFolder() { setOption("folder", "~/Documents/Meetings") }
    readonly property bool refines: options.refiner !== "none"

    // The choices come from the lists dictation uses.
    readonly property var services: Models.speechServices.map(s => ({ id: s.id, label: s.label, icon: s.icon }))
    readonly property var refiners: Library.providerChoices.map(p => ({ id: p.id, label: p.label, icon: p.icon, help: p.help }))
    readonly property string refinerModel: Library.modelLabel(options.refiner)
    readonly property var cleanupLevels: Library.levelOptions
    readonly property var profiles: Library.profiles.map(p => ({ id: p.id, label: p.name, icon: p.icon }))
    readonly property var tones: Library.toneOptions
    readonly property var destinations: [
        { id: "none", label: "Just show them here", icon: "eye" },
        { id: "beside", label: "Next to each audio file", icon: "file-text" },
        { id: "folder", label: "One folder…", icon: "folder" }
    ]
    readonly property string destinationHint: options.destination === "beside" ? "Each transcript is saved as ⟨name⟩-transcribed.txt"
                                            : options.destination === "none" ? "Copy or export from the results afterwards" : ""

    // Running ------------------------------------------------------------

    // What was started: the files and options as they were.
    property var batch: ({ files: [], options: options, pipeline: "" })
    property int current: 0
    property string phase: "Transcribing"
    property real progress: 0
    readonly property var currentFile: running ? batch.files[current] : null
    readonly property string processingCount: "File " + (current + 1) + " of " + batch.files.length
    readonly property string processingTitle: currentFile ? currentFile.name : ""
    function queueState(i) { return i < current ? "done" : i === current ? "current" : "waiting" }
    readonly property var queueLabels: ({ done: "Done", waiting: "Waiting" })

    // Each file plays the same script; once it has all been heard, the next
    // file starts, and after the last the batch finishes.
    readonly property string _script: "Thanks for making the time, Dana. To start, could you walk me through how your team handles "
        + "meeting notes today? Sure. Right now someone volunteers at the start of each call, and honestly it's usually me. "
        + "I type while people talk, so I miss half of what's said, and the notes end up as fragments nobody reads. "
        + "We tried recording everything, but then nobody wants to listen back to an hour of audio to find one decision. "
        + "What would good look like for you? A clean summary with the decisions and owners, ready before the next call starts."
    property string partial: ""
    property var _timer: Timer {
        interval: Theme.ms(140)
        repeat: true
        running: tx.running
        onTriggered: {
            if (tx.partial.length >= tx._script.length) {
                if (tx.current + 1 >= tx.batch.files.length) { tx._finish(); return }
                tx.current += 1
                tx.partial = ""
            }
            const rest = tx._script.slice(tx.partial.length)
            tx.partial = tx._script.slice(0, tx.partial.length + rest.search(/\s|$/) + 1)
            tx.progress = tx.partial.length / tx._script.length
        }
    }

    // Finished -----------------------------------------------------------

    property bool showRaw: false
    property int retrying: -1
    // What each sample file turns into; the voice memo fails until retried.
    readonly property var _transcripts: ({
        "Weekly product sync.mp4": { words: 6214, timings: true, folder: "~/Videos",
          refined: "Launch timing\nThe team agreed to move the public launch to November 12 so the billing migration can finish first. Priya owns the updated timeline and will share it by Friday.\n\nOnboarding\nSam walked through the new onboarding flow. Drop-off at the workspace step fell from 38% to 21% in the beta. The remaining issue is the empty state on the projects page, which still reads like an error.\n\nDecisions\n- Ship the onboarding flow to everyone next Tuesday.\n- Keep the old importer for one more release.\n- Marco to draft the launch email.",
          raw: "okay so um let's start with launch timing I think we're all agreeing that we move it to the twelfth of November right so billing can actually finish first uh Priya can you own the timeline and get it out by Friday yeah I can do that okay cool onboarding Sam do you want to walk us through it yeah so the new flow drop off at the workspace step went from like thirty eight percent to twenty one in the beta which is great the thing that's still weird is the empty state on projects it kind of reads like an error" },
        "Customer interview, Dana.m4a": { words: 3480, timings: true, folder: "~/Recordings",
          refined: "Thanks for making the time, Dana. To start, could you walk me through how your team handles meeting notes today?\n\nSure. Right now someone volunteers at the start of each call, and it's usually me. I type while people talk, so I miss half of what's said, and the notes end up as fragments nobody reads.\n\nWe tried recording everything, but nobody wants to listen back to an hour of audio to find one decision. What we want is a clean summary with the decisions and owners, ready before the next call starts.",
          raw: "thanks for making the time Dana to start could you walk me through how your team handles meeting notes today sure so right now um someone volunteers at the start of each call and honestly it's usually me I type while people talk so I miss like half of what's said and the notes end up as fragments that nobody reads" },
        "Voice memo 21 Oct.wav": { words: 24, timings: false, folder: "~/Recordings", failsFirst: true,
          refined: "Reminder for Thursday: book the room for the planning session, send Priya the agenda the day before, and bring the printed roadmap.",
          raw: "okay reminder for Thursday book the room for the planning session uh send Priya the agenda the day before and bring the printed roadmap" }
    })
    // A file's result under the batch's options: refined only when the batch
    // refines, and a saved path only when it saves.
    function _result(file, failed) {
        const t = _transcripts[file.name]
        const base = { name: file.name, kind: file.kind, durationMs: file.durationMs }
        if (failed) return Object.assign(base, { words: 0, failed: true,
            error: "The connection dropped while sending the audio. Check your network, then retry." })
        const opts = batch.options
        const stem = file.name.replace(/\.[^.]+$/, "") + "-transcribed.txt"
        return Object.assign(base, { words: t.words, timings: t.timings, raw: t.raw,
            refined: opts.refiner !== "none" ? t.refined : "",
            savedPath: opts.destination === "beside" ? t.folder + "/" + stem : opts.destination === "folder" ? opts.folder + "/" + stem : "" })
    }
    property var results: []
    readonly property string resultsTitle: results.filter(r => !r.failed).length === 1 ? "Transcript" : "Transcripts"
    readonly property int failedCount: results.filter(r => r.failed).length
    readonly property bool refined: batch.options.refiner !== "none"
    readonly property string batchSummary: {
        const saving = batch.options.destination === "beside" ? ", and saved next to each audio file"
                     : batch.options.destination === "folder" ? ", and saved in " + batch.options.folder : ""
        return (results.length - failedCount) + " done" + (failedCount ? " and " + failedCount + " failed" : "")
            + ", from " + _audioLength(batch.files) + ". " + batch.pipeline + saving + "."
    }
    readonly property string subtitlesNote: "Subtitles come from the Raw Transcript, since refinement rewrites the words its timings belong to."
    readonly property var exportFormats: [
        { id: "text", label: "Text file…", icon: "file-text" },
        { id: "srt", label: "SRT subtitles…", icon: "captions" },
        { id: "vtt", label: "WebVTT subtitles…", icon: "captions" }
    ]
    function wordsLabel(n) { return Words.count(n, "word", "words") }
    function savedLabel(path) { return "Saved to " + path }

    function start() {
        // The caption too, since the models it names are live settings.
        batch = { files: files.slice(), options: Object.assign({}, options),
                  pipeline: Library.pipelineCaption(options.service, options.refiner) }
        current = 0
        partial = ""
        progress = 0
        state = "running"
    }
    function _finish() {
        results = batch.files.map(f => _result(f, !!_transcripts[f.name].failsFirst))
        state = "finished"
    }
    function cancel() { state = "ready" }
    function reset() { state = "ready"; results = [] }
    // A retry takes a moment, then succeeds.
    function retry(index) { retrying = index; _retryDone.restart() }
    property var _retryDone: Timer {
        interval: Theme.ms(1800)
        onTriggered: {
            tx.results = tx.results.map((r, i) => i === tx.retrying ? tx._result(tx.batch.files[i], false) : r)
            tx.retrying = -1
        }
    }
    // A result's transcript as its card shows it: raw when showRaw is on or
    // the batch didn't refine.
    function transcriptOf(r) { return (showRaw || !r.refined ? r.raw : r.refined) || "" }
    function copy(index) { System.copy(transcriptOf(results[index])) }
    function copyAll() { System.copy(results.filter(r => !r.failed).map(transcriptOf).join("\n\n")) }
    function exportResult(index, format) { return text.saved }
    function exportAll(format) { return text.saved }

    // A preview that starts mid-batch or finished starts from the files.
    Component.onCompleted: {
        if (state === "running") start()
        else if (state === "finished") { start(); _finish() }
    }
}
