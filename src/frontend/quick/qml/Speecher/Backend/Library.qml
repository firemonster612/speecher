pragma Singleton
import QtQuick
import Speecher.Wording

// What the Text pages show. Writing Profiles with their tones and cleanup
// levels and the three word lists are LibraryLists (host/QuickLibrary);
// this words them and derives what the pages show from them. Each list has
// one owner: speech services, refinement providers and runners are Models',
// and the Apps and App Types that pick a profile are Apps'.
LibraryLists {
    id: library

    // Refinement ---------------------------------------------------------

    // A refinement provider's model by its name in the provider's list, or
    // as given.
    function modelLabel(providerId, model) {
        const models = providerId === "openai" ? Models.openAiModels
                     : providerId === "anthropic" ? Models.anthropicModels : []
        return (models.find(m => m.id === model) || { label: model || "" }).label
    }
    // "Heard by Claude, cleaned up by Claude Sonnet 5.5": who hears a
    // dictation and who cleans it up, from a speech service, a refinement
    // provider id and the model it runs.
    function pipelineCaption(service, refiner, refinerModel) {
        const provider = Models.refinementProviders.find(p => p.id === refiner)
        const model = refiner !== "none" ? modelLabel(refiner, refinerModel) || (provider ? provider.label : refiner) : ""
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
    // A profile's icon: a built-in's own, a pen for one you added.
    readonly property var _profileIcons: ({ work: "briefcase", email: "mail", personal: "message-circle", ai_coding: "bot", other: "shapes" })
    function profileIcon(id) { return _profileIcons[id] || "pen-line" }
    // "New profile", or "New profile 2" and on while that name is taken.
    function newProfileName() {
        const taken = name => profiles.some(p => p.name.toLowerCase() === name.toLowerCase())
        let name = profilesText.newProfile
        for (let n = 2; taken(name); ++n) name = profilesText.newProfile + " " + n
        return name
    }
    function profileName(id) { const p = profiles.find(o => o.id === id); return p ? p.name : id }
    // "Medium cleanup, Casual, Spanish", or "No cleanup".
    function profileSummary(p) {
        if (p.cleanup === "none" && !p.language) return "No cleanup"
        return [levelLabel(p.cleanup) + " cleanup", p.tone === "none" ? "" : toneLabel(p.tone), p.language].filter(s => s).join(", ")
    }
    // The Apps and App Types that write with a profile, for its "Used in":
    // App Types whose own profile it is (Other apps' always counts), then
    // Apps that write with it now. Each lands on its row in Apps.
    function profileUsers(id) {
        const types = Apps.types.filter(o => o.profile === id).map(o => ({
            id: "apptype:" + o.id, name: Schema.apps.type(o.id).label, icon: "", typeIcon: Schema.apps.type(o.id).icon }))
        const apps = Apps.entries.filter(e => e.effective.profile === id).map(e => ({
            id: "appentry:" + e.id, name: e.name, icon: e.icon, typeIcon: "" }))
        return types.concat(apps)
    }
    // The question before deleting a profile, and what deleting it changes.
    function profileDeleteQuestion(name) { return "Delete " + name + "?" }
    function profileDeleteDetail(users) {
        return users.length ? users.map(u => u.name).join(", ") + " will follow their type, or use Other."
                            : "No app or app type uses it."
    }

    // The Writing Profiles page's fixed wording, its forms' and the profile
    // editor's.
    readonly property var profilesText: ({
        addProfile: "Add profile",
        newProfile: "New profile",
        unused: "No app or app type uses it yet.",
        name: "Name",
        newTone: "New tone",
        editTone: "Edit tone",
        addTone: "Add tone",
        toneNamePlaceholder: "Dry",
        toneInstruction: "Instruction",
        toneInstructionHelp: "Refinement follows this when a profile picks the tone.",
        toneInstructionPlaceholder: "Short sentences, no exclamation marks.",
        noTones: "No tones of your own",
        tonesEmpty: "Describe a voice once, such as short and deadpan, then pick it in any profile.",
        newLevel: "New cleanup level",
        editLevel: "Edit cleanup level",
        addLevel: "Add level",
        levelNamePlaceholder: "Meeting notes",
        basedOn: "Based on",
        customOnly: "Custom only",
        levelInstructions: "Instructions",
        levelInstructionsPlaceholder: "Keep bullet points as bullet points.",
        noLevels: "No cleanup levels of your own",
        levelsEmpty: "Start from Light, Medium or High and add your own rules, such as keeping bullet points.",
        toneSkipped: "Cleanup None skips refinement, so tone has no effect.",
        languageHelp: "Writes in this language, whatever you speak. Pick one or type any language, regional varieties too.",
        noRefiner: "Clean up my words is off on Refinement, so this profile's cleanup and output language have no effect.",
        everyProfile: "As for every profile.",
        custom: "Custom",
        default_: "Default",
        speech: "Speech",
        ownSpeechHelp: "Tried first, then the services under Speech to text.",
        defaultSpeechHelp: "The services under Speech to text, in their order.",
        speechModel: "Speech model",
        refinement: "Refinement",
        refinementSkipped: "Cleanup None skips refinement for this profile.",
        defaultRefinementHelp: "The services under Refinement, in their order.",
        refinementModel: "Refinement model"
    })
    // Cleanup None with an output language still translates, which takes
    // Light cleanup from the profile's provider.
    function translatesAtLight(provider) { return "None still translates, at Light cleanup, with " + provider + "." }
    // A profile's own refinement service, whose thinking and speed are its
    // entry's in Refinement's order.
    function ownRefinementHelp(provider) { return "Thinking and speed are " + provider + "'s, as Refinement's order sets them." }

    // Vocabulary ---------------------------------------------------------

    // "3 of 100 key terms go to Claude. Refinement uses all 7 terms."
    readonly property string keyTermsText: {
        const service = Models.service(((Settings.values.speechServices || [])[0] || { service: "" }).service).label
        const speech = speechTakesKeyTerms ? keyTermsSent + " of " + keyTermLimit + " key terms go to " + service + "."
                                           : service + " takes no key terms."
        const refinement = terms.length > refinementTermLimit ? "Refinement uses the first " + Words.num(refinementTermLimit) + "."
                                                              : "Refinement uses all " + Words.count(terms.length, "term", "terms") + "."
        return speech + " " + refinement
    }
    // Vocabulary's row on Speech to text, which the speech service reads too.
    readonly property string speechVocabularyText: terms.length ? keyTermsText
        : "Names and words you say often. Key terms go to the speech service as hints, so it hears them."
    readonly property string openVocabulary: "Open Vocabulary"
    // "3 of 5 in use.", or nothing without corrections.
    readonly property string correctionsInUseText: corrections.length
        ? corrections.filter(c => c.enabled).length + " of " + corrections.length + " in use." : ""

    // The Vocabulary page's fixed wording: its tabs' buttons, forms and
    // empty lists.
    readonly property var vocabularyText: ({
        addTerm: "Add term",
        importTerms: "Import CSV",
        importTermsTitle: "Import terms from a CSV file",
        importReplacementsTitle: "Import snippets from a JSON file",
        termsFiles: ["CSV files (*.csv)", "All files (*)"],
        replacementsFiles: ["JSON files (*.json)", "All files (*)"],
        noNewTerms: "That file has no terms you don't have.",
        noNewReplacements: "That file has no phrases you don't have.",
        newTerm: "New term",
        editTerm: "Edit term",
        term: "Term",
        everyProfile: "Every profile",
        searchTerms: "Search terms",
        keyTermTip: "Key term: sent to the speech service as a hint.",
        noTermsMatch: "No terms match",
        noTermsMatchHelp: "Try another spelling, or add it as a new term.",
        noTerms: "No vocabulary terms",
        noTermsHelp: "Add a name or product you say often, or import a list from a CSV file.",
        undoCorrection: "Undo last correction",
        editCorrection: "Edit correction",
        heard: "Heard",
        corrected: "Corrected",
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
    // The tabs: the list's name, then how many it holds once it holds any:
    // "Terms (7)", "Learned corrections", "Replacements & snippets (4)",
    // each named as its schema row is.
    function _tab(name, n) { return n ? name + " (" + Words.num(n) + ")" : name }
    readonly property string termsCount: _tab(Schema.row("vocabularyEntries").label, terms.length)
    readonly property string correctionsCount: _tab(Schema.row("learnedCorrections").label, corrections.length)
    readonly property string replacementsCount: _tab(Schema.row("bindingRules").label, replacements.length)
    // Each list's caption says which stage of a dictation reads it.
    readonly property string termsCaption: Schema.row("vocabularyEntries").help + " "
        + (terms.length ? keyTermsText : "Key terms go to the speech service as hints, and refinement reads every term.")
    readonly property string replacementsCaption: Schema.row("bindingRules").help
        + " They apply to what the speech service heard, and refinement leaves them as they are. Matching ignores case and treats punctuation as spaces."
    // A term's context, then where it came from: "Our dashboards. Imported.",
    // with nothing said for one you added.
    function termDetail(term) {
        const context = term.context && !/[.!?]$/.test(term.context) ? term.context + "." : term.context
        const source = term.source === "learned" ? "Learned from your edits." : term.source === "csv" ? "Imported." : ""
        return [context, source].filter(s => s).join(" ")
    }
    // After importing the list named: "Imported 2 terms.", or that nothing was new.
    function importedText(list, added) {
        const terms = list === "terms"
        if (!added) return terms ? vocabularyText.noNewTerms : vocabularyText.noNewReplacements
        return "Imported " + (terms ? Words.count(added, "term", "terms") : Words.count(added, "replacement", "replacements")) + "."
    }
    function importFailedText(error) { return "Nothing imported. " + error }
    // "Work and Chat only": the profiles a term is limited to.
    function termProfilesText(ids) { return Words.list(ids.map(profileName)) + " only" }
    function termUsesText(uses) { return Words.count(uses, "use", "uses") }
    // "Learned in org.kde.konsole, 92% sure", by its app's id, or in several apps
    // for one learned in more than one.
    function correctionSource(c) {
        return "Learned in " + (c.app || "several apps") + ", " + Math.round(c.confidence * 100) + "% sure"
    }

    // Refused edits ---------------------------------------------------------

    // Why LibraryLists refused a form's save, for the list it edits: the
    // reason add or update returned. Empty for none.
    readonly property var _refusals: ({
        profiles: { blank: "Give the profile a name.", taken: "Another profile has that name." },
        customTones: { blank: "Give the tone a name.", taken: "Another tone has that name.",
                       incomplete: "Add the instruction the tone follows." },
        customCleanupLevels: { blank: "Give the level a name.", taken: "Another cleanup level has that name.",
                               incomplete: "A Custom only level needs instructions." },
        terms: { blank: "Type the term.", taken: "That term is in the list already." },
        corrections: { blank: "Type what was heard.", taken: "Another correction fixes those words in that app.",
                       incomplete: "Type what it's corrected to." },
        replacements: { blank: "The phrase needs a letter or a number.", taken: "Another replacement has that phrase.",
                        incomplete: "Type what Speecher writes." },
        any: { missing: "It, or something it uses, was deleted meanwhile.", builtIn: "Built-in profiles keep their names.",
               invalid: "Speecher couldn't save this." }
    })
    function refusalText(list, reason) {
        return reason ? (_refusals[list] || {})[reason] || _refusals.any[reason] || "" : ""
    }
}
