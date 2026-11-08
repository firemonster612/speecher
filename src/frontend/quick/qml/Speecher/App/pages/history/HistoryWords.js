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
        { id: "file", label: "Files" }
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
    offText: "Speecher keeps nothing about your dictations.",
    statsTitle: "Speecher keeps stats, not words",
    statsText: "Home counts your dictations. Keep their text to find and copy them here.",
    keepText: "Keep text",
    keepTextQuestion: "Keep what you dictate?",
    keepTextConfirm: "Keep text",
    settings: "What Speecher keeps",
    emptyTitle: "Nothing here yet",
    emptyText: "What you dictate or transcribe shows here.",

    refined: "Refined",
    spoken: "Spoken",
    copy: "Copy",
    copied: "Copied",
    pasteAgain: "Paste again",
    play: "Play",
    stop: "Stop",
    delete_: "Delete",
    more: "More",
    file: "File"
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
