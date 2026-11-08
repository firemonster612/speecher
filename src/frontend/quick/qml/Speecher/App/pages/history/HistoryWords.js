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
        { id: "all", label: "Everything" }
    ],
    clearDetail: "Their text and audio are deleted from this computer. Home keeps counting them unless you check below.",
    alsoStats: "Also delete their stats",
    clearConfirm: "Delete",
    clearFailed: "Speecher couldn't clear History. Try again.",
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
    offText: "Speecher keeps nothing about your dictations. Recordings you start still save their transcripts.",
    statsTitle: "Speecher keeps stats, not words",
    statsText: "Home counts your dictations. Keep their text to find and copy them here.",
    keepText: "Keep text",
    keepTextQuestion: "Keep what you dictate?",
    keepTextConfirm: "Keep text",
    settings: "What Speecher keeps",
    emptyTitle: "Nothing here yet",
    emptyText: "What you dictate, transcribe or record shows here.",

    refined: "Refined",
    spoken: "Spoken",
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
    findInRecording: "Find in this recording…",
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
    openFile: "Open file",
    showInFolder: "Show in folder",
    back: "All of History",
    fileMissing: "The transcript file was moved or deleted. History still has its lines.",
    deleteRecording: "Delete this recording?",
    deleteRecordingDetail: "Its transcript file stays unless you check below.",
    alsoDeleteFile: "Also delete the transcript file and its audio"
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

// The reader's line under its title: "Recording, Thursday 10:00 to 10:42".
function readerDetail(r, today) {
    const day = dayHeading(r.day, today)
    const span = Qt.formatTime(r.startedAt, "HH:mm") + (r.live ? "" : " to " + Qt.formatTime(r.finishedAt, "HH:mm"))
    return text.recording + ", " + day + " " + span
}

// "Speecher keeps it on this computer for 90 days, then deletes the words."
function keepTextDetail(textDays) {
    return textDays > 0 ? "Speecher keeps it on this computer for " + textDays + " days, then deletes the words."
                        : "Speecher keeps it on this computer until you delete it."
}

// The line under the page's title: what History keeps now.
function keeping(keeps, textDays, audioDays) {
    if (keeps === "nothing") return "Keeping nothing new."
    if (keeps === "stats") return "Keeping stats only, not your words."
    const textFor = textDays > 0 ? "for " + textDays + " days" : "until you delete it"
    if (keeps === "text") return "Keeping text " + textFor + ", no audio."
    const audio = audioDays < 0 || (textDays > 0 && audioDays >= textDays) ? "audio as long"
                : "audio for " + (audioDays === 1 ? "a day" : audioDays + " days")
    return "Keeping text " + textFor + ", " + audio + "."
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

function _num(n) { return Number(n).toLocaleString(Qt.locale(), "f", 0) }
