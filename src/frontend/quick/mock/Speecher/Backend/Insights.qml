pragma Singleton
import QtQuick
import Speecher.Launch

// Mock of core's InsightsSummary and Home's wording: about five months of
// generated dictation ending in a 47-day streak, summarised for the chosen
// period. This is the one record of past dictation: General counts and
// clears it here. Start it empty or off with --insights-state=empty|off.
QtObject {
    id: insights

    // ready, empty (no dictation yet, or cleared) or off (Keep insights is
    // off in General; what was kept stays until cleared).
    readonly property string state: Settings.values.insightsEnabled === false ? "off" : _cleared ? "empty" : "ready"
    readonly property bool hasData: state === "ready"
    property bool _cleared: Launch.arg("insights-state", "") === "empty"

    // The history General shows and clears.
    readonly property int kept: _sum(heatmap, "dictations")
    readonly property string keptText: kept > 0 ? Words.count(kept, "dictation", "dictations") + " kept since " + Qt.formatDate(firstDictation, "MMMM yyyy") + "."
                                                : "Nothing is kept right now."
    readonly property string clearText: "Delete " + Words.count(kept, "dictation", "dictations")
    readonly property string clearActionText: "Clear history…"
    function clear() { _cleared = true }

    property string period: "30d"
    readonly property var periods: [
        { id: "7d", label: "Last 7 days" },
        { id: "30d", label: "Last 30 days" },
        { id: "year", label: "This year" },
        { id: "all", label: "All time" }
    ]
    property string heatMeasure: "dictations"
    readonly property var heatMeasures: [
        { id: "dictations", label: "Dictations" },
        { id: "words", label: "Words" },
        { id: "audio", label: "Minutes of audio" }
    ]

    // Home's fixed wording.
    readonly property var text: ({
        yourDictation: "Your dictation",
        activity: "Activity",
        timeOfDay: "Time of day",
        profilesUsed: "Writing Profiles",
        apps: "Apps",
        records: "Records",
        milestones: "Milestones",
        recent: "Earlier dictations",
        learnedCorrections: "Learned corrections",
        reviewCorrections: "Review learned corrections",
        less: "Less",
        more: "More",
        nothingYet: "Your next dictation appears here.",
        copy: "Copy",
        startDictation: "Start dictation",
        stop: "Stop",
        pressShortcut: "Press the shortcut in any app to dictate into it.",
        noShortcut: "Set a dictation shortcut to dictate from any app.",
        setShortcut: "Set shortcut",
        privacyNote: "Insights are stored only on this computer and never sent anywhere.",
        insightsSettings: "Insights settings",
        offTitle: "Insights are off",
        offBody: "Speecher isn't counting new dictation. History you already have stays on this computer until you clear it in Insights settings.",
        emptyTitle: "No dictation yet",
        emptyBody: "Counts, streaks and your busiest hours appear here after your first dictation."
    })

    readonly property var share: ({
        label: "Share",
        copyImage: "Copy image with stats",
        copyText: "Copy as text",
        saveJson: "Save as JSON…",
        saved: "Saved"
    })
    // The period's figures, one "Words: 1,284" line each. Core copies them
    // drawn as a card for the image; the mock copies the text both times.
    function _shareText() { return summary ? summary.rows.map(r => r.label + ": " + r.value).join("\n") : "" }
    function copyImage() { System.copy(_shareText()) }
    function copyText() { System.copy(_shareText()) }
    // Returns the confirmation the Share button shows.
    function saveJson() { return share.saved }

    // The data ------------------------------------------------------------

    readonly property date today: new Date(2026, 9, 23)
    readonly property date firstDictation: new Date(2026, 4, 11)
    readonly property int typingWordsPerMinute: 40

    // Every day of the last 53 Monday-first weeks up to today, oldest first.
    readonly property var heatmap: _cleared ? [] : _generate()

    function _rng(seed) {
        let s = seed
        return () => { s = (s * 1664525 + 1013904223) % 4294967296; return s / 4294967296 }
    }
    function _day(offset) { return new Date(today.getFullYear(), today.getMonth(), today.getDate() + offset) }
    function _generate() {
        const r = _rng(11)
        const streakStart = new Date(2026, 8, 7)
        const firstOffset = -((today.getDay() + 6) % 7) - 52 * 7
        const days = []
        for (let i = firstOffset; i <= 0; ++i) {
            const date = _day(i)
            const weekend = date.getDay() === 0 || date.getDay() === 6
            const ramp = Math.min(1, (date - firstDictation) / (86400000 * 100))
            let n = 0
            if (date >= firstDictation && date.getTime() !== _day(-48).getTime()) {
                const active = date >= streakStart || r() < 0.62 + 0.25 * ramp - (weekend ? 0.4 : 0)
                if (active) n = Math.max(1, Math.round((weekend ? 4 : 7 + 15 * ramp) * (0.45 + r() * 1.1) * (i > -30 ? 1.25 : 1)))
            }
            const words = Math.round(n * (30 + r() * 20))
            days.push({ date: date, dictations: n, words: words,
                        audioMs: n ? Math.round(words / (132 + r() * 20) * 60000) : 0 })
        }
        return days
    }

    // Wording helpers (core's InsightsSummary free functions) ----------------

    function hourLabel(h) {
        const h12 = h % 12 === 0 ? 12 : h % 12
        return h12 + " " + (h < 12 ? "am" : "pm")
    }
    function clockText(ms) {
        const s = Math.round(ms / 1000)
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }
    function minutesText(m) {
        if (m < 60) return m + " min"
        const h = Math.floor(m / 60), rest = m % 60
        return rest ? h + " h " + rest + " min" : h + " h"
    }
    function longDate(d) { return Qt.formatDate(d, "dddd, MMMM d, yyyy") }
    function shortDate(d) { return Qt.formatDate(d, "MMMM d") }

    readonly property var _books: [
        ["the Gettysburg Address", 272], ["The Raven", 1086], ["the Declaration of Independence", 1458],
        ["The Tell-Tale Heart", 2093], ["An Occurrence at Owl Creek Bridge", 3767], ["Macbeth", 16174],
        ["Romeo and Juliet", 24545], ["Hamlet", 30557], ["The Great Gatsby", 47094],
        ["the first Harry Potter book", 76944], ["The Hobbit", 95356], ["To Kill a Mockingbird", 100388],
        ["The Fellowship of the Ring", 187790], ["Moby-Dick", 206052], ["War and Peace", 587287]
    ]
    readonly property var _fractions: [
        [0.25, "a quarter of"], [1 / 3, "a third of"], [0.5, "half of"], [0.75, "three-quarters of"],
        [1, "as long as"], [2, "twice the length of"], [3, "three times the length of"], [4, "four times the length of"]
    ]
    function bookComparison(words) {
        if (!words) return ""
        let best = null
        for (const [book, length] of _books)
            for (const [f, phrase] of _fractions) {
                const miss = Math.abs(Math.log(words / length / f))
                if (!best || miss <= best.miss) best = { miss: miss, book: book, phrase: phrase }
            }
        return "About " + best.phrase + " " + best.book
    }

    // The summary for the chosen period --------------------------------------

    readonly property var summary: hasData && heatmap.length ? _summarize(period) : null

    function _sum(days, key) { return days.reduce((t, d) => t + d[key], 0) }
    // "23% more than the previous 30 days", or nothing without a previous period.
    function _delta(cur, prev, label) {
        if (!prev) return ""
        const pct = Math.round((cur - prev) / prev * 100)
        if (pct === 0) return "Same as the previous " + label
        return Math.abs(pct) + "% " + (pct > 0 ? "more" : "fewer") + " than the previous " + label
    }
    function _sentence(...parts) { return parts.filter(p => p).join(". ") + "." }
    function _summarize(p) {
        const all = heatmap
        const n = all.length
        const len = p === "7d" ? 7 : p === "30d" ? 30 : 0
        const days = len ? all.slice(n - len) : p === "year" ? all.filter(d => d.date.getFullYear() === today.getFullYear()) : all
        const prev = len ? all.slice(n - 2 * len, n - len) : null
        const label = p === "7d" ? "7 days" : "30 days"

        const words = _sum(days, "words")
        const dictations = _sum(days, "dictations")
        const audioMs = _sum(days, "audioMs")
        const activeDays = days.filter(d => d.dictations > 0).length
        const audioMin = audioMs / 60000
        const wpm = Math.round(words / audioMin)
        const saved = Math.round(words / typingWordsPerMinute - audioMin)

        // Hour of day: mostly mornings, with an afternoon second wind.
        const weights = [0, 0, 0, 0, 0, 0, 0.2, 0.8, 2.2, 3.6, 4.2, 3.1, 1.6, 1.9, 3.0, 3.3, 2.4, 1.5, 0.9, 0.7, 0.6, 0.4, 0.2, 0.05]
        const total = weights.reduce((a, b) => a + b, 0)
        const hourCounts = weights.map(w => Math.round(w / total * dictations))
        const peakHour = hourCounts.indexOf(Math.max(...hourCounts))
        const byWeekday = [0, 0, 0, 0, 0, 0, 0]
        for (const d of days) byWeekday[d.date.getDay()] += d.dictations
        const busiest = byWeekday.indexOf(Math.max(...byWeekday))

        const apps = [
            ["Slack", "Work", 0.31], ["Thunderbird", "Email", 0.21], ["Visual Studio Code", "AI coding", 0.19],
            ["Firefox", "Personal", 0.12], ["Obsidian", "Personal", 0.09], ["4 other apps", "", 0.08]
        ]
        const profiles = [["Work", 0.31], ["Email", 0.21], ["Personal", 0.21], ["AI coding", 0.19], ["Other", 0.08]]

        return {
            // The period at a glance: what was counted, the figure, and a note.
            rows: [
                { label: "Words", value: Words.num(words),
                  note: _sentence(bookComparison(words), _delta(words, prev && _sum(prev, "words"), label)) },
                { label: "Dictations", value: Words.num(dictations),
                  note: _sentence(Words.num(activeDays ? dictations / activeDays : 0) + " a day when you dictate",
                                  _delta(dictations, prev && _sum(prev, "dictations"), label)) },
                { label: "Audio", value: audioMs >= 3600000 ? Words.num(audioMs / 3600000, 1) + " hours" : Math.round(audioMin) + " min",
                  note: _sentence(clockText(dictations ? audioMs / dictations : 0) + " a dictation on average") },
                { label: "Streak", value: Words.count(streak.current, "day", "days"), note: streak.text },
                { label: "Pace", value: Words.num(wpm) + " words a minute",
                  note: _sentence(Words.num(wpm / typingWordsPerMinute, 1) + " times as fast as typing at " + typingWordsPerMinute + " words a minute") },
                { label: "Time saved", value: minutesText(saved), note: "Against typing the same words." }
            ],
            hourCounts: hourCounts,
            peakHour: peakHour,
            hourText: "Most around " + hourLabel(peakHour) + ", and most on " + Qt.locale().dayName(busiest, Locale.LongFormat) + "s.",
            profiles: profiles.map(([name, s]) => ({ name: name, words: Words.count(Math.round(words * s), "word", "words"), percent: Math.round(s * 100) + "%" })),
            apps: apps.map(([name, profile, s]) => ({ name: name, profile: profile, words: Words.count(Math.round(words * s), "word", "words") }))
        }
    }
    function hourTip(h, count) {
        return { title: hourLabel(h) + " to " + hourLabel((h + 1) % 24), detail: Words.count(count, "dictation", "dictations") }
    }

    // Streak, heatmap and records ignore the period ---------------------------

    readonly property var streak: hasData && heatmap.length ? _streak() : ({ current: 0, best: 0, text: "" })
    function _streak() {
        let current = 0
        for (let i = heatmap.length - 1; i >= 0 && heatmap[i].dictations > 0; --i) ++current
        let best = 0, run = 0
        for (const d of heatmap) { run = d.dictations > 0 ? run + 1 : 0; best = Math.max(best, run) }
        return { current: current, best: best,
                 text: current > 0 && current >= best ? "Your longest yet." : "Your longest was " + Words.count(best, "day", "days") + "." }
    }

    function heatLevels(measure) {
        const key = measure === "words" ? "words" : measure === "audio" ? "audioMs" : "dictations"
        const values = heatmap.map(d => d[key])
        const sorted = values.filter(v => v > 0).sort((a, b) => a - b)
        const q = [0.25, 0.5, 0.75].map(f => sorted[Math.floor(f * (sorted.length - 1))] || 0)
        return values.map(v => v === 0 ? 0 : 1 + q.filter(x => v > x).length)
    }
    function dayTip(index, measure) {
        const d = heatmap[index]
        const detail = longDate(d.date)
        if (!d.dictations) return { title: "No dictation", detail: detail }
        if (measure === "words") return { title: Words.count(d.words, "word", "words"), detail: detail }
        if (measure === "audio") return { title: Math.max(1, Math.round(d.audioMs / 60000)) + " min of audio", detail: detail }
        return { title: Words.count(d.dictations, "dictation", "dictations"), detail: detail }
    }
    readonly property int weeks: Math.ceil(heatmap.length / 7)
    // Month names over the week columns they start in, never on the first
    // column or the last two.
    readonly property var monthLabels: {
        const labels = []
        for (let c = 1; c < weeks - 2; ++c) {
            const monday = heatmap[c * 7].date
            if (monday.getDate() <= 7) labels.push({ column: c, label: Qt.locale().monthName(monday.getMonth(), Locale.ShortFormat) })
        }
        return labels
    }
    readonly property var heatRowLabels: ["Mon", "", "Wed", "", "Fri", "", ""]
    readonly property string heatmapCaption: Words.count(heatmap.filter(d => d.dictations > 0).length, "day", "days") + " with dictation in the last year."

    readonly property real allTimeWords: _sum(heatmap, "words")
    readonly property var records: hasData && heatmap.length ? _records() : []
    function _records() {
        const busiest = heatmap.reduce((a, b) => b.dictations > a.dictations ? b : a)
        const wordiest = heatmap.reduce((a, b) => b.words > a.words ? b : a)
        const since = Math.round((today - firstDictation) / 86400000)
        return [
            { title: "Longest dictation", detail: "312 words into Thunderbird on September 14", value: "4:52" },
            { title: "Busiest day", detail: longDate(busiest.date), value: Words.count(busiest.dictations, "dictation", "dictations") },
            { title: "Most words in a day", detail: longDate(wordiest.date), value: Words.count(wordiest.words, "word", "words") },
            { title: "First dictation", detail: since + " days ago", value: Qt.formatDate(firstDictation, "MMMM d, yyyy") }
        ]
    }

    // Word milestones either side of the all-time count; the next one carries
    // how far along it is.
    readonly property var _milestoneSteps: [1000, 5000, 10000, 25000, 50000, 100000, 250000, 500000, 1000000]
    readonly property var milestones: {
        const words = allTimeWords
        const i = _milestoneSteps.findIndex(m => m > words)
        const shown = _milestoneSteps.slice(Math.max(0, i - 3), i + 2)
        return shown.map(m => {
            const k = _milestoneSteps.indexOf(m)
            const from = k > 0 ? _milestoneSteps[k - 1] : 0
            return { label: Words.count(m, "word", "words"),
                     state: m <= words ? "passed" : m === _milestoneSteps[i] ? "next" : "later",
                     status: m <= words ? "Passed" : m === _milestoneSteps[i] ? Words.num(m - words) + " to go" : "",
                     progress: Math.max(0, Math.min(1, (words - from) / (m - from))) }
        })
    }
    readonly property string milestoneCaption: hasData ? Words.count(allTimeWords, "word", "words") + " dictated in all." : ""

    readonly property var corrections: ({
        caption: "23 learned from edits you made after dictating.",
        recent: [
            { heard: "post gres", fixed: "Postgres" },
            { heard: "speacher", fixed: "Speecher" },
            { heard: "clawed code", fixed: "Claude Code" }
        ]
    })

    // One tip, a different one each day.
    readonly property var _tips: [
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
    readonly property var tip: _tips[today.getDate() % _tips.length]

    // The latest dictations, newest first.
    readonly property var recent: hasData ? [
        { text: "Sounds good, let's move the review to Thursday afternoon so Priya can join. I'll update the doc with the open questions before then.",
          words: 27, app: "Slack", profile: "Work", when: "2 min ago" },
        { text: "Refactor the session store so it flushes on idle instead of on every write, and add a test that kills the process mid-flush.",
          words: 25, app: "Visual Studio Code", profile: "AI coding", when: "18 min ago" },
        { text: "Hi Marco, thanks for sending the draft over. I've left a few comments on the pricing section, mostly about how we phrase the annual discount.",
          words: 28, app: "Thunderbird", profile: "Email", when: "1 h ago" },
        { text: "Pick up oat milk, the good coffee beans and something for Sam's birthday on Saturday.",
          words: 15, app: "Obsidian", profile: "Personal", when: "3 h ago" },
        { text: "The flaky test only fails when the fixture loads before the clock is mocked. Moving the mock into the setup hook fixes it locally.",
          words: 25, app: "Slack", profile: "Work", when: "yesterday" }
    ] : []
    // "27 words into Slack, 2 min ago, Work profile"
    function recentDetail(d) { return Words.count(d.words, "word", "words") + " into " + d.app + ", " + d.when + ", " + d.profile + " profile" }
    function copyRecent(index) { System.copy(recent[index].text) }

    Component.onCompleted: if (Launch.arg("insights-state", "") === "off") Qt.callLater(() => Settings.set("insightsEnabled", false))
}
