// Home's wording: its titles and notes, and the lines it builds from the
// numbers Insights carries. Dates are Dates at the start of their day.
.pragma library
.import QtQml 2.15 as QtQml
.import Speecher.Wording 1.0 as Wording

var text = {
    yourDictation: "Your dictation",
    activity: "Activity",
    timeOfDay: "Time of day",
    profilesUsed: "Writing Profiles",
    apps: "Apps",
    records: "Records",
    milestones: "Milestones",
    learnedCorrections: "Learned corrections",
    less: "Less",
    more: "More",
    nothingYet: "Your next dictation appears here.",
    copy: "Copy",
    startDictation: "Start dictation",
    stop: "Stop",
    pressShortcut: "Press the shortcut in any app to dictate into it.",
    noShortcut: "Set a dictation shortcut to dictate from any app.",
    noShortcutHere: "Your desktop gives Speecher no shortcut. Start here, or bind speecher --start-listening to a key.",
    notReady: "Not ready to dictate",
    fixFirst: "Dictation can start once these are fixed.",

    setShortcut: "Set shortcut",
    privacyNote: "Insights are stored only on this computer and never sent anywhere.",
    insightsSettings: "Insights settings",
    offTitle: "Insights are off",
    offBody: "Speecher isn't counting new dictation. History you already have stays on this computer until you clear it in Insights settings.",
    emptyTitle: "No dictation yet",
    emptyBody: "Counts, streaks and your busiest hours appear here after your first dictation."
}

var periods = [
    { id: "7d", label: "Last 7 days" },
    { id: "30d", label: "Last 30 days" },
    { id: "year", label: "This year" },
    { id: "all", label: "All time" }
]
var heatMeasures = [
    { id: "dictations", label: "Dictations" },
    { id: "words", label: "Words" },
    { id: "audio", label: "Minutes of audio" }
]

var share = {
    label: "Share",
    copyImage: "Copy image with stats",
    copyText: "Copy as text",
    saveJson: "Save as JSON…",
    saved: "Saved",
    saveFailed: "Couldn't save",
    saveFailedDetail: "Speecher couldn't write the stats to that file.",
    copyFailed: "Couldn't copy",
    copyFailedDetail: "Speecher couldn't draw the stats picture.",
    saveTitle: "Save stats",
    jsonFilter: "JSON files (*.json)",
    imageTitle: "My Speecher stats"
}
// "speecher-stats-2026-09-26.json"
function jsonFileName(today) { return "speecher-stats-" + Qt.formatDate(today, "yyyy-MM-dd") + ".json" }

function _words() { return Wording.Words }
function _count(n, one, many) { return _words().count(n, one, many) }
function _num(n, decimals) { return _words().num(n, decimals) }
function _capitalized(s) { return s.charAt(0).toUpperCase() + s.slice(1) }
function _sentence(parts) { return parts.filter(p => p).join(". ") + "." }

function hourLabel(h) {
    const h12 = h % 12 === 0 ? 12 : h % 12
    return h12 + " " + (h < 12 ? "am" : "pm")
}
// "4:52"
function clockText(ms) {
    const s = Math.round(ms / 1000)
    return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
}
// "45 min", "2 h", "2 h 5 min"
function minutesText(m) {
    if (m < 60) return m + " min"
    const h = Math.floor(m / 60), rest = m % 60
    return rest ? h + " h " + rest + " min" : h + " h"
}
// "4.0 hours" from an hour up, else "34 min"
function audioText(ms) {
    return ms >= 3600000 ? _num(ms / 3600000, 1) + " hours" : Math.round(ms / 60000) + " min"
}
function longDate(d) { return Qt.formatDate(d, "dddd, MMMM d, yyyy") }
// "today", "yesterday", a weekday within the week, else "Sep 14, 2026".
function relativeDay(d, today) {
    const days = Math.round((today - d) / 86400000)
    if (days === 0) return "today"
    if (days === 1) return "yesterday"
    if (days > 1 && days < 7) return Qt.locale().dayName(d.getDay(), QtQml.Locale.LongFormat)
    return Qt.formatDate(d, "MMM d, yyyy")
}
function periodName(period) { return periods.find(p => p.id === period).label }

// A profile by its name; a profile deleted before records kept names has none.
function profileName(name) { return name || "Deleted profile" }
// An app row's name: the app, or the fold of the ones past the top five.
function appName(app) { return app.otherApps ? _count(app.otherApps, "other app", "other apps") : app.name }

// Core's BookComparison: the words are `times` the book's length.
var _bookShares = [[1 / 4, "a quarter of"], [1 / 3, "a third of"], [1 / 2, "half of"], [3 / 4, "three-quarters of"],
                   [1, "as long as"], [2, "twice the length of"], [3, "three times the length of"], [4, "four times the length of"]]
