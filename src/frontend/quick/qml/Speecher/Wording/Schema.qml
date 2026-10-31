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

// The settings layout and its wording, which belong to the UI: pages, and
// rows with their label, help, control kind and options. Every settings page
// and the search palette read rows from here by id; Settings holds their
// values under the same ids, and option ids are the values core stores.
//
// Row fields: id, page, section, kind (toggle, choice, slider, text, secret,
// shortcut, action, custom), label, help, options
// [{ id, label, help?, icon? }], control (segmented, select, cards, or
// language for the Spoken language's LanguageSelect) for choices, min/max/step/unit for sliders, placeholder for text, actionLabel
// and actionIcon for actions, keywords for search, and enabledWhen
// { id, equals } to grey a row out until another row has a value, and
// hiddenWhen { id, equals } to fold it away while another row has a value.
// needsBlur marks a row that is off and greyed, showing its disabledHelp,
// where Appearance.canBlur is false; its saved value stays.
// needs names what a row or an option needs of the computer, a System
// property such as hasVirtualKeyboard; without it, its page and search leave
// it out, and a choice left with one option leaves its row out (available,
// options).
// Custom rows may also carry wording for their own controls: emptyText and
// addLabel for lists, disabledHelp for help shown while the row is greyed
// out, and offHelp for help shown while its switch is off.
QtObject {
    readonly property var pages: Pages.pages
    // Rows the search palette offers before anything is typed.
    readonly property var suggestions: ["globalShortcut", "shortcutBehavior", "showLiveText", "pauseMedia"]
    readonly property var rows: [].concat(Shortcuts.rows, Microphone.rows, Speech.rows, Feedback.rows,
                                          Refinement.rows, Profiles.rows, Vocabulary.rows, Pasting.rows,
                                          Accounts.rows, Models.rows, General.rows, Updates.rows)
    // The app types Speecher sorts apps into, for paste rules and recognition.
    readonly property var appCategories: Pasting.categories
    // Each row's Since Version, by row id: { rowId: "0.2.1" }.
    readonly property var since: Updates.since

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
    // The pages under a page, in sidebar order.
    function subpages(id) { return pages.filter(p => p.parent === id) }
    // The pages a page sits under, outermost first: [Refinement] for Vocabulary.
    function ancestors(id) {
        const p = page(id)
        return p.parent ? ancestors(p.parent).concat([page(p.parent)]) : []
    }
    // Where a page is, as words: "Refinement › Vocabulary", or its title.
    function trail(id) { return ancestors(id).concat([page(id)]).map(p => p.title).join(" › ") }
    // Where a row is: its page's trail and its section, if it has one.
    function rowTrail(spec) { return trail(spec.page) + (spec.section ? ", " + spec.section : "") }

    // Whether this computer has the row; has(need) answers for a needs.
    function available(spec, has) {
        if (spec.needs && !has(spec.needs)) return false
        return spec.kind !== "choice" || options(spec, has).length > 1
    }
    // Whether the row that turns this one on (enabledWhen) is on, by the
    // values Settings holds.
    function rowEnabled(spec, values) {
        return !spec.enabledWhen || values[spec.enabledWhen.id] === spec.enabledWhen.equals
    }
    // The row's options this computer has.
    function options(spec, has) {
        return (spec.options || []).filter(o => !o.needs || has(o.needs))
    }

    // Ranks pages and the rows this computer has (available) against query.
    // Every word has to match somewhere; matches in the label count most,
    // then keywords, section and help. A subpage also matches its parent's
    // title, so searching for a page finds the pages under it; a subpage
    // and its rows carry the path to it as their trail.
    function search(query, limit, has) {
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
        // Ties keep the schema's order, so rows of one page read as they do
        // there.
        const results = []
        for (const p of pages) {
            const parentTitle = p.parent ? page(p.parent).title : ""
            // A page whose title the query names, give or take its ending
            // ("paste" for Pasting), comes before its own rows.
            const stems = p.title.toLowerCase().split(/\s+/)
            const named = words.every(w => w.length >= 4 && stems.some(t => t.startsWith(w.slice(0, -1))))
            const s = score([[p.title, 12], [parentTitle, 6], [p.keywords, 5]]) + (named ? 30 : 0)
            if (s > 0) results.push({ type: "page", id: p.id, page: p.id, label: p.title, icon: p.icon,
                                      trail: p.parent ? trail(p.id) : "", score: s + 4 })
        }
        for (const r of rows) {
            const optionText = options(r, has).map(o => o.label).join(" ")
            const s = score([[r.label, 10], [r.keywords, 6], [r.section, 4], [optionText, 3], [r.help, 2]])
            if (s > 0 && available(r, has)) results.push({ type: "row", id: r.id, page: r.page, label: r.label, icon: page(r.page).icon,
                                      trail: rowTrail(r), score: s })
        }
        results.forEach((r, i) => r.order = i)
        results.sort((a, b) => b.score - a.score || a.order - b.order)
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
    // The help of the option a choice row holds, such as what pressing the
    // shortcut does for shortcutBehavior.
    function valueHelp(spec, value) {
        const o = (spec.options || []).find(o => o.id === value)
        return o ? o.help || "" : ""
    }
}
