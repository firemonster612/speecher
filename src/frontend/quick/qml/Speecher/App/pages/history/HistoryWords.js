// The History page's wording: its states, filters and actions, and the lines
// it builds from what History carries. Dates come as Dates.
.pragma library
.import QtQml 2.15 as QtQml

var text = {
    clear: "Clear…",
    clearRanges: [
        { id: "hour", label: "Last hour" },
        { id: "today", label: "Today" },
        { id: "7d", label: "Last 7 days" },
        { id: "all", label: "All dictations and files" }
    ],
    clearDetail: "Their text and audio are deleted from this computer. Audio files you transcribed and their saved transcripts stay where they are, and so do recordings. Home keeps counting them unless you check below.",
    alsoStats: "Also delete their stats",
    clearConfirm: "Delete",
    nothingToClear: "Nothing to clear",

    search: "Search what you said, an app or a file…",
    clearSearch: "Clear search",
    kinds: [
        { id: "all", label: "All" },
        { id: "dictation", label: "Dictations" },
        { id: "file", label: "Files" },
        { id: "recording", label: "Recordings" }
    ],
    anyApp: "Any app",
    anyProfile: "Any profile",
    whens: [
        { id: "any", label: "Any time" },
        { id: "today", label: "Today" },
        { id: "yesterday", label: "Yesterday" },
        { id: "7d", label: "Last 7 days" },
        { id: "30d", label: "Last 30 days" },
        { id: "year", label: "This year" }
    ],
    clearFilters: "Clear filters",

    offTitle: "History is off",
    offText: "Speecher keeps nothing about your dictations or files. Recordings you start still keep their transcripts here.",
    statsTitle: "Speecher keeps stats, not words",
    statsText: "Home counts your dictations. Keep their text to find and copy them here.",
    keepText: "Keep text",
    keepTextQuestion: "Keep what you dictate?",
    keepTextConfirm: "Keep text",
    settings: "Privacy & data",
    change: "Change in Privacy & data",
    emptyTitle: "Nothing here yet",
    emptyText: "What you dictate, transcribe or record shows here.",

    refined: "Refined",
    spoken: "As spoken",
    copy: "Copy",
    copied: "Copied",
    pasteAgain: "Paste again",
    play: "Play",
    stop: "Stop",
    delete_: "Delete",
    more: "More",
    file: "File",

    recording: "Recording",
    open: "Open",
    refineAgain: "Refine again",
    refining: "Refining…",
    keepThis: "Keep this",
    refineFailed: "Couldn't refine it again.",
    openTranscript: "Open transcript",
    transcribeAgain: "Transcribe again",
    locate: "Locate…",
    locateDialog: "Locate the audio file",
    findInTranscript: "Find in this transcript…",
    previous: "Previous match",
    next: "Next match",
    playFromHere: "Play from here",
    following: "Following",
    jumpToLatest: "Jump to latest",
    export_: "Export",
    exportFormats: [
        { id: "md", label: "Markdown" },
        { id: "txt", label: "Plain text" },
        { id: "json", label: "JSON" }
    ],
    exportDialog: "Export recording",
    exported: "Exported",
    showInFolder: "Show in folder",
    back: "All of History",
    fileMissing: "The saved transcript was moved or deleted. History still has its words.",
    fileDiverges: "The transcript file was changed into something other than its lines, so History shows them as they were recorded.",
    deleteRecording: "Delete this recording?",
    deleteRecordingDetail: "Its transcript file stays unless you check below.",
    alsoDeleteFile: "Also delete the transcript file and its audio",
    tryAgain: "Try again",
    dismiss: "Dismiss",
    removeOnly: "Remove from History only"
}

// The line under a file's actions, saying what Delete leaves, by its name:
// "Deleting it from History leaves interview-03.wav and its saved
// transcript where they are."
function fileStays(r) {
    return "Deleting it from History leaves " + fileName(r) + (r.transcriptPath ? " and its saved transcript" : "")
        + " where " + (r.transcriptPath ? "they are." : "it is.")
}

// What a file's Delete says once it's done, with Undo.
function fileDeleted(r) {
    return "Deleted from History. " + fileName(r) + (r.transcriptPath ? " and its saved transcript stay where they are." : " stays where it is.")
}

// Why a file can't be transcribed again: "interview-03.wav isn't in
// ~/Calls any more. Locate it to transcribe it again."
function sourceMissing(r, home) {
    const at = folderOf(r.sourcePath)
    return fileName(r) + (at ? " isn't in " + shortPath(at, home) + " any more." : " was moved or deleted.")
        + " Locate it to transcribe it again."
}

// A file entry's name: its title, else the last part of its path.
function fileName(r) { return r.title || (r.sourcePath || "").split(/[\\/]/).pop() || text.file }

// The folder a path is in; empty for none.
function folderOf(path) {
    const at = Math.max((path || "").lastIndexOf("/"), (path || "").lastIndexOf("\\"))
    return at > 0 ? path.slice(0, at) : ""
}