// "half of Hamlet"
function _bookShare(c) {
    const share = _bookShares.find(b => b[0] === c.times)
    return (share ? share[1] : _num(c.times) + " times the length of") + " " + c.book
}
// "About half of Hamlet"
function bookText(s) {
    if (!s.words) return "Nothing yet"
    if (!s.bookComparison) return "A few sentences so far"
    return "About " + _bookShare(s.bookComparison)
}
// "Hamlet is about 30,557 words", then on a new line "Changes to about
// twice the length of Macbeth at 32,348 words"; empty with no words.
function bookTip(s) {
    if (!s.words) return ""
    const c = s.bookComparison, next = s.nextBookComparison
    return [c ? _capitalized(c.book) + " is about " + _num(c.bookWords) + " words" : "",
            next ? "Changes to about " + _bookShare(next) + " at " + _num(next.at) + " words" : ""]
        .filter(line => line).join("\n")
}

// "23% more than the previous 30 days", or nothing without a delta.
function _delta(delta, period) {
    if (delta === null || delta === undefined) return ""
    const days = period === "7d" ? "7 days" : "30 days"
    if (delta === 0) return "Same as the previous " + days
    return Math.abs(delta) + "% " + (delta > 0 ? "more" : "fewer") + " than the previous " + days
}

// The line under the streak.
function streakNote(streak, today) {
    if (streak.current > 0) {
        if (!streak.dictatedToday) return "Dictate today to keep it going."
        return streak.current >= streak.best ? "Your longest yet." : "Your longest was " + _count(streak.best, "day", "days") + "."
    }
    if (streak.brokenLength > 0)
        return _count(streak.brokenLength, "day", "days") + " in a row, until " + relativeDay(streak.brokenEnded, today) + "."
    return ""
}

// The period's figures: [{ label, value, note, tip }]; tip, the note's
// tooltip, only on Words.
function figures(s, period, today) {
    const rows = [
        { label: "Words", value: _num(s.words), note: _sentence([bookText(s), _delta(s.wordsDelta, period)]), tip: bookTip(s) },
        { label: "Dictations", value: _num(s.dictations),
          note: _sentence([s.dictations ? _num(s.dictationsPerActiveDay, 1) + " a day when you dictate" : "", _delta(s.dictationsDelta, period)]) },
        { label: "Audio", value: audioText(s.audioMs),
          note: s.dictations ? clockText(s.averageAudioMs) + " a dictation on average." : "" },
        { label: "Streak", value: _count(s.streak.current, "day", "days"), note: streakNote(s.streak, today) }
    ]
    if (s.wordsPerMinute > 0) {
        rows.push({ label: "Pace", value: _num(s.wordsPerMinute) + " words a minute",
                    note: _num(s.wordsPerMinute / s.typingWordsPerMinute, 1) + " times as fast as typing at " + s.typingWordsPerMinute + " words a minute." })
        rows.push({ label: "Time saved", value: minutesText(s.minutesSaved), note: "Against typing the same words." })
    }
    return rows
}

function hourText(s) {
    if (!s.hasHourData) return "After a few days of dictation this shows the hours you talk most."
    return "Most around " + hourLabel(s.peakHour) + ", and most on " + Qt.locale().dayName(s.busiestWeekday % 7, QtQml.Locale.LongFormat) + "s."
}
function hourTip(h, count) {
    return { title: hourLabel(h) + " to " + hourLabel((h + 1) % 24), detail: _count(count, "dictation", "dictations") }
}
// What the hour chart says to assistive technology in place of its bars.
function hourDescription(s) {
    if (!s || !s.dictations) return "No dictation in this period."
    return "Dictations by hour of day. Most around " + hourLabel(s.peakHour) + ", with " + _count(s.hourCounts[s.peakHour], "dictation", "dictations") + "."
}

// Monday first, as the heatmap's rows run.
function heatRowLabels() {
    return [1, 3, 5].reduce((labels, day) => { labels[day - 1] = Qt.locale().dayName(day, QtQml.Locale.ShortFormat); return labels }, ["", "", "", "", "", "", ""])
}
function heatmapCaption(activeDays) { return _count(activeDays, "day", "days") + " with dictation in the last year." }
function _dayValue(day, measure) {
    if (measure === "words") return _count(day.words, "word", "words")
    if (measure === "audio") return Math.max(1, Math.round(day.audioMs / 60000)) + " min of audio"
    return _count(day.dictations, "dictation", "dictations")
}
function dayTip(day, measure) {
    return { title: day.dictations ? _dayValue(day, measure) : "No dictation", detail: longDate(day.date) }
}
// What the heatmap says to assistive technology: the active days and the
// busiest under the measure.
function heatmapDescription(days, measure, activeDays) {
    const key = measure === "words" ? "words" : measure === "audio" ? "audioMs" : "dictations"
    const busiest = days.reduce((best, day) => !best || day[key] > best[key] ? day : best, null)
    const caption = heatmapCaption(activeDays)
    if (!busiest || !busiest.dictations) return caption
    return caption + " Busiest: " + Qt.formatDate(busiest.date, "MMM d, yyyy") + ", " + _dayValue(busiest, measure) + "."
}

