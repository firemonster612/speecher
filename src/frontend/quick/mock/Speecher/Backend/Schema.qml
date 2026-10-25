pragma Singleton
import QtQuick
import "schema/pages.js" as Pages
import "schema/shortcuts.js" as Shortcuts
import "schema/microphone.js" as Microphone
import "schema/speech.js" as Speech
import "schema/feedback.js" as Feedback
import "schema/refinement.js" as Refinement
import "schema/profiles.js" as Profiles
import "schema/vocabulary.js" as Vocabulary
import "schema/pasting.js" as Pasting
import "schema/accounts.js" as Accounts
import "schema/models.js" as Models
import "schema/general.js" as General
import "schema/updates.js" as Updates

// Mock of the settings schema core will provide: pages, and rows with their
// label, help, control kind and options. Every settings page and the search
// palette read rows from here by id.
//
// Row fields: id, page, section, kind (toggle, choice, slider, text, secret,
// shortcut, action, custom), label, help, value (the default), options
// [{ id, label, help?, icon? }], control (segmented, select, cards) for
// choices, min/max/step/unit for sliders, placeholder for text, actionLabel
// and actionIcon for actions, keywords for search, and enabledWhen
// { id, equals } to grey a row out until another row has a value, and
// hiddenWhen { id, equals } to fold it away while another row has a value.
// Custom rows may also carry wording for their own controls: emptyText and
// addLabel for lists, disabledHelp for help shown while the row is greyed
// out, and offHelp for help shown while its switch is off.
QtObject {
    readonly property var groups: Pages.groups
    readonly property var pages: Pages.pages
    // Rows the search palette offers before anything is typed.
    readonly property var suggestions: ["globalShortcut", "shortcutBehavior", "showLiveText", "pauseMedia"]
    readonly property var rows: [].concat(Shortcuts.rows, Microphone.rows, Speech.rows, Feedback.rows,
                                          Refinement.rows, Profiles.rows, Vocabulary.rows, Pasting.rows,
                                          Accounts.rows, Models.rows, General.rows, Updates.rows)

    readonly property var _rowsById: {
        const map = {}
        for (const r of rows) map[r.id] = r
        return map
    }
    readonly property var _pagesById: {
        const map = {}
        for (const p of pages) map[p.id] = p
        return map
    }

    function row(id) {
        return _rowsById[id] || { id: id, page: "", label: id, help: "This row is not in the schema yet.", kind: "missing" }
    }
    function page(id) { return _pagesById[id] || { id: id, title: id, icon: "circle" } }

    // Ranks pages and rows against query. Every word has to match somewhere;
    // matches in the label count most, then keywords, section and help.
    function search(query, limit) {
        const words = query.toLowerCase().split(/\s+/).filter(w => w.length > 0)
        if (!words.length) return []
        const score = (fields) => {
            let total = 0
            for (const w of words) {
                let best = 0
                for (const [text, weight] of fields) {
                    const t = (text || "").toLowerCase()
                    const i = t.indexOf(w)
                    if (i < 0) continue
                    const atWord = i === 0 || /[\s\-&(]/.test(t[i - 1])
                    best = Math.max(best, weight * (atWord ? 1.6 : 1) * (i === 0 ? 1.3 : 1))
                }
                if (best === 0) return 0
                total += best
            }
            return total
        }
        const results = []
        for (const p of pages) {
            const s = score([[p.title, 12], [p.keywords, 5]])
            if (s > 0) results.push({ type: "page", id: p.id, page: p.id, label: p.title, icon: p.icon, score: s + 4 })
        }
        for (const r of rows) {
            const p = page(r.page)
            const optionText = (r.options || []).map(o => o.label).join(" ")
            const s = score([[r.label, 10], [r.keywords, 6], [r.section, 4], [optionText, 3], [r.help, 2]])
            if (s > 0) results.push({ type: "row", id: r.id, page: r.page, label: r.label, icon: p.icon,
                                      trail: p.title + (r.section ? ", " + r.section : ""), score: s })
        }
        results.sort((a, b) => b.score - a.score)
        return results.slice(0, limit || 12)
    }

    // A row's value as words: an option's label, or a slider's number with
    // its unit ("40 words", but "40%").
    function valueLabel(spec, value) {
        if (spec.kind === "slider") {
            const unit = spec.unit || ""
            return Math.round(value) + (unit === "%" || unit === "" ? unit : " " + unit)
        }
        const o = (spec.options || []).find(o => o.id === value)
        return o ? o.label : ""
    }
}
