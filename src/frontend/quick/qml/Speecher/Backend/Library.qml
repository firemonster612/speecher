pragma Singleton
import QtQuick
import Speecher.Wording

// What the Text pages show. Writing Profiles with their tones and cleanup
// levels, the three word lists and the app matches are LibraryLists
// (host/QuickLibrary); this words them and derives what the pages show from
// them. Each list has one owner: speech services, refinement
// providers and runners are Models', paste rules are Settings rows, the
// fallback profile is the defaultWritingProfile setting, and app types are
// the pasting schema's.
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
    // The apps matched to a profile: your matches, then the built-in ones
    // yours don't override.
    function profileApps(id) {
        const own = recognitionRules
        const builtIn = builtInRecognitionRules.filter(b => !own.some(r => r.match === b.match))
        return own.concat(builtIn).filter(r => r.profile === id).map(r => r.match)
    }
    // Where a profile is used, given its apps and whether it is the one for
    // apps no match names: "Used in 22 apps." Matches are desktop IDs, not
    // names, so the line counts them rather than listing them.
    function profileUse(apps, isFallback) {
        const used = apps.length === 0 ? "" : "Used in " + Words.count(apps.length, "app", "apps") + "."
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
        newProfile: "New profile",
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
        languageHelp: "Translates what you say. Pick one or type any language, regional varieties too.",
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
    // "Default (Claude)": a profile's choice that follows the app's.
    function defaultChoice(label) { return "Default (" + label + ")" }
    // A profile's own refinement provider, whose settings live on Refinement.
    function ownRefinementHelp(provider) { return "Thinking and speed are " + provider + "'s settings under Refinement." }

    // Vocabulary ---------------------------------------------------------

    // "3 of 100 key terms go to Claude. Refinement uses all 7 terms."
    readonly property string keyTermsText: {
        const service = Models.service(Settings.values.speechProvider).label
        const speech = speechTakesKeyTerms ? keyTermsSent + " of " + keyTermLimit + " key terms go to " + service + "."
                                           : service + " takes no key terms."
        const refinement = terms.length > refinementTermLimit ? "Refinement uses the first " + Words.num(refinementTermLimit) + "."
                                                              : "Refinement uses all " + Words.count(terms.length, "term", "terms") + "."
        return speech + " " + refinement
    }
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
    // "Terms (7)", "Learned corrections", "Replacements (4)".
    function _tab(name, n) { return n ? name + " (" + Words.num(n) + ")" : name }
    readonly property string termsCount: _tab("Terms", terms.length)
    readonly property string correctionsCount: _tab("Learned corrections", corrections.length)
    readonly property string replacementsCount: _tab("Replacements", replacements.length)
    readonly property string replacementsCaption: Schema.row("bindingRules").help + " Matching ignores case and treats punctuation as spaces."
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

    // Pasting ------------------------------------------------------------

    readonly property var appCategories: Schema.appCategories
    function categoryLabel(id) { const c = appCategories.find(o => o.id === id); return c ? c.label : pastingText.automatic }

    // Application recognition's wording, on the App rules page.
    readonly property var pastingText: ({
        automatic: "Automatic",
        addMatch: "Add match",
        newMatch: "New app match",
        editMatch: "Edit app match",
        matchApp: "App name or ID",
        matchAppHelp: "Matches the application ID, application name, process name or accessible role.",
        app: "App",
        appType: "App type",
        appTypeHelp: "Picks the paste rule.",
        profile: "Writing profile",
        profileHelp: "Picks how the text is cleaned up.",
        deleteMatch: "Delete match",
        noMatches: "No matches of your own",
        noMatchesHelp: "The built-in matches below cover common apps. Add one for an app they miss.",
        builtInMatches: "Built-in matches",
        builtInMatchesHelp: "Common apps Speecher already knows. Yours above win when both match.",
        searchBuiltIn: "Search built-in matches",
        builtInMatchTip: "Click to add your own match for it."
    })

    // "2 of yours, 14 built in"
    readonly property string recognitionRulesText: recognitionRules.length + " of yours, " + builtInRecognitionRules.length + " built in"
    readonly property string builtInMatchesCount: Words.count(builtInRecognitionRules.length, "app", "apps")
    // What a built-in match sets: "Chat, Work profile".
    function builtInMatchSummary(rule) {
        return [rule.category ? categoryLabel(rule.category) : "",
                rule.profile ? profileName(rule.profile) + " profile" : ""].filter(s => s).join(", ")
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
        recognitionRules: { blank: "Type the app's name or ID.", taken: "You have a match for that app already.",
                            incomplete: "Choose an app type, a profile or both." },
        any: { missing: "It, or something it uses, was deleted meanwhile.", builtIn: "Built-in profiles keep their names." }
    })
    function refusalText(list, reason) {
        return reason ? (_refusals[list] || {})[reason] || _refusals.any[reason] || "" : ""
    }
}