// A folder with the home folder as ~.
function shortPath(path, home) {
    return home && path.indexOf(home) === 0 ? "~" + path.slice(home.length) : path
}

// What a clear that couldn't finish says, as a deleted Recording's files
// say it: what wouldn't go, what stayed, and what went.
// "Speecher couldn't delete 1402.m4a, so 1 dictation or file stays as it
// was. The other 21 were cleared."
function clearStayed(result) {
    // A clear that failed with no text involved, such as on stats alone.
    if (result.left === 0) return "Speecher couldn't finish clearing History."
    const names = result.files.map(f => f.split(/[\\/]/).pop())
    const shown = names.length > 3 ? names.slice(0, 3).join(", ") + " and " + (names.length - 3) + " more" : _list(names)
    const stays = result.left === 1 ? "1 dictation or file stays as it was." : _num(result.left) + " dictations and files stay as they were."
    const went = result.cleared > 0 ? (result.cleared === 1 ? " The other one was cleared." : " The other " + _num(result.cleared) + " were cleared.") : ""
    return (names.length ? "Speecher couldn't delete " + shown + ", so " : "Speecher couldn't clear them, so ") + stays + went
}

// Why a Recording's files didn't go, and that it stays until they do.
function filesStayed(files) {
    const names = files.map(f => f.split(/[\\/]/).pop())
    return "Speecher couldn't delete " + _list(names) + ". The recording stays in History until "
        + (files.length > 1 ? "they're" : "it's") + " gone, or you remove it from History only."
}

// What a finished Recording missed and how it ended, a sentence each, the
// worst first: "Them stopped at 00:12:31: Connection reset.", "2 sentences
// weren't transcribed.", "Stopped after 30 minutes of silence."
function endingNotes(ending) {
    if (!ending || !ending.reason) return []
    const notes = []
    const speaker = s => s === "me" ? "Your side" : s === "them" ? "The call audio" : s
    for (const stop of ending.streamStops || [])
        notes.push(speaker(stop.speaker) + " stopped at " + lineClock(stop.atMs) + (stop.problem ? ": " + stop.problem.replace(/\.$/, "") : "") + ".")
    if (ending.lostAudioMs > 0) notes.push(duration(ending.lostAudioMs) + " of audio was lost while a side reconnected.")
    if (ending.missedUtterances > 0)
        notes.push(ending.missedUtterances === 1 ? "1 sentence wasn't transcribed." : ending.missedUtterances + " sentences weren't transcribed.")
    if (ending.unwrittenLines > 0)
        notes.push(ending.unwrittenLines === 1 ? "1 line couldn't be written to the file." : ending.unwrittenLines + " lines couldn't be written to the file.")
    if (ending.audioError) notes.push("The transcript was saved, but its audio couldn't be: " + ending.audioError.replace(/\.$/, "") + ".")
    if (ending.reason === "interrupted") notes.push("Speecher quit while it was recording, so it ends there.")
    return notes
}

// A Recording's name: its own, or when it started, "Call at 10:00".
function recordingTitle(r) {
    return r.title || "Call at " + Qt.formatTime(r.startedAt, "HH:mm")
}

// "3 of 7", where Find is in a recording's matches.
function foundOf(at, count) { return count ? at + " of " + count : "No matches" }

// A line's time from the recording's start, "00:12:31".
function lineClock(ms) {
    const s = Math.floor(ms / 1000)
    const two = n => String(n).padStart(2, "0")
    return two(Math.floor(s / 3600)) + ":" + two(Math.floor(s / 60) % 60) + ":" + two(s % 60)
}

// The reader's line under its title. A Recording's: "Recording, Thursday
// 10:00 to 10:42", and how it stopped where it wasn't by hand: ", stopped
// after silence". A file's: where its audio is, when, how long, its words
// and profile: "File in ~/Calls, Friday 17:20, 28 min, 4,102 words, Other".
function readerDetail(r, today, home) {
    const day = dayHeading(r.day, today)
    if (r.kind === "file") {
        const at = folderOf(r.sourcePath)
        const parts = [text.file + (at ? " in " + shortPath(at, home) : ""), day + " " + Qt.formatTime(r.finishedAt, "HH:mm")]
        if (r.durationMs > 0) parts.push(duration(r.durationMs))
        parts.push(_num(r.words) + (r.words === 1 ? " word" : " words"))
        if (r.profileName) parts.push(r.profileName)
        return parts.join(", ")
    }
    const span = Qt.formatTime(r.startedAt, "HH:mm") + (r.live ? "" : " to " + Qt.formatTime(r.finishedAt, "HH:mm"))
    const silence = r.ending && r.ending.reason === "silence" ? ", stopped after silence" : ""
    return text.recording + ", " + day + " " + span + silence
}

