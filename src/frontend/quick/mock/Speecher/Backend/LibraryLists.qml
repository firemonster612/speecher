import QtQuick
import Speecher.Launch

// Mock of the lists Library builds on (host/QuickLibrary in the app): the
// Writing Profiles with your tones and cleanup levels, the vocabulary terms,
// learned corrections, replacements and app matches, with the same commands.
// Lists are plain arrays of objects and are replaced, never edited in place,
// so bindings see every change.
//
// Preview states: --library=empty empties every list, and
// --setting-<rowId>=<value> sets a settings row before the page loads.
QtObject {
    id: lists

    readonly property bool _empty: Launch.arg("library", "") === "empty"

    property var profiles: [
        { id: "work", name: "Work", builtIn: true, cleanup: "balanced", tone: "none",
          language: "", instructions: "", speech: "", speechModel: "", refinement: "", refinementModel: "" },
        { id: "email", name: "Email", builtIn: true, cleanup: "strong_polish", tone: "formal",
          language: "", instructions: "Keep it short and sign off with my first name.", speech: "", speechModel: "",
          refinement: "anthropic", refinementModel: "claude-sonnet-5-5" },
        { id: "personal", name: "Personal", builtIn: true, cleanup: "light_cleanup", tone: "casual",
          language: "", instructions: "", speech: "", speechModel: "", refinement: "", refinementModel: "" },
        { id: "ai_coding", name: "AI coding", builtIn: true, cleanup: "balanced", tone: "none",
          language: "", instructions: "Keep file names, flags and code identifiers exactly as said.", speech: "local",
          speechModel: "parakeet", refinement: "", refinementModel: "" },
        { id: "other", name: "Other", builtIn: true, cleanup: "balanced", tone: "none",
          language: "", instructions: "", speech: "", speechModel: "", refinement: "", refinementModel: "" }
    ].concat(_empty ? [] : [
        { id: "custom_spanish", name: "Spanish replies", builtIn: false, cleanup: "light_cleanup",
          tone: "casual", language: "Spanish", instructions: "", speech: "", speechModel: "", refinement: "", refinementModel: "" }
    ])
    property var customTones: _empty ? [] : [
        { id: "custom_dry", name: "Dry", instruction: "Short sentences, no exclamation marks, a little deadpan." }
    ]
    property var customCleanupLevels: _empty ? [] : [
        { id: "custom_notes", name: "Meeting notes", base: "balanced", instructions: "Turn lists of actions into bullet points, one owner per line." }
    ]

    property var terms: _empty ? [] : [
        { term: "Speecher", keyTerm: true, context: "", profiles: [], source: "manual", uses: 214 },
        { term: "Kubernetes", keyTerm: true, context: "The container platform, when I talk about clusters or deploys.",
          profiles: ["work", "ai_coding"], source: "manual", uses: 38 },
        { term: "Ngozi Okafor", keyTerm: true, context: "", profiles: [], source: "learned", uses: 17 },
        { term: "Qt Quick", keyTerm: true, context: "", profiles: ["ai_coding"], source: "manual", uses: 12 },
        { term: "pnpm", keyTerm: true, context: "", profiles: ["ai_coding"], source: "csv", uses: 4 },
        { term: "Lisbon office", keyTerm: false, context: "Our team in Portugal, not the city in general.",
          profiles: ["work"], source: "csv", uses: 6 },
        { term: "Wayland", keyTerm: false, context: "", profiles: [], source: "manual", uses: 0 }
    ]
    readonly property int keyTermLimit: 100
    readonly property int refinementTermLimit: 1000
    readonly property bool speechTakesKeyTerms: ["claude", "endpoint"].includes(Settings.values.speechProvider)
    readonly property int keyTermsSent: speechTakesKeyTerms ? Math.min(keyTermLimit, terms.filter(t => t.keyTerm).length) : 0

    // Newest first.
    property var corrections: _empty ? [] : [
        { id: "c1", heard: "speaker", corrected: "Speecher", app: "slack", confidence: 0.94, enabled: true },
        { id: "c2", heard: "cube control", corrected: "kubectl", app: "org.kde.konsole", confidence: 0.88, enabled: true },
        { id: "c3", heard: "in goes the", corrected: "Ngozi", app: "", confidence: 0.71, enabled: true },
        { id: "c4", heard: "get hub", corrected: "GitHub", app: "dev.zed.Zed", confidence: 0.63, enabled: false }
    ]

    property var replacements: _empty ? [] : [
        { phrase: "my address", replacement: "Rua Garrett 120, 1200-205 Lisboa" },
        { phrase: "sign off", replacement: "Thanks,\nEnzo" },
        { phrase: "standup template", replacement: "Yesterday:\nToday:\nBlocked on:\nSent on {date} at {time}" },
        { phrase: "shrug", replacement: "¯\\_(ツ)_/¯" }
    ]

    property var recognitionRules: _empty ? [] : [
        { match: "obsidian", category: "office", profile: "personal" },
        { match: "org.kde.neochat", category: "", profile: "personal" },
        { match: "element", category: "", profile: "custom_spanish" }
    ]
    readonly property var builtInRecognitionRules: [
        { match: "t3code", category: "ai_coding", profile: "ai_coding" }, { match: "chatgpt", category: "ai_coding", profile: "ai_coding" },
        { match: "codex", category: "ai_coding", profile: "ai_coding" }, { match: "cursor", category: "ai_coding", profile: "ai_coding" },
        { match: "zed", category: "ai_coding", profile: "ai_coding" }, { match: "claude code", category: "ai_coding", profile: "ai_coding" },
        { match: "konsole", category: "terminal", profile: "work" }, { match: "ghostty", category: "terminal", profile: "work" },
        { match: "kitty", category: "terminal", profile: "work" }, { match: "alacritty", category: "terminal", profile: "work" },
        { match: "thunderbird", category: "email", profile: "email" }, { match: "kmail", category: "email", profile: "email" },
        { match: "firefox", category: "browser", profile: "" }, { match: "chrome", category: "browser", profile: "" },
        { match: "libreoffice", category: "office", profile: "work" }, { match: "kate", category: "code_editor", profile: "work" },
        { match: "code", category: "code_editor", profile: "work" }, { match: "jetbrains", category: "code_editor", profile: "work" },
        { match: "signal", category: "", profile: "personal" }, { match: "discord", category: "", profile: "personal" },
        { match: "telegram", category: "", profile: "personal" }, { match: "slack", category: "", profile: "work" },
        { match: "linear", category: "", profile: "work" }, { match: "notion", category: "", profile: "work" }
    ]

    // Ids for what you add: given once, never reused, kept when renamed.
    property int _lastId: 0
    function newId(prefix) { return prefix + "_" + (++_lastId) }

    // The mock keeps what the app refuses out only where a page would show
    // the difference: blank names and repeated terms.
    function add(list, item) {
        if (list === "terms" && terms.some(t => t.term.toLowerCase() === item.term.toLowerCase())) return false
        const defaults = list === "terms" ? { source: "manual", uses: 0 } : list === "profiles" ? { builtIn: false } : {}
        lists[list] = lists[list].concat([Object.assign(defaults, item)])
        return true
    }
    function update(list, index, patch) {
        const next = lists[list].slice()
        next[index] = Object.assign({}, next[index], patch)
        lists[list] = next
        return true
    }
    // Returns the token undo takes: here, the function that puts it back.
    function remove(list, index) {
        const next = lists[list].slice()
        const gone = next.splice(index, 1)[0]
        lists[list] = next
        return () => {
            const back = lists[list].slice()
            back.splice(index, 0, gone)
            lists[list] = back
        }
    }
    function undo(token) { if (token) token() }

    // Deleting a profile, tone or level also drops what pointed at it, so
    // nothing names something that is gone.
    function removeProfile(id) {
        profiles = profiles.filter(p => p.id !== id)
        recognitionRules = recognitionRules.map(r => r.profile === id ? Object.assign({}, r, { profile: "" }) : r)
                                           .filter(r => r.category || r.profile)
        terms = terms.map(t => t.profiles.includes(id) ? Object.assign({}, t, { profiles: t.profiles.filter(p => p !== id) }) : t)
        if (Settings.values.defaultWritingProfile === id) Settings.set("defaultWritingProfile", "other")
    }
    function removeTone(id) { return _removeChoice("customTones", "tone", id, "none") }
    function removeLevel(id) {
        const base = customCleanupLevels.find(l => l.id === id).base
        return _removeChoice("customCleanupLevels", "cleanup", id, base === "custom_only" ? "balanced" : base)
    }
    // Removes entry id from the list named and moves every profile's field
    // that chose it to instead. The undo puts the entry back where it was
    // and points back only the fields that still hold instead, so edits made
    // while its toast is up stay.
    function _removeChoice(list, field, id, instead) {
        const putBack = remove(list, lists[list].findIndex(e => e.id === id))
        const moved = profiles.filter(p => p[field] === id).map(p => p.id)
        profiles = profiles.map(p => moved.includes(p.id) ? Object.assign({}, p, { [field]: instead }) : p)
        return () => {
            putBack()
            profiles = profiles.map(p => moved.includes(p.id) && p[field] === instead ? Object.assign({}, p, { [field]: id }) : p)
        }
    }
    function undoLastCorrection() { return corrections.length ? remove("corrections", 0) : null }

    // Adds the terms of the CSV named; the mock reads one.
    function importTerms(path) {
        const incoming = [
            { term: "Grafana", keyTerm: true, context: "", profiles: [], source: "csv", uses: 0 },
            { term: "Priya Raman", keyTerm: true, context: "", profiles: [], source: "csv", uses: 0 }
        ].filter(n => !terms.some(t => t.term === n.term))
        terms = terms.concat(incoming)
        return { added: incoming.length, error: "" }
    }
    // Adds the snippets of the JSON file named; the mock reads one.
    function importReplacements(path) {
        const incoming = [
            { phrase: "my phone", replacement: "+351 912 345 678" },
            { phrase: "meeting link", replacement: "https://meet.example.com/enzo" }
        ].filter(n => !replacements.some(r => r.phrase === n.phrase))
        replacements = replacements.concat(incoming)
        return { added: incoming.length, error: "" }
    }

    // Later, so a binding that first touched Library isn't still reading
    // Settings when the values change.
    function _applySettingArgs() {
        for (const a of Qt.application.arguments) {
            const m = /^--setting-([^=]+)=(.*)$/.exec(a)
            if (m) Settings.set(m[1], m[2] === "true" ? true : m[2] === "false" ? false : m[2] === "" ? [] : m[2])
        }
    }
    Component.onCompleted: Qt.callLater(_applySettingArgs)
}
