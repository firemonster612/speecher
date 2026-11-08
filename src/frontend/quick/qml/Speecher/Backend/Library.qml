pragma Singleton
import QtQuick
import Speecher.Wording

// What the Text pages show. Writing Profiles with their tones and cleanup
// levels and the Vocabulary are LibraryLists (host/QuickLibrary);
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

    readonly property var spellings: vocabulary.filter(e => e.kind === "spelling")
    // "3 of 100 key terms go to Claude. Refinement uses all 7 spellings."
    readonly property string keyTermsText: {
        const service = Models.service(((Settings.values.speechServices || [])[0] || { service: "" }).service).label
        const speech = speechTakesKeyTerms ? keyTermsSent + " of " + keyTermLimit + " key terms go to " + service + "."
                                           : service + " takes no key terms."
        const refinement = spellings.length > refinementTermLimit ? "Refinement uses the first " + Words.num(refinementTermLimit) + "."
                                                                  : "Refinement uses all " + Words.count(spellings.length, "spelling", "spellings") + "."
        return speech + " " + refinement
    }
    // Vocabulary's row on Speech to text, which the speech service reads too.
    readonly property string speechVocabularyText: spellings.length ? keyTermsText
        : "Names and words you say often. Key terms go to the speech service as hints, so it hears them."
    readonly property string openVocabulary: "Open Vocabulary"

    // An app an entry is limited to, by name: an App's own, else the
    // installed or recently dictated-into app with that id, else the id.
    function appName(limit) {
        if (limit.startsWith("app:")) return (Apps.entries.find(e => e.id === limit) || { name: Schema.vocabulary.t.aRemovedApp }).name
        const id = limit.toLowerCase()
        const named = Apps.entries.find(e => e.ids.some(i => i.toLowerCase() === id))
            || Apps.installed.find(a => a.ids.some(i => i.toLowerCase() === id))
            || Apps.recent.find(a => a.appId.toLowerCase() === id)
        return named ? named.name : limit
    }
    // Whether the entry is limited to App `app` (an entry of Apps.entries):
    // by its id, or by an id it is recognized by, as a correction learned in
    // it is.
    function limitedToApp(entry, app) {
        return entry.apps.some(a => a === app.id || app.ids.some(i => i.toLowerCase() === a.toLowerCase()))
    }
    // Whether the entry is limited to what a filter names: "profile/<id>" or
    // "app/<an App's id or an app id>"; "" is any.
    function limitedTo(entry, limit) {
        if (!limit) return true
        if (limit.startsWith("profile/")) return entry.profiles.includes(limit.slice(8))
        const id = limit.slice(4)
        const app = Apps.entries.find(e => e.id === id)
        return app ? limitedToApp(entry, app) : entry.apps.some(a => a.toLowerCase() === id.toLowerCase())
    }
    function limitedCount(limit) { return vocabulary.filter(e => limitedTo(e, limit)).length }
    // The filters "Limited to" offers: each profile and app some entry is
    // limited to, an app as the App that recognizes it where there is one,
    // and `also`, the one a link asked for. [{ id, label }]
    function limitChoices(also) {
        const profiles = new Set()
        const apps = new Set()
        for (const e of vocabulary) {
            e.profiles.forEach(p => profiles.add(p))
            e.apps.forEach(a => apps.add(a.startsWith("app:") ? a
                : (Apps.entries.find(app => app.ids.some(i => i.toLowerCase() === a.toLowerCase())) || { id: a }).id))
        }
        const choices = [...profiles].map(p => ({ id: "profile/" + p, label: profileName(p) + " profile", icon: profileIcon(p) }))
            .concat([...apps].map(a => ({ id: "app/" + a, label: appName(a), icon: "app-window" })))
        if (also && !choices.some(c => c.id === also))
            choices.push(also.startsWith("profile/") ? { id: also, label: profileName(also.slice(8)) + " profile", icon: profileIcon(also.slice(8)) }
                                                     : { id: also, label: appName(also.slice(4)), icon: "app-window" })
        return choices
    }
    // "Work and Email only, in Slack": the entry's limits, or nothing for one
    // that applies everywhere.
    function limitText(entry) {
        const profiles = entry.profiles.length ? Words.list(entry.profiles.map(profileName)) + " only" : ""
        const apps = entry.apps.length ? "in " + Words.list(entry.apps.map(appName)) : ""
        return [profiles, apps].filter(s => s).join(", ")
    }
    // A spelling's context, then where it came from: "Our dashboards.
    // Imported.", with nothing said for one you added.
    function entryDetail(entry) {
        if (entry.learned) return Schema.vocabulary.evidenceText(entry)
        const context = entry.context && !/[.!?]$/.test(entry.context) ? entry.context + "." : entry.context
        const source = entry.source === "csv" || entry.source === "import" ? "Imported." : entry.source === "learned" ? "Learned from your edits." : ""
        return [context, source].filter(s => s).join(" ")
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
        vocabulary: { blank: "Type the words. A phrase to replace needs a letter or a number.",
                      taken: "That's in the list already, with the same limits.",
                      incomplete: "A learned correction needs something to write.",
                      missing: "It, or an app it's limited to, was removed meanwhile." },
        any: { missing: "It, or something it uses, was deleted meanwhile.", builtIn: "Built-in profiles keep their names.",
               invalid: "Speecher couldn't save this." }
    })
    function refusalText(list, reason) {
        return reason ? (_refusals[list] || {})[reason] || _refusals.any[reason] || "" : ""
    }
}