// The Records card: [{ title, detail, value }]. The longest streak shows
// unless it is the one still going.
function records(s, today) {
    const since = Math.round((today - s.firstDictation) / 86400000)
    const streak = s.streak.bestEndsToday && s.streak.current > 0 ? [] : [
        { title: "Longest streak", detail: "Ended " + relativeDay(s.streak.bestEnd, today),
          value: _count(s.streak.best, "day", "days") }
    ]
    return streak.concat([
        { title: "Longest dictation", value: clockText(s.longest.audioMs),
          detail: _count(s.longest.words, "word", "words") + " into " + s.longest.app + ", " + relativeDay(s.longest.date, today) },
        { title: "Busiest day", detail: _capitalized(relativeDay(s.busiestDay.date, today)),
          value: _count(s.busiestDay.dictations, "dictation", "dictations") },
        { title: "Most words in a day", detail: _capitalized(relativeDay(s.wordiestDay.date, today)),
          value: _count(s.wordiestDay.words, "word", "words") },
        { title: "First dictation", detail: since === 0 ? "Today" : _count(since, "day", "days") + " ago",
          value: Qt.formatDate(s.firstDictation, "MMMM d, yyyy") }
    ])
}

// The Milestones card: [{ label, state, status, progress }].
function milestones(s) {
    return s.milestones.map(m => ({
        label: _count(m.words, "word", "words"), state: m.state, progress: m.progress,
        status: m.state === "passed" ? "Passed" : m.state === "next" ? _num(m.words - s.allTimeWords) + " to go" : ""
    }))
}
function milestoneCaption(s) { return _count(s.allTimeWords, "word", "words") + " dictated in all." }

// The learned corrections card says how learning stands, and what to do.
function correctionsCaption(c) {
    if (!c.learning) return "Learning is off."
    if (!c.accessible) return "Speecher learns corrections once desktop accessibility is on."
    if (!c.count) return "Fix a dictated word the same way twice and it appears here."
    return _count(c.count, "correction", "corrections") + " learned from edits you made after dictating."
}
// The card's button and the Vocabulary row it opens; no label, no button.
function correctionsAction(c) {
    if (!c.learning) return { label: "Turn on learning", row: "correctionLearningControl" }
    if (c.count) return { label: "Review learned corrections", row: "learnedCorrections" }
    return { label: "", row: "" }
}

// "27 words into Slack, today, Work profile"; the words alone when
// insights did not record the dictation.
function latestDetail(latest, today) {
    const parts = [_count(latest.words, "word", "words") + (latest.app ? " into " + latest.app : "")]
    if (latest.date) parts.push(relativeDay(latest.date, today))
    if (latest.date) parts.push(latest.profile ? latest.profile + " profile" : "a deleted profile")
    return parts.join(", ")
}

// The share picture's figures, [{ label, value }], and the line under them.
function shareFigures(s) {
    return [
        { label: "Words", value: _num(s.words) },
        { label: "Streak", value: _count(s.streak.current, "day", "days") },
        { label: "Dictations", value: _num(s.dictations) },
        { label: "Audio", value: audioText(s.audioMs) }
    ]
}
function shareLine(s) { return bookText(s) + ", " + _num(s.wordsPerMinute) + " words a minute" }

// Copy as text: a few lines for Slack or any chat.
function shareText(s, period) {
    const name = periodName(period)
    const lines = [share.imageTitle + ", " + name.charAt(0).toLowerCase() + name.slice(1),
                   _count(s.words, "word", "words") + " dictated"]
    if (s.words) lines.push(bookText(s))
    const streak = s.streak
    if (streak.current > 0)
        lines.push(_num(streak.current) + "-day streak" + (streak.current >= streak.best ? ", my longest yet" : " (best: " + _count(streak.best, "day", "days") + ")"))
    lines.push(_count(s.dictations, "dictation", "dictations") + ", " + audioText(s.audioMs) + " of audio transcribed")
    if (s.wordsPerMinute > 0)
        lines.push(_num(s.wordsPerMinute) + " words per minute, " + _num(s.wordsPerMinute / s.typingWordsPerMinute, 1) + "× faster than typing")
    if (s.apps.length) lines.push("Top apps: " + s.apps.map(a => appName(a) + " " + a.percent + "%").join(", "))
    return lines.join("\n")
}

// One tip, a different one each day.
var _tips = [
    { page: "refinement", action: "Refinement settings",
      text: "Select some text, press the shortcut and say what to change, such as \"make this friendlier\"." },
    { page: "profiles", action: "Writing profiles",
      text: "Writing Profiles set the tone and cleanup for each app, so Slack stays casual and email stays formal." },
    { page: "vocabulary", action: "Vocabulary",
      text: "Names and terms in Vocabulary are spelled right the first time." },
    { page: "shortcuts", action: "Shortcuts",
      text: "Pause a dictation while you think, then resume it. Nothing you said is lost." },
    { page: "transcribe", action: "Transcribe files",
      text: "Meetings, voice memos and podcasts can be transcribed with the same cleanup as dictation." }
]
function tip(today) { return _tips[(today ? today.getDate() : 0) % _tips.length] }