// "Speecher keeps it on this computer for 90 days, then deletes the words."
function keepTextDetail(textDays) {
    return textDays > 0 ? "Speecher keeps it on this computer for " + textDays + " days, then deletes the words."
                        : "Speecher keeps it on this computer until you delete it."
}

// The line under the page's title: what History keeps of dictations and
// files now, then of recordings, which keep theirs until deleted.
// "Dictations and files: text for 90 days, no audio. Recordings: until you
// delete them."
function keeping(keeps, textDays, audioDays) {
    const recordings = " Recordings: until you delete them."
    if (keeps === "nothing") return "Dictations and files: nothing new." + recordings
    if (keeps === "stats") return "Dictations and files: stats only, not your words." + recordings
    const textFor = textDays > 0 ? "for " + textDays + " days" : "until you delete it"
    if (keeps === "text") return "Dictations and files: text " + textFor + ", no audio." + recordings
    const audio = audioDays < 0 || (textDays > 0 && audioDays >= textDays) ? "audio as long"
                : "audio for " + (audioDays === 1 ? "a day" : audioDays + " days")
    return "Dictations and files: text " + textFor + ", " + audio + "." + recordings
}

// Setup's line on what History keeps, before the first dictation:
// "Speecher keeps what you dictate on this computer for 90 days, so you can
// find it in History."
function firstRun(keeps, textDays) {
    if (keeps === "nothing") return "Speecher keeps nothing about what you dictate."
    if (keeps === "stats") return "Speecher counts your dictations for Home, and never keeps your words."
    const days = textDays > 0 ? "for " + textDays + " days" : "until you delete it"
    return "Speecher keeps what you dictate on this computer " + days + (keeps === "audio" ? ", with its audio" : "")
        + ", so you can find it in History."
}

function matches(n) { return n === 1 ? "1 match" : _num(n) + " matches" }
// Nothing matches "403 retry"
function noMatch(query) { return query ? "Nothing matches “" + query + "”" : "Nothing matches these filters" }

// "Delete the text and audio of 23 dictations from today?"
function clearQuestion(count, range) {
    const what = count === 1 ? "1 dictation or file" : _num(count) + " dictations and files"
    const when = { hour: "from the last hour", today: "from today", "7d": "from the last 7 days", all: "" }[range]
    return "Delete the text and audio of " + what + (when ? " " + when : "") + "?"
}

// A day's heading: Today, Yesterday, a weekday within the week, then a date.
function dayHeading(day, today) {
    const date = new Date(day + "T00:00:00")
    const days = Math.round((today - date) / 86400000)
    if (days === 0) return "Today"
    if (days === 1) return "Yesterday"
    if (days > 1 && days < 7) return Qt.locale().dayName(date.getDay(), QtQml.Locale.LongFormat)
    return Qt.formatDate(date, date.getFullYear() === today.getFullYear() ? "MMMM d" : "MMMM d, yyyy")
}

// "6 s", "1 min 20 s", "28 min", "1 h 4 min"
function duration(ms) {
    const s = Math.round(ms / 1000)
    if (s < 60) return s + " s"
    const m = Math.floor(s / 60)
    if (m < 10) return m + " min" + (s % 60 ? " " + (s % 60) + " s" : "")
    if (m < 60) return m + " min"
    return Math.floor(m / 60) + " h" + (m % 60 ? " " + (m % 60) + " min" : "")
}

// A row's detail, its parts set apart by commas: the app or "File", the
// time, how long, the words of a file, the profile.
function detail(r) {
    if (r.kind === "recording") {
        const started = Qt.formatTime(r.startedAt, "HH:mm")
        return r.live ? text.recording + ", " + started : [text.recording, started, duration(r.durationMs)].join(", ")
    }
    const parts = [r.kind === "file" ? text.file : (r.app || "Unknown app")]
    parts.push(Qt.formatTime(r.finishedAt, "HH:mm"))
    if (r.durationMs > 0) parts.push(duration(r.durationMs))
    if (r.kind === "file") parts.push(_num(r.words) + (r.words === 1 ? " word" : " words"))
    if (r.profileName) parts.push(r.profileName)
    return parts.join(", ")
}

// What became of a dictation, where it didn't simply paste; empty otherwise.
function outcome(r) {
    if (r.outcome === "undelivered") return "Not delivered"
    if (r.outcome === "copied") return "Not pasted, copied"
    if (r.outcome === "missed") return "Missed some of the call"
    if (r.kind === "edit") return "Edit to a selection"
    return ""
}

// The first words of an entry's text, for a toast.
function shortened(words) {
    const line = words.replace(/\s+/g, " ").trim()
    return line.length > 40 ? line.slice(0, 39).trim() + "\u2026" : line
}

// "0:06", the playing position or an entry's length.
function clock(ms) {
    const s = Math.round(ms / 1000)
    return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
}

function _list(names) { return names.length > 1 ? names.slice(0, -1).join(", ") + " and " + names[names.length - 1] : names[0] || "" }
function _num(n) { return Number(n).toLocaleString(Qt.locale(), "f", 0) }
