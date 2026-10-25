pragma Singleton
import QtQuick
import Speecher.Launch

// The preview's Insights, with host/QuickInsights's interface (its header
// documents each field): about five months of generated dictation,
// summarised for the chosen period the way core's summarize does. Start it
// empty or off with --insights-state=empty|off.
QtObject {
    id: insights

    readonly property string state: Settings.values.insightsEnabled === false ? "off" : _cleared ? "empty" : "ready"
    readonly property bool hasData: state === "ready"
    property bool _cleared: Launch.arg("insights-state", "") === "empty"

    readonly property int kept: _sum(heatmap, "dictations")
    readonly property var firstDictation: kept ? _firstDictation : null
    function clear() { _cleared = true; return true }

    property string period: "30d"
    property string heatMeasure: "dictations"

    function copyText(text) { System.copy(text) }
    function copyImage(image) { return true }
    function saveJson(file) { return true }
    function copyLatest() { if (latest) System.copy(latest.text) }

    readonly property date today: new Date(2026, 9, 23)
    readonly property date _firstDictation: new Date(2026, 4, 11)

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
            const ramp = Math.min(1, (date - _firstDictation) / (86400000 * 100))
            let n = 0
            if (date >= _firstDictation && date.getTime() !== _day(-48).getTime()) {
                const active = date >= streakStart || r() < 0.62 + 0.25 * ramp - (weekend ? 0.4 : 0)
                if (active) n = Math.max(1, Math.round((weekend ? 4 : 7 + 15 * ramp) * (0.45 + r() * 1.1) * (i > -30 ? 1.25 : 1)))
            }
            const words = Math.round(n * (30 + r() * 20))
            days.push({ date: date, dictations: n, words: words,
                        audioMs: n ? Math.round(words / (132 + r() * 20) * 60000) : 0 })
        }
        return days
    }
    function _sum(days, key) { return days.reduce((t, d) => t + d[key], 0) }

    readonly property var heatLevels: {
        const key = heatMeasure === "words" ? "words" : heatMeasure === "audio" ? "audioMs" : "dictations"
        const values = heatmap.map(d => d[key])
        const sorted = values.filter(v => v > 0).sort((a, b) => a - b)
        const q = [0.25, 0.5, 0.75].map(f => sorted[Math.floor(f * (sorted.length - 1))] || 0)
        return values.map(v => v === 0 ? 0 : 1 + q.filter(x => v > x).length)
    }
    // Month names over the week columns they start in, never on the first
    // column or the last two.
    readonly property var monthLabels: {
        const labels = []
        const weeks = Math.ceil(heatmap.length / 7)
        for (let c = 1; c < weeks - 2; ++c) {
            const monday = heatmap[c * 7].date
            if (monday.getDate() <= 7) labels.push({ column: c, label: Qt.locale().monthName(monday.getMonth(), Locale.ShortFormat) })
        }
        return labels
    }

    readonly property var summary: hasData && heatmap.length ? _summarize(period) : null

    function _percent(cur, prev) { return prev ? Math.round((cur - prev) / prev * 100) : null }
    function _summarize(p) {
        const all = heatmap
        const n = all.length
        const len = p === "7d" ? 7 : p === "30d" ? 30 : 0
        const days = len ? all.slice(n - len) : p === "year" ? all.filter(d => d.date.getFullYear() === today.getFullYear()) : all
        const prev = len ? all.slice(n - 2 * len, n - len) : null

        const words = _sum(days, "words")
        const dictations = _sum(days, "dictations")
        const audioMs = _sum(days, "audioMs")
        const activeDays = days.filter(d => d.dictations > 0).length
        const wpm = Math.round(words / (audioMs / 60000))

        // Hour of day: mostly mornings, with an afternoon second wind.
        const weights = [0, 0, 0, 0, 0, 0, 0.2, 0.8, 2.2, 3.6, 4.2, 3.1, 1.6, 1.9, 3.0, 3.3, 2.4, 1.5, 0.9, 0.7, 0.6, 0.4, 0.2, 0.05]
        const total = weights.reduce((a, b) => a + b, 0)
        const hourCounts = weights.map(w => Math.round(w / total * dictations))

        const apps = [["Slack", "Work", 0.31], ["Thunderbird", "Email", 0.21], ["Visual Studio Code", "AI coding", 0.19],
                      ["Firefox", "Personal", 0.12], ["Obsidian", "Personal", 0.09], ["", "", 0.08]]
        const profiles = [["Work", 0.31], ["Email", 0.21], ["Personal", 0.21], ["AI coding", 0.19], ["Other", 0.08]]

        let current = 0
        for (let i = all.length - 1; i >= 0 && all[i].dictations > 0; --i) ++current
        let best = 0, run = 0
        for (const d of all) { run = d.dictations > 0 ? run + 1 : 0; best = Math.max(best, run) }
        const allTimeWords = _sum(all, "words")
        const steps = [1000, 10000, 50000, 100000, 250000, 500000, 1000000]
        const next = steps.findIndex(m => m > allTimeWords)

        return {
            words: words, dictations: dictations, audioMs: audioMs,
            averageAudioMs: dictations ? audioMs / dictations : 0,
            dictationsPerActiveDay: activeDays ? dictations / activeDays : 0,
            wordsDelta: prev ? _percent(words, _sum(prev, "words")) : null,
            dictationsDelta: prev ? _percent(dictations, _sum(prev, "dictations")) : null,
            bookComparison: { book: "Hamlet", times: 0.5, bookWords: 30557 },
            nextBookComparison: { book: "Romeo and Juliet", times: 0.75, at: 17317 },
            wordsPerMinute: wpm, typingWordsPerMinute: 40, minutesSaved: Math.max(0, Math.round(words / 40 - audioMs / 60000)),
            hourCounts: hourCounts, peakHour: hourCounts.indexOf(Math.max(...hourCounts)), busiestWeekday: 2,
            hasHourData: dictations >= 20,
            apps: apps.map(([name, profile, s]) => ({ name: name, otherApps: name ? 0 : 4, profile: profile, words: Math.round(words * s), percent: Math.round(s * 100) })),
            profiles: profiles.map(([name, s]) => ({ name: name, words: Math.round(words * s), percent: Math.round(s * 100) })),
            streak: { current: current, best: best, bestEnd: today, bestEndsToday: current === best, dictatedToday: true,
                      brokenLength: 0, brokenEnded: null },
            activeDaysLastYear: all.filter(d => d.dictations > 0).length,
            allTimeWords: allTimeWords,
            milestones: steps.slice(Math.max(0, next - 3), next + 2).map(m => {
                const k = steps.indexOf(m), from = k > 0 ? steps[k - 1] : 0
                return { words: m, state: k < next ? "passed" : k === next ? "next" : "later",
                         progress: Math.max(0, Math.min(1, (allTimeWords - from) / (m - from))) }
            }),
            longest: { audioMs: 292000, words: 312, app: "Thunderbird", date: new Date(2026, 8, 14) },
            busiestDay: { date: new Date(2026, 9, 6), dictations: 34 },
            wordiestDay: { date: new Date(2026, 9, 6), words: 1490 },
            firstDictation: _firstDictation
        }
    }

    readonly property var corrections: ({
        count: 23, learning: true, accessible: true,
        recent: [
            { heard: "post gres", fixed: "Postgres" },
            { heard: "speacher", fixed: "Speecher" },
            { heard: "clawed code", fixed: "Claude Code" }
        ]
    })

    readonly property var latest: hasData ? {
        text: "Sounds good, let's move the review to Thursday afternoon so Priya can join. I'll update the doc with the open questions before then.",
        words: 27, app: "Slack", profile: "Work", date: today
    } : null

    Component.onCompleted: if (Launch.arg("insights-state", "") === "off") Qt.callLater(() => Settings.set("insightsEnabled", false))
}
