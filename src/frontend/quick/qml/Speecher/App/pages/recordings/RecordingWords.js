// The Recordings page's wording, the popup pill's and Home's recording row:
// what the Recording singleton carries, in words. Core's own messages (a
// refusal, a failed start, echo and write problems) come through as they are.
.pragma library

var text = {
    name: "Name",
    namePlaceholder: "Weekly sync",
    record: "Record",
    stop: "Stop",
    open: "Open",
    connecting: "Connecting…",
    finishing: "Finishing…",
    saved: "Saved",
    recording: "Recording",
    openSpeech: "Open Speech to text",
    runSetup: "Run setup",
    openMicrophone: "Open Microphone",
    cancel: "Cancel",
    changes: "Change for recordings",
    changesHelp: "The speech service recordings try first. Dictation keeps its own.",
    consentQuestion: "Tell everyone on the call that you're recording.",
    consentDetail: "In many places it's the law.",
    transcript: "Transcript, as it's written",
    following: "Following",
    jumpToLatest: "Jump to latest",
    openFile: "Open file",
    allInHistory: "All recordings in History",
    noRecordings: "Recordings you make show here.",
    sameAsSpeech: "Speech to text",
    noneCanRecord: "none can record",
    nextRecording: "For the next recording.",
    callAudioStopped: "Call audio stopped",
    homeRow: "Recording a call"
}

// The streams' states, as each row says it.
var streamStates = {
    recording: "Recording",
    paused: "Paused while you dictate",
    reconnecting: "Reconnecting…",
    stopped: "Stopped"
}

// "me" and "them" as people say them.
var speakers = { me: "Me", them: "Them" }

// "12:03", or "1:02:03" past an hour.
function clock(ms) {
    const s = Math.floor(ms / 1000)
    const mmss = Math.floor(s / 60) % 60 + ":" + String(s % 60).padStart(2, "0")
    return s < 3600 ? mmss : Math.floor(s / 3600) + ":" + mmss.padStart(5, "0")
}

// A stream's state, with what it lost or why it stopped.
function streamState(stream) {
    let words = streamStates[stream.state] || stream.state
    if (stream.lostAudioMs > 0) words += ", lost " + clock(stream.lostAudioMs)
    if (stream.state === "stopped" && stream.problem) words += ": " + stream.problem
    return words
}

// "12 lines couldn't be written: disk full"
function writeProblem(count, error) {
    return (count === 1 ? "1 line couldn't be written" : count + " lines couldn't be written") + (error ? ": " + error : "")
}

// The line under Record: what the settings below will do.
// "Microphone and call audio, with Claude. Saved as Markdown in ~/Documents/Speecher."
function summary(plan, serviceLabel, home) {
    const sources = plan.sources === "both" ? "Microphone and call audio" : "Microphone only"
    const format = plan.format === "txt" ? "plain text" : "Markdown"
    return sources + ", with " + serviceLabel + ". Saved as " + format + " in " + shortPath(plan.folder, home) + "."
}

// Which services can record calls and which can't, from their labels:
// "ChatGPT and Claude can record calls. Local models and Your server can't
// yet, as they don't send each sentence's text as it ends."
function serviceHelp(can, cannot) {
    const list = names => names.length > 1 ? names.slice(0, -1).join(", ") + " and " + names[names.length - 1] : names[0]
    const yes = can.length ? list(can) + " can record calls." : "No speech service you use can record calls."
    return cannot.length ? yes + " " + list(cannot) + " can't yet, as " + (cannot.length > 1 ? "they don't" : "it doesn't")
                           + " send each sentence's text as it ends." : yes
}

// The line under a running recording's status: what it started with, which
// changes below leave alone. "Heard by ChatGPT. Stops after 30 minutes of
// silence."
function runningSummary(running, serviceLabel) {
    const stop = running.autoStopMinutes > 0
        ? " Stops after " + (running.autoStopMinutes === 60 ? "an hour" : running.autoStopMinutes + " minutes") + " of silence."
        : ""
    return "Heard by " + serviceLabel + "." + stop
}

// A folder with the home folder as ~.
function shortPath(path, home) {
    return home && path.indexOf(home) === 0 ? "~" + path.slice(home.length) : path
}
