// The Recordings page's wording: what the Recording singleton carries, in
// words. Core's own messages (a
// refusal, a failed start, echo and write problems) come through as they are.
.pragma library
.import Speecher.Wording 1.0 as Wording

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
    openMicrophone: "Open Microphone",
    cancel: "Cancel",
    changes: "Change for recordings",
    changesHelp: "The speech service recordings try first. Dictation keeps its own.",
    consentQuestion: "Tell everyone on the call that you're recording.",
    consentDetail: "In many places it's the law.",
    transcript: "Transcript, as it's written",
    following: "Following",
    jumpToLatest: "Jump to latest",
    openTranscript: "Open transcript",
    subtitleMicOnly: "Record a call to a transcript file from your microphone, as me.",
    allInHistory: "All recordings in History",
    noRecordings: "Recordings you make show here.",
    sameAsSpeech: "Speech to text",
    noneCanRecord: "none can record",
    nextRecording: "For the next recording.",
    callAudioStopped: "Call audio stopped"
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


// A stream's state, with what it lost or why it stopped.
function streamState(stream) {
    let words = streamStates[stream.state] || stream.state
    if (stream.lostAudioMs > 0) words += ", lost " + Wording.Words.clock(stream.lostAudioMs)
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
// "ChatGPT, Claude and This computer can record calls."
function serviceHelp(can, cannot) {
    const list = names => names.length > 1 ? names.slice(0, -1).join(", ") + " and " + names[names.length - 1] : names[0]
    const yes = can.length ? list(can) + " can record calls." : "No speech service you use can record calls."
    return cannot.length ? yes + " " + list(cannot) + " can't yet." : yes
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

// Stop after silence's help, with what counts as speaking and where that's
// set, which the page makes a link: "… Sound louder than Quiet below, now
// 4%, on Microphone, counts as speaking."
function autoStopHelp(help, threshold) {
    return help + " Sound louder than Quiet below, now " + threshold + ", on Microphone, counts as speaking."
}

// A folder with the home folder as ~.
function shortPath(path, home) {
    return home && path.indexOf(home) === 0 ? "~" + path.slice(home.length) : path
}
