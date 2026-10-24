pragma Singleton
import QtQuick
import Speecher.Launch
import Speecher.Theme

// The preview's stand-in for host/QuickTranscription, with the same
// properties and commands: a Transcribe files batch played from a script.
// Start it in a state with --transcribe-state=empty|ready|running|finished;
// ready is the default. --transcribe-advance=<seconds> moves the batch on a
// step that often, so a recording can show the steps change.
QtObject {
    id: tx

    readonly property string _start: Launch.arg("transcribe-state", "ready")
    property int step: 0

    readonly property real _advanceEvery: Number(Launch.arg("transcribe-advance", "0"))
    property var _advance: Timer {
        interval: Theme.ms(tx._advanceEvery * 1000)
        repeat: true
        running: tx._advanceEvery > 0 && tx.step < 2
        onTriggered: tx.step === 1 ? tx._finish() : tx.start()
    }

    // Files --------------------------------------------------------------

    readonly property var _sampleFiles: [
        { path: "/home/sam/Videos/Weekly product sync.mp4", name: "Weekly product sync.mp4", kind: "video", durationMs: 2472000, bytes: 212400000 },
        { path: "/home/sam/Recordings/Customer interview, Dana.m4a", name: "Customer interview, Dana.m4a", kind: "audio", durationMs: 1385000, bytes: 21100000 },
        { path: "/home/sam/Recordings/Voice memo 21 Oct.wav", name: "Voice memo 21 Oct.wav", kind: "audio", durationMs: 168000, bytes: 29600000 }
    ]
    property var files: _start === "empty" ? [] : _sampleFiles

    function chooseFiles(title, filterName) { files = files.concat(_sampleFiles.filter(f => !files.some(g => g.path === f.path))) }
    function addUrls(urls) { chooseFiles() }
    function removeFile(path) { files = files.filter(f => f.path !== path) }

    // Options ------------------------------------------------------------

    property var options: ({
        service: Settings.values.speechProvider || "claude",
        vocabulary: true,
        refiner: Settings.values.refinementProvider || "none",
        cleanup: "balanced",
        profile: Settings.values.defaultWritingProfile || "other",
        tone: "none",
        language: Settings.values.spokenLanguage || "en",
        destination: "beside",
        folder: "/home/sam/Documents/Transcripts"
    })
    function setOption(id, value) {
        const next = Object.assign({}, options)
        next[id] = value
        options = next
    }
    function chooseFolder(title) { setOption("folder", "/home/sam/Documents/Meetings") }
    readonly property string refinerModel: Library.modelLabel(options.refiner)

    // Running ------------------------------------------------------------

    property var batch: ({ files: [], speech: "", refinement: "", refines: false, destination: "beside", folder: "", cancelled: false })
    property int current: -1
    property string phase: "transcribing"
    property real progress: 0
    // A steady made-up shape per file: speech-like bursts with pauses.
    readonly property var peaks: {
        const out = []
        for (let i = 0; i < 240; ++i)
            out.push(Math.max(0.05, Math.abs(Math.sin(i / 7 + current) * Math.cos(i / 23 + current * 2))))
        return out
    }
    readonly property var queue: batch.files.map((f, i) => i < current ? "done" : i === current ? "current" : "waiting")

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
        running: tx.step === 1
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
    property string problem: ""
    signal exported(int index)
    // What each sample file turns into; the voice memo fails until retried.
    readonly property var _transcripts: ({
        "Weekly product sync.mp4": { timings: true,
          refined: "Launch timing\nThe team agreed to move the public launch to November 12 so the billing migration can finish first. Priya owns the updated timeline and will share it by Friday.\n\nOnboarding\nSam walked through the new onboarding flow. Drop-off at the workspace step fell from 38% to 21% in the beta. The remaining issue is the empty state on the projects page, which still reads like an error.\n\nDecisions\n- Ship the onboarding flow to everyone next Tuesday.\n- Keep the old importer for one more release.\n- Marco to draft the launch email.",
          raw: "okay so um let's start with launch timing I think we're all agreeing that we move it to the twelfth of November right so billing can actually finish first uh Priya can you own the timeline and get it out by Friday yeah I can do that okay cool onboarding Sam do you want to walk us through it yeah so the new flow drop off at the workspace step went from like thirty eight percent to twenty one in the beta which is great the thing that's still weird is the empty state on projects it kind of reads like an error" },
        "Customer interview, Dana.m4a": { timings: true,
          refined: "Thanks for making the time, Dana. To start, could you walk me through how your team handles meeting notes today?\n\nSure. Right now someone volunteers at the start of each call, and it's usually me. I type while people talk, so I miss half of what's said, and the notes end up as fragments nobody reads.\n\nWe tried recording everything, but nobody wants to listen back to an hour of audio to find one decision. What we want is a clean summary with the decisions and owners, ready before the next call starts.",
          raw: "thanks for making the time Dana to start could you walk me through how your team handles meeting notes today sure so right now um someone volunteers at the start of each call and honestly it's usually me I type while people talk so I miss like half of what's said and the notes end up as fragments that nobody reads" },
        "Voice memo 21 Oct.wav": { timings: false, failsFirst: true,
          refined: "Reminder for Thursday: book the room for the planning session, send Priya the agenda the day before, and bring the printed roadmap.",
          raw: "okay reminder for Thursday book the room for the planning session uh send Priya the agenda the day before and bring the printed roadmap" }
    })
    property var _finished: []
    // A file's result as the host maps it: the shown text follows showRaw.
    function _result(entry) {
        const file = entry.file
        if (entry.failed) return Object.assign({}, file, { failed: true, words: 0, text: "", timings: false, savedPath: "",
            error: "The connection dropped while sending the audio. Check your network, then retry." })
        const t = _transcripts[file.name]
        const text = showRaw || !batch.refines ? t.raw : t.refined
        const folder = batch.destination === "folder" ? batch.folder : file.path.replace(/\/[^/]*$/, "")
        return Object.assign({}, file, { failed: false, error: "", text: text, words: text.split(/\s+/).filter(Boolean).length,
            timings: t.timings, savedPath: batch.destination === "none" ? "" : folder + "/" + file.name.replace(/\.[^.]+$/, "") + "-transcribed.txt" })
    }
    readonly property var results: _finished.map(_result)

    function start() {
        batch = { files: files.slice(), speech: Models.service(options.service).label,
                  refinement: options.refiner !== "none" ? (Library.modelLabel(options.refiner) || Library.provider(options.refiner).label) : "",
                  refines: options.refiner !== "none", destination: options.destination, folder: options.folder, cancelled: false }
        current = 0
        partial = ""
        progress = 0
        _finished = []
        problem = ""
        step = 1
    }
    function _finish() {
        _finished = batch.files.map(f => ({ file: f, failed: !!_transcripts[f.name].failsFirst }))
        current = -1
        step = 2
    }
    function cancel() { step = 0 }
    function reset() { step = 0; _finished = []; showRaw = false }
    // A retry takes a moment, then succeeds.
    function retry(index) { retrying = index; _retryDone.restart() }
    property var _retryDone: Timer {
        interval: Theme.ms(1800)
        onTriggered: {
            tx._finished = tx._finished.map((e, i) => i === tx.retrying ? { file: e.file, failed: false } : e)
            tx.retrying = -1
        }
    }
    function copy(index) { System.copy(results[index].text) }
    function copyAll() { System.copy(results.filter(r => !r.failed).map(r => r.text).join("\n\n\n")) }
    function exportResult(index, format, title, fileType) { exported(index) }
    function exportAll(format, title) { exported(-1) }

    // A preview that starts mid-batch or finished starts from the files.
    Component.onCompleted: {
        if (_start === "running") start()
        else if (_start === "finished") { start(); _finish() }
    }
}
