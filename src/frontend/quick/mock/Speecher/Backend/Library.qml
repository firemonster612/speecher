pragma Singleton
import QtQuick
import Speecher.Wording
import Speecher.Launch

// Placeholder: the app ships this mock until its wiring PR replaces it.
// Mock of what the Text pages show: Writing Profiles with their tones and
// cleanup levels, the three word lists, and the app recognition rules. The
// refinement providers and runners are Models'. Lists are plain arrays of
// objects and are replaced, never edited in place, so bindings see every
// change. Each list has one owner: speech services are Models', the fallback
// profile is the defaultWritingProfile setting, and app types are the
// pasting schema's.
//
// Preview states: --library=empty empties every list, and
// --setting-<rowId>=<value> sets a settings row before the page loads.
QtObject {
    id: library

    readonly property bool _empty: Launch.arg("library", "") === "empty"

    // Refinement ---------------------------------------------------------

    // The model a refinement provider refines with, as its settings name it.
    function modelLabel(providerId) {
        const v = Settings.values
        const listed = (models, id) => (models.find(m => m.id === id) || { label: id || "" }).label
        return providerId === "openai" ? listed(Models.openAiModels, v.openAiModel)
             : providerId === "anthropic" ? listed(Models.anthropicModels, v.anthropicModel)
             : providerId === "local" ? v.localRunnerModel || ""
             : providerId === "endpoint" ? v.refinementEndpointModel || "" : ""
    }
    // "Heard by Claude Voice, cleaned up by Claude Sonnet 5.5": who hears a
    // dictation and who cleans it up, from a speech service and a refinement
    // provider id.
    function pipelineCaption(service, refiner) {
        const provider = Models.refinementProviders.find(p => p.id === refiner)
        const model = refiner !== "none" ? modelLabel(refiner) || (provider ? provider.label : refiner) : ""
        return "Heard by " + Models.service(service).label + (model ? ", cleaned up by " + model : "")
    }

    // Writing Profiles ---------------------------------------------------

    readonly property var cleanupLevels: [
        { id: "none", label: "None", help: "Pastes your words as spoken, unless the profile translates." },
        { id: "light_cleanup", label: "Light", help: "Fixes punctuation, capitals and clear mistakes, and keeps your wording." },
        { id: "balanced", label: "Medium", help: "Also removes filler words and false starts, and adds paragraphs." },
        { id: "strong_polish", label: "High", help: "Also rewrites for clarity, flow and organization, keeping the facts." }
    ]
    // The same sentence at each built-in level, for the profile editor's preview.
    readonly property var cleanupSamples: ({
        none: "so um can you send me the the numbers from last quarter before friday thanks",
        light_cleanup: "So, um, can you send me the numbers from last quarter before Friday? Thanks.",
        balanced: "Can you send me the numbers from last quarter before Friday? Thanks.",
        strong_polish: "Could you send me last quarter's numbers by Friday? Thank you."
    })
    readonly property var tones: [
        { id: "none", label: "No tone", help: "Keeps the tone you spoke in." },
        { id: "formal", label: "Formal", help: "Professional and polished." },
        { id: "casual", label: "Casual", help: "Relaxed and friendly." },
        { id: "very_casual", label: "Very casual", help: "Loose and conversational, like a text to a friend." },
        { id: "excited", label: "Excited", help: "Upbeat and enthusiastic." },
        { id: "gen_z", label: "Gen Z", help: "Gen Z slang and phrasing." }
    ]
    property var customTones: _empty ? [] : [
        { id: "custom_dry", name: "Dry", instruction: "Short sentences, no exclamation marks, a little deadpan." }
    ]
    property var customCleanupLevels: _empty ? [] : [
        { id: "custom_notes", name: "Meeting notes", base: "balanced", instructions: "Turn lists of actions into bullet points, one owner per line." }
    ]
    // Built-in and custom levels and tones as one choice each. A custom
    // level carries the built-in level it starts from as base.
    readonly property var levelOptions: cleanupLevels.concat(customCleanupLevels.map(l => ({
        id: l.id, label: l.name, base: l.base,
        help: l.base === "custom_only" ? "Your instructions only." : levelLabel(l.base) + ", plus your own instructions." })))
    readonly property var toneOptions: tones.concat(customTones.map(t => ({ id: t.id, label: t.name, help: t.instruction })))
    function levelLabel(id) {
        const builtIn = cleanupLevels.find(l => l.id === id)
        if (builtIn) return builtIn.label
        const custom = customCleanupLevels.find(l => l.id === id)
        return custom ? custom.name : "Medium"
    }
    function toneLabel(id) { const t = toneOptions.find(o => o.id === id); return t ? t.label : "No tone" }
    // What a custom level does, in its list: "Based on Medium. Turn lists…"
    function levelDetail(l) {
        return (l.base === "custom_only" ? "Your instructions only. " : "Based on " + levelLabel(l.base) + ". ") + l.instructions
    }
    // Languages a profile's output language suggests.
    readonly property var outputLanguages: ["Spanish", "French", "German", "Portuguese", "Italian", "Japanese", "Chinese"]
    property string additionalInstructions: _empty ? "" : "Write numbers as digits. Spell Speecher with one capital S."

    property var profiles: [
        { id: "work", name: "Work", icon: "briefcase", builtIn: true, cleanup: "balanced", tone: "none",
          language: "", instructions: "", speech: "", speechModel: "", refinement: "", refinementModel: "" },
        { id: "email", name: "Email", icon: "mail", builtIn: true, cleanup: "strong_polish", tone: "formal",
          language: "", instructions: "Keep it short and sign off with my first name.", speech: "", speechModel: "",
          refinement: "anthropic", refinementModel: "claude-sonnet-5-5" },
        { id: "personal", name: "Personal", icon: "message-circle", builtIn: true, cleanup: "light_cleanup", tone: "casual",
          language: "", instructions: "", speech: "", speechModel: "", refinement: "", refinementModel: "" },
        { id: "ai_coding", name: "AI coding", icon: "bot", builtIn: true, cleanup: "balanced", tone: "none",
          language: "", instructions: "Keep file names, flags and code identifiers exactly as said.", speech: "local",
          speechModel: "parakeet", refinement: "", refinementModel: "" },
        { id: "other", name: "Other", icon: "shapes", builtIn: true, cleanup: "balanced", tone: "none",
          language: "", instructions: "", speech: "", speechModel: "", refinement: "", refinementModel: "" }
    ].concat(_empty ? [] : [
        { id: "custom_spanish", name: "Spanish replies", icon: "languages", builtIn: false, cleanup: "light_cleanup",
          tone: "casual", language: "Spanish", instructions: "", speech: "", speechModel: "", refinement: "", refinementModel: "" }
    ])
    function profileName(id) { const p = profiles.find(o => o.id === id); return p ? p.name : id }
    // "Medium cleanup, Casual, Spanish", or "No cleanup".
    function profileSummary(p) {
        if (p.cleanup === "none" && !p.language) return "No cleanup"
        return [levelLabel(p.cleanup) + " cleanup", p.tone === "none" ? "" : toneLabel(p.tone), p.language].filter(s => s).join(", ")
    }
    // The apps matched to a profile: your matches, then the built-in ones
    // yours don't override.
    function profileApps(id) {
        const own = recognitionRules
        const builtIn = builtInRecognitionRules.filter(b => !own.some(r => r.match === b.match))
        return own.concat(builtIn).filter(r => r.profile === id).map(r => r.name || r.match)
    }
    // Where a profile is used, given its apps and whether it is the one for
    // apps no match names: "Used in Slack, Linear and 2 more apps."
    function profileUse(apps, isFallback) {
        const more = apps.length - 4
        const used = apps.length === 0 ? "" : "Used in " + (more > 0 ? apps.slice(0, 4).join(", ") + " and " + Words.count(more, "more app", "more apps") : Words.list(apps)) + "."
        if (isFallback) return used.length ? used + " Also used for apps no match names." : "Used for every app no match names."
        return used.length ? used : "No apps use it yet."
    }
    // The question before deleting a profile, and what deleting it changes.
    function profileDeleteQuestion(name) { return "Delete " + name + "?" }
    function profileDeleteDetail(apps, isFallback) {
        return (apps.length ? Words.count(apps.length, "app match uses", "app matches use") + " it and will lose it."
                            : "No app matches use it.") + (isFallback ? " Unrecognized apps will use Other." : "")
    }

    // The Writing Profiles page's fixed wording, its forms' and the profile
    // editor's.
    readonly property var profilesText: ({
        addProfile: "Add profile",
        appMatches: "App matches",
        appMatchesHelp: "Which apps use which profile. Your own matches come first.",
        name: "Name",
        newTone: "New tone",
        editTone: "Edit tone",
        addTone: "Add tone",
        toneNamePlaceholder: "Dry",
        toneInstruction: "Instruction",
        toneInstructionHelp: "Refinement follows this when a profile picks the tone.",
        toneInstructionPlaceholder: "Short sentences, no exclamation marks.",
        tonesEmpty: "Describe a voice once, such as short and deadpan, then pick it in any profile.",
        newLevel: "New cleanup level",
        editLevel: "Edit cleanup level",
        addLevel: "Add level",
        levelNamePlaceholder: "Meeting notes",
        basedOn: "Based on",
        customOnly: "Custom only",
        levelInstructions: "Instructions",
        levelInstructionsPlaceholder: "Keep bullet points as bullet points.",
        levelsEmpty: "Start from Light, Medium or High and add your own rules, such as keeping bullet points.",
        toneSkipped: "Cleanup None skips refinement, so tone has no effect.",
        languageHelp: "Translates what you say.",
        custom: "Custom",
        default_: "Default",
        speech: "Speech",
        ownSpeechHelp: "Falls back to the services under Speech to text.",
        defaultSpeechHelp: "Set under Speech to text, with its fallbacks.",
        speechModel: "Speech model",
        refinement: "Refinement",
        refinementSkipped: "Cleanup None skips refinement for this profile.",
        defaultRefinementHelp: "Set under Refinement, with its fallbacks.",
        refinementModel: "Refinement model",
        defaultModel: "Default model"
    })
    // "Default (Claude Voice)": a profile's choice that follows the app's.
    function defaultChoice(label) { return "Default (" + label + ")" }
    // A profile's own refinement provider, whose settings live on Refinement.
    function ownRefinementHelp(provider) { return "Thinking and speed are " + provider + "'s settings under Refinement." }

    // Ids for what you add: given once, never reused, kept when renamed.
    property int _lastId: 0
    function newId(prefix) { return prefix + "_" + (++_lastId) }

    // Deleting a profile, tone or level also drops what pointed at it, so
    // nothing names something that is gone. Deleting a tone or level
    // returns a function that puts back what the deletion changed, for an
    // Undo.
    function removeProfile(id) {
        profiles = profiles.filter(p => p.id !== id)
        recognitionRules = recognitionRules.map(r => r.profile === id ? Object.assign({}, r, { profile: "" }) : r)
                                           .filter(r => r.category || r.profile)
        terms = terms.map(t => t.profiles.includes(id) ? Object.assign({}, t, { profiles: t.profiles.filter(p => p !== id) }) : t)
        if (Settings.values.defaultWritingProfile === id) Settings.set("defaultWritingProfile", "other")
        if (Transcription.options.profile === id) Transcription.setOption("profile", Settings.values.defaultWritingProfile)
    }
    function removeTone(id) { return _removeChoice("customTones", "tone", id, "none") }
    function removeLevel(id) {
        const base = customCleanupLevels.find(l => l.id === id).base
        return _removeChoice("customCleanupLevels", "cleanup", id, base === "custom_only" ? "balanced" : base)
    }
    // Removes entry id from the list named and moves every profile's and
    // Transcribe's field that chose it to instead. The Undo puts the entry
    // back where it was and points back only the fields that still hold
    // instead, so edits made while its toast is up stay.
    function _removeChoice(list, field, id, instead) {
        const putBack = remove(list, library[list].findIndex(e => e.id === id))
        const moved = profiles.filter(p => p[field] === id).map(p => p.id)
        const transcribe = Transcription.options[field] === id
        profiles = profiles.map(p => moved.includes(p.id) ? Object.assign({}, p, { [field]: instead }) : p)
        if (transcribe) Transcription.setOption(field, instead)
        return () => {
            putBack()
            profiles = profiles.map(p => moved.includes(p.id) && p[field] === instead ? Object.assign({}, p, { [field]: id }) : p)
            if (transcribe && Transcription.options[field] === instead) Transcription.setOption(field, id)
        }
    }

    // Vocabulary ---------------------------------------------------------

    property var terms: _empty ? [] : [
        { term: "Speecher", keyTerm: true, context: "", profiles: [], source: "Added", uses: 214 },
        { term: "Kubernetes", keyTerm: true, context: "The container platform, when I talk about clusters or deploys.",
          profiles: ["work", "ai_coding"], source: "Added", uses: 38 },
        { term: "Ngozi Okafor", keyTerm: true, context: "", profiles: [], source: "Learned", uses: 17 },
        { term: "Qt Quick", keyTerm: true, context: "", profiles: ["ai_coding"], source: "Added", uses: 12 },
        { term: "Lisbon office", keyTerm: false, context: "Our team in Portugal, not the city in general.",
          profiles: ["work"], source: "Imported", uses: 6 },
        { term: "pnpm", keyTerm: true, context: "", profiles: ["ai_coding"], source: "Imported", uses: 4 },
        { term: "Wayland", keyTerm: false, context: "", profiles: [], source: "Added", uses: 0 }
    ]
    readonly property int keyTermLimit: 100
    // "3 of 100 key terms go to Claude Voice. Refinement uses all 7 terms."
    readonly property string keyTermsText: terms.filter(t => t.keyTerm).length + " of " + keyTermLimit + " key terms go to "
        + Models.service(Settings.values.speechProvider).label + ". Refinement uses all " + Words.count(terms.length, "term", "terms") + "."
    // Adds the terms in a CSV the person picked; the mock picks one.
    function importTerms() {
        const incoming = [
            { term: "Grafana", keyTerm: true, context: "", profiles: [], source: "Imported", uses: 0 },
            { term: "Priya Raman", keyTerm: true, context: "", profiles: [], source: "Imported", uses: 0 }
        ]
        terms = terms.concat(incoming.filter(n => !terms.some(t => t.term === n.term)))
    }

    // Newest first.
    property var corrections: _empty ? [] : [
        { id: "c1", heard: "speaker", corrected: "Speecher", app: "Slack", confidence: 0.94, enabled: true },
        { id: "c2", heard: "cube control", corrected: "kubectl", app: "Konsole", confidence: 0.88, enabled: true },
        { id: "c3", heard: "in goes the", corrected: "Ngozi", app: "Thunderbird", confidence: 0.71, enabled: true },
        { id: "c4", heard: "get hub", corrected: "GitHub", app: "Zed", confidence: 0.63, enabled: false }
    ]

    function undoLastCorrection() { corrections = corrections.slice(1) }
    // "3 of 5 in use.", or nothing without corrections.
    readonly property string correctionsInUseText: corrections.length
        ? corrections.filter(c => c.enabled).length + " of " + corrections.length + " in use." : ""

    property var replacements: _empty ? [] : [
        { phrase: "my address", replacement: "Rua Garrett 120, 1200-205 Lisboa" },
        { phrase: "sign off", replacement: "Thanks,\nEnzo" },
        { phrase: "standup template", replacement: "Yesterday:\nToday:\nBlocked on:\nSent on {date} at {time}" },
        { phrase: "shrug", replacement: "¯\\_(ツ)_/¯" }
    ]
    // Adds the snippets in a JSON file the person picked; the mock picks one.
    function importReplacements() {
        const incoming = [
            { phrase: "my phone", replacement: "+351 912 345 678" },
            { phrase: "meeting link", replacement: "https://meet.example.com/enzo" }
        ]
        replacements = replacements.concat(incoming.filter(n => !replacements.some(r => r.phrase === n.phrase)))
    }

    // The Vocabulary page's fixed wording: its tabs' buttons, forms and
    // empty lists.
    readonly property var vocabularyText: ({
        addTerm: "Add term",
        importTerms: "Import CSV",
        newTerm: "New term",
        editTerm: "Edit term",
        term: "Term",
        everyProfile: "Every profile",
        searchTerms: "Search terms",
        keyTermTip: "Key term: sent to the speech service as a hint.",
        noTermsMatch: "No terms match",
        noTermsMatchHelp: "Try another spelling, or add it as a new term.",
        noTerms: "No vocabulary terms",
        noTermsHelp: "Add names and words the speech service should spell your way, or import a CSV.",
        undoCorrection: "Undo last correction",
        correctionsNeedAccessibility: "Turn on desktop accessibility to learn corrections after pasting.",
        noCorrections: "No learned corrections yet",
        noCorrectionsHelp: "When you fix a dictated word the same way more than once, the correction appears here.",
        addReplacement: "Add replacement",
        importReplacements: "Import snippets",
        newReplacement: "New replacement",
        editReplacement: "Edit replacement",
        whenYouSay: "When you say",
        speecherWrites: "Speecher writes",
        speecherWritesHelp: "Several lines are fine. {date} and {time} become today's date and the time.",
        replacementPlaceholder: "Sent on {date} at {time}",
        noReplacements: "No replacements or snippets",
        noReplacementsHelp: "Add a spoken phrase and the exact text to put in its place, or import snippets from JSON."
    })
    // The tabs: "7 terms", "5 learned corrections", "4 replacements and snippets".
    readonly property string termsCount: Words.count(terms.length, "term", "terms")
    readonly property string correctionsCount: Words.count(corrections.length, "learned correction", "learned corrections")
    readonly property string replacementsCount: Words.count(replacements.length, "replacement or snippet", "replacements and snippets")
    readonly property string replacementsCaption: Schema.row("bindingRules").help + " Matching ignores case and treats punctuation as spaces."
    // Where a term came from, after its context: "Imported.", or nothing for one you added.
    function termSource(term) { return term.source === "Learned" ? "Learned from your edits." : term.source === "Imported" ? "Imported." : "" }
    // "Work and Chat only": the profiles a term is limited to.
    function termProfilesText(ids) { return Words.list(ids.map(profileName)) + " only" }
    function termUsesText(uses) { return Words.count(uses, "use", "uses") }
    // "Learned in Slack, 92% sure"
    function correctionSource(c) { return "Learned in " + c.app + ", " + Math.round(c.confidence * 100) + "% sure" }

    // Pasting ------------------------------------------------------------

    readonly property var appCategories: Schema.appCategories
    function categoryLabel(id) { const c = appCategories.find(o => o.id === id); return c ? c.label : pastingText.automatic }

    // Application recognition's wording, on the Pasting page.
    readonly property var pastingText: ({
        automatic: "Automatic",
        addMatch: "Add match",
        newMatch: "New app match",
        matchApp: "App name or ID",
        matchAppHelp: "Matches the application ID, application name, process name or accessible role.",
        app: "App",
        appType: "App type",
        appTypeHelp: "Picks the paste rule.",
        profile: "Writing Profile",
        profileHelp: "Picks how the text is cleaned up.",
        deleteMatch: "Delete match",
        noMatches: "No matches of your own",
        noMatchesHelp: "The built-in matches below cover common apps. Add one for an app they miss.",
        builtInMatches: "Built-in matches",
        builtInMatchesHelp: "Common apps Speecher already knows. Yours above win when both match.",
        searchBuiltIn: "Search built-in matches",
        builtInMatchTip: "Click to add your own match for it."
    })

    property var recognitionRules: _empty ? [] : [
        { match: "obsidian", category: "office", profile: "personal" },
        { match: "org.kde.neochat", category: "", profile: "personal" },
        { match: "element", category: "", profile: "custom_spanish" }
    ]
    // "2 of yours, 14 built in"
    readonly property string recognitionRulesText: recognitionRules.length + " of yours, " + builtInRecognitionRules.length + " built in"
    readonly property string builtInMatchesCount: Words.count(builtInRecognitionRules.length, "app", "apps")
    // What a built-in match sets: "Chat, Work profile".
    function builtInMatchSummary(rule) {
        return [rule.category ? categoryLabel(rule.category) : "",
                rule.profile ? profileName(rule.profile) + " profile" : ""].filter(s => s).join(", ")
    }
    // Matches Speecher ships with; name is how the app shows in a profile.
    readonly property var builtInRecognitionRules: [
        { match: "t3code", name: "T3 Code", category: "ai_coding", profile: "ai_coding" }, { match: "chatgpt", name: "ChatGPT", category: "ai_coding", profile: "ai_coding" },
        { match: "codex", name: "Codex", category: "ai_coding", profile: "ai_coding" }, { match: "cursor", name: "Cursor", category: "ai_coding", profile: "ai_coding" },
        { match: "zed", name: "Zed", category: "ai_coding", profile: "ai_coding" }, { match: "claude code", name: "Claude Code", category: "ai_coding", profile: "ai_coding" },
        { match: "konsole", name: "Konsole", category: "terminal", profile: "work" }, { match: "ghostty", name: "Ghostty", category: "terminal", profile: "work" },
        { match: "kitty", name: "Kitty", category: "terminal", profile: "work" }, { match: "alacritty", name: "Alacritty", category: "terminal", profile: "work" },
        { match: "thunderbird", name: "Thunderbird", category: "email", profile: "email" }, { match: "kmail", name: "KMail", category: "email", profile: "email" },
        { match: "firefox", name: "Firefox", category: "browser", profile: "" }, { match: "chrome", name: "Chrome", category: "browser", profile: "" },
        { match: "libreoffice", name: "LibreOffice", category: "office", profile: "work" }, { match: "kate", name: "Kate", category: "code_editor", profile: "work" },
        { match: "code", name: "VS Code", category: "code_editor", profile: "work" }, { match: "jetbrains", name: "JetBrains IDEs", category: "code_editor", profile: "work" },
        { match: "signal", name: "Signal", category: "", profile: "personal" }, { match: "discord", name: "Discord", category: "", profile: "personal" },
        { match: "telegram", name: "Telegram", category: "", profile: "personal" }, { match: "slack", name: "Slack", category: "", profile: "work" },
        { match: "linear", name: "Linear", category: "", profile: "work" }, { match: "notion", name: "Notion", category: "", profile: "work" }
    ]

    // Editing ------------------------------------------------------------
    // One set of edits for every list above, by property name.

    function add(list, item) { library[list] = library[list].concat([item]) }
    function update(list, index, item) {
        const next = library[list].slice()
        next[index] = Object.assign({}, next[index], item)
        library[list] = next
    }
    // Returns a function that puts the entry back where it was, for an Undo.
    function remove(list, index) {
        const next = library[list].slice()
        const gone = next.splice(index, 1)[0]
        library[list] = next
        return () => {
            const back = library[list].slice()
            back.splice(index, 0, gone)
            library[list] = back
        }
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
