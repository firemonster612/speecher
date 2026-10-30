pragma Singleton
import QtQuick
import Speecher.Backend
import Speecher.Wording

// What the pages and setup say about Models' data: each speech service's and
// refinement provider's icon, help and status, ratings with their labels, a
// Local Model's facts in words, the hardware line, the endpoint checks and
// the runners. Lookups by id read Models' lists, so bindings follow them.
QtObject {
    id: text

    // Speech services ----------------------------------------------------------

    // What each service is good at, how to sign in to one that signs in,
    // and how its text arrives; %1 in help is how many languages it listens
    // for. A service this table lacks, such as the test stub, shows its
    // label alone.
    readonly property var _services: ({
        codex: { icon: "openai", help: "Very accurate, %1.",
                 signIn: "Sign in to ChatGPT in the Codex app, then check again.", textShows: "A phrase at a time",
                 models: [{ name: "GPT Live Transcribe", note: "Writes each phrase as you pause." },
                          { name: "GPT Transcribe", note: "Goes over the whole recording when you stop, while Transcribe again for accuracy is on." }] },
        claude: { icon: "claude", help: "%1, automatic punctuation and numerals.",
                  signIn: "Sign in to Claude Code in its desktop app or with /login in the claude CLI, then check again.", textShows: "Words as you speak",
                  models: [{ name: "Deepgram Nova 3", note: "Writes your words as you speak." }] },
        endpoint: { icon: "server", help: "Your server sets speed, accuracy and languages.",
                    textShows: "Text after you stop", models: [] },
        local: { icon: "cpu", help: "Runs on this computer: no account, works offline after a one-time download.", models: [] }
    })
    function _service(id) { return _services[id] || { icon: "circle", help: "", textShows: "", models: [] } }

    // A speech service with its wording: { id, label, icon, help,
    // signIn (how to sign in, for one that signs in), textShows, models,
    // state, problem, languages }, and languageLink, the
    // part of help that says how many languages, where it does.
    function service(id) {
        const known = Models.speechServices.find(s => s.id === id)
                      || { id: id, label: Models.service(id).label, state: "unknown", problem: "", languages: [] }
        const wording = _service(id)
        const saysCount = wording.help.indexOf("%1") >= 0
        return Object.assign({}, wording, known, { textShows: textShows(id),
                                                   help: wording.help.replace("%1", languageCount(known.languages)),
                                                   languageLink: saysCount ? languageLink(known.languages) : "" })
    }
    // How a service's text arrives; a Local Model's depends on its model.
    function textShows(id) {
        if (id !== "local") return _service(id).textShows
        const m = catalogModel(Models.localModelChoice)
        return m && m.streams ? "Words as you speak" : "Text after you stop"
    }

    // A provider's status beside it: its problem, as core says it, or Ready
    // once nothing stands in its way. One not yet checked says so, or why
    // its sign-in check couldn't reach the service. A Local Model still
    // downloading says how far it is, and one not downloaded what it would
    // fetch.
    function status(provider) {
        if (provider.state === "ready") return "Ready"
        if (provider.signInChecking) return checking
        if (provider.state === "unknown")
            return (provider.signInCheckFailure || (provider.signsIn ? signInUnknown : notChecked)).replace(/\.$/, "")
        if (provider.id === "local" && provider.state === "noModel" && !provider.refines) {
            const fraction = Models.downloads[Models.localModelChoice]
            if (fraction !== undefined) return "Downloading, " + Words.percent(fraction)
            const m = catalogModel(Models.localModelChoice)
            if (m) return size(m.sizeBytes) + " download"
        }
        return provider.problem.replace(/\.$/, "")
    }
    // What setup says of a service while its sign-in is checked.
    readonly property string checking: "Checking…"
    // Asks for the speech sign-ins, as a page that shows them does when it
    // opens, while one of ids (every service that signs in, by default)
    // is still unknown; Check again asks Models.checkSignIns() itself.
    function checkUnknownSignIns(ids) {
        const unknown = s => s.signsIn && s.state === "unknown" && !s.signInChecking && (!ids || ids.includes(s.id))
        if (Models.speechServices.some(unknown)) Models.checkSignIns()
    }
    readonly property string notChecked: "Not checked yet"
    // A sign-in no check has vouched for yet, as Accounts says Unknown.
    readonly property string signInUnknown: "Sign-in unknown"
    // The state a choice's status is toned by: an unknown one is being
    // checked, its check failed, or, for a sign-in, unknown as Accounts
    // tones it; something that doesn't sign in is waiting for its check.
    function _shownState(provider) {
        if (provider.state !== "unknown") return provider.state
        return provider.signInChecking ? "checking" : provider.signInCheckFailure ? "checkFailed"
             : provider.signsIn ? "unknown" : "notChecked"
    }
    // A provider as ServiceCards shows it, rated from ratings (Models'
    // ratings by id), or with no rating at all when ratings is left out.
    // note is the line under the ratings, its parts as sentences.
    function _choice(provider, help, ratings, noteParts) {
        const choice = { id: provider.id, label: provider.label, icon: provider.icon, help: help,
                         state: _shownState(provider), status: status(provider) }
        if (ratings) choice.bars = bars(ratings[provider.id])
        if (noteParts) choice.note = noteParts.filter(t => !!t).join(". ")
        return choice
    }
    // Speech services as a choice for dictation, saying what each is good
    // at, how its text arrives and what it was rated on; Transcribe files',
    // whose text all arrives at the end, leave out how it arrives. Setup's
    // say what each needs and what it was rated on.
    function speechChoices(services) {
        return services.map(s => _helpChoice(s.id, [textShows(s.id), ratedAsNote(Models.speechRatings[s.id])]))
    }
    function fileSpeechChoices(services) {
        return services.map(s => _helpChoice(s.id, [ratedAsNote(Models.speechRatings[s.id])]))
    }
    // A service by what it is good at, the count of languages in that help
    // linking to them.
    function _helpChoice(id, noteParts) {
        const s = service(id)
        return Object.assign(_choice(s, s.help, Models.speechRatings, noteParts), { helpLink: s.languageLink })
    }

    // Where a speech service stands in the chain: 0 for the service itself,
    // then its place among the fallbacks.
    function roleText(place) { return place === 0 ? "Used as your service." : "Used as fallback " + place + "." }
    // What the fallback list says under each fallback: { text, state }.
    function fallbackStatus(provider, index, primaryLabel) {
        if (provider.state !== "ready" && provider.state !== "unknown") return { text: status(provider), state: provider.state }
        return { text: index === 0 ? "Used when " + primaryLabel + " is unavailable." : "Used when the ones above are unavailable.",
                 state: "ready" }
    }

    // Ratings --------------------------------------------------------------------

    readonly property var _measures: ({ accuracy: "Accuracy", quality: "Quality", speed: "Speed" })
    // A rating's bars as RatingBars draws them, or null for none.
    function bars(rating) { return rating ? rating.bars.map(b => ({ label: _measures[b.measure], value: b.value })) : null }
    // " Rated as Parakeet 0.6B on this computer.", after a role, or nothing.
    function ratedAsText(rating) { return rating && rating.ratedModel ? " Rated as " + rating.ratedModel + " on this computer." : "" }
    // "Rated as Parakeet 0.6B", under a card's ratings, or nothing when
    // they are the service's own.
    function ratedAsNote(rating) { return rating && rating.ratedModel ? "Rated as " + rating.ratedModel : "" }
    readonly property string unrated: "No rating"

    // Local Models ---------------------------------------------------------------

    function catalogModel(id) { return Models.catalog.find(m => m.id === id) || null }
    // Downloaded models as a choice, for a profile's own speech model.
    readonly property var downloadedModels: Models.downloaded.map(id => ({ id: id, label: catalogModel(id).name }))

    readonly property var _ratings: ({ recommended: "Recommended", good: "Good", situational: "Situational",
                                       notRecommended: "Not recommended" })
    function ratingLabel(m) { return _ratings[m.rating] }
    readonly property var _fits: ({ fits: "Fits", tight: "Tight fit", tooLarge: "Too large" })
    // "731 MB", "2.4 GB": decimal units, as download sizes are quoted.
    function size(bytes) { return bytes >= 1e9 ? (bytes / 1e9).toFixed(1) + " GB" : Math.round(bytes / 1e6) + " MB" }
    // "0.3 s", never below 0.1 s.
    function seconds(s) { return Math.max(0.1, s).toFixed(1) + " s" }
    // How long 10 s of speech takes here.
    function speedLine(m) {
        if (m.measuredSeconds !== null) return seconds(m.measuredSeconds) + " per 10 s (measured)"
        if (m.estimatedSeconds !== null) return "About " + seconds(m.estimatedSeconds) + " per 10 s (estimated)"
        return Models.hardwareKnown ? "Not measured on a computer like this one yet" : "Checking this computer…"
    }
    function wordErrors(m) { return m.werClear + "% clear, " + m.werEveryday + "% everyday" }
    // "English", or how many languages: what a service or model listens
    // for, by their codes.
    function languageCount(codes) { return codes.length === 1 && codes[0] === "en" ? "English" : codes.length + " languages" }
    // The count as a link to the list, or nothing where there is one.
    function languageLink(codes) { return codes.length > 1 ? languageCount(codes) : "" }
    // The languages by code, as the Spoken language row names them and in
    // its order, for a LanguagePopover.
    function languageOptions(codes) { return Schema.row("spokenLanguage").options.filter(o => codes.indexOf(o.id) >= 0) }
    // The Spoken Languages a speech service can be set to: those it listens
    // for, and Automatic where it detects the language.
    function spokenLanguageOptions(s) { return languageOptions(s.detects ? s.languages.concat(["auto"]) : s.languages) }
    // The line under a model's name: languages and how its text arrives.
    function metaLine(m) {
        return languageCount(m.languages) + ", text " + (m.streams ? "as you speak" : "after you stop")
             + (m.fit === "tight" ? ", " + _fits.tight.toLowerCase() : "")
    }
    // Why a model can't run here, or the last download's or test's failure.
    function problem(m) { return m.problem || (m.fit === "tooLarge" ? tooLarge + "." : "") }
    // "AMD Ryzen 9 7950X, 32 threads, NVIDIA GeForce RTX 4070 (CUDA), 64 GB of
    // memory, 12 GB on the graphics card"
    readonly property string computer: {
        const c = Models.computer
        if (!Models.hardwareKnown || !c.chip) return ""
        const gb = bytes => Math.round(bytes / 1e9) + " GB"
        return [c.chip + ", " + c.threads + " threads",
                c.gpu ? c.gpu + " (" + c.gpuBackend + ")" : "no graphics acceleration",
                gb(c.memoryBytes) + " of memory" + (c.gpuMemoryBytes ? ", " + gb(c.gpuMemoryBytes) + " on the graphics card" : "")].join(", ")
    }
    readonly property string modelFolder: Models.modelFolder + ", " + size(Models.modelFolderBytes) + " used"
    readonly property var accelerationOptions: Models.accelerationOptions.map(o => ({
        id: o.id, label: o.available ? o.label : o.label + " (not available)",
        help: o.available ? "" : "No graphics card here reaches this backend." }))
    readonly property var graphicsCards: Models.graphicsCards.map(c => ({ id: c.id, label: c.label || "Missing graphics card" }))

    // The model browser's fixed wording.
    readonly property string useModel: "Use this model"
    readonly property string testSpeed: "Test speed"
    readonly property string inUse: "In use"
    readonly property string suggested: "Suggested for this computer"
    readonly property string tooLarge: "Too large for this computer"
    readonly property string ratedFor: "Ratings, speeds and fits are for this computer: %1."
    readonly property string ratedOnceChecked: "Ratings, speeds and fits will match this computer once it has been checked."
    readonly property string compareNote: "Accuracy is measured on clear read speech and on everyday speech. Speed is an estimate until a model is downloaded and tested here."
    readonly property string deleteModel: "Delete model"
    readonly property string deleteBody: "The model file is removed from this computer. You can download it again later."
    function deleteQuestion(name) { return "Delete " + name + "?" }
    function download(bytes) { return "Download " + size(bytes) }
    function progress(m, fraction) { return size(m.sizeBytes * fraction) + " of " + size(m.sizeBytes) }
    // The facts under a card's summary, by key.
    readonly property var factLabels: ({ bestFor: "Best for", wordErrors: "Word errors", speed: "10 s of speech",
                                         languages: "Languages", licence: "Licence", file: "File" })
    // The library's filter: every model, or only those on this computer.
    readonly property var libraryFilters: [{ id: "all", label: "All" }, { id: "downloaded", label: "Downloaded" }]

    // Custom Endpoints -----------------------------------------------------------

    // A connection check as StatusText shows it: { text, tone, busy, dot }.
    function endpointCheck(check) {
        if (check.checking) return { text: "Checking…", tone: "neutral", busy: true, dot: true }
        if (!check.checked) return { text: "Not tested yet.", tone: "neutral", busy: false, dot: false }
        return { text: check.status, tone: check.ok ? "positive" : "negative", busy: false, dot: true }
    }

    // Refinement -----------------------------------------------------------------

    // What refinement does to a sentence, for the Refinement page's and
    // setup's proof: spoken words with refinement's edits, marked as core's
    // CorrectionDiff marks a selection edit. An illustration, not a run.
    readonly property var refinementSample: [
        { kind: "removed", text: "um so" }, { kind: "same", text: " I think we should " },
        { kind: "removed", text: "uh" }, { kind: "same", text: " move the " }, { kind: "removed", text: "the" },
        { kind: "same", text: " standup to " }, { kind: "removed", text: "like ten thirty" }, { kind: "same", text: " " },
        { kind: "added", text: "10:30," }, { kind: "same", text: " because half the team is in Lisbon this week" },
        { kind: "added", text: "." }
    ]

    readonly property var _refiners: ({
        openai: { icon: "openai", help: "Uses your ChatGPT or Codex sign-in.",
                  signIn: "Sign in with the Codex app, or add an OpenAI API key on Accounts." },
        anthropic: { icon: "claude", help: "Uses your Claude Code sign-in.",
                     signIn: "Sign in to Claude Code in its desktop app or with /login in the claude CLI." },
        local: { icon: "cpu", help: "A model server running on this computer." },
        endpoint: { icon: "server", help: "Your own OpenAI- or Anthropic-compatible server." },
        none: { icon: "circle-off", help: "Keeps your words as spoken." }
    })
    // A refinement provider with its wording, or None.
    function provider(id) {
        if (id === "none") return { id: "none", label: "None", icon: _refiners.none.icon, help: _refiners.none.help, state: "", problem: "", refines: true }
        const known = Models.refinementProviders.find(p => p.id === id) || { id: id, label: id, state: "unknown", problem: "" }
        return Object.assign({ icon: "circle", help: "" }, _refiners[id], known, { refines: true })
    }
    // The providers, then None, as a choice, with what each was rated on.
    function refinementChoices(providers) {
        return providers.map(p => _choice(provider(p.id), provider(p.id).help, Models.refinementRatings,
                                          [ratedAsNote(Models.refinementRatings[p.id])]))
            .concat([_choice(provider("none"), _refiners.none.help)])
    }
    // The model a provider refines with, as its settings name it.
    function refinementModel(id) {
        const v = Settings.values
        const listed = (models, model) => (models.find(m => m.id === model) || { label: model || "" }).label
        return id === "openai" ? listed(Models.openAiModels, v.openAiModel)
             : id === "anthropic" ? listed(Models.anthropicModels, v.anthropicModel)
             : id === "local" ? v.localRunnerModel || ""
             : id === "endpoint" ? v.refinementEndpointModel || "" : ""
    }

    // What the Local Runner row says: looking, what was found, or none.
    readonly property string runnersText: {
        const running = Models.runners.filter(r => r.running)
        return Models.detectingRunners ? "Looking for Ollama, LM Studio and llama-server…"
             : running.length ? "Speecher found " + Words.list(running.map(r => r.label)) + "."
             : Models.runnersChecked ? "None found. Install and start Ollama, LM Studio or llama-server."
             : "Not checked yet."
    }
    // A chosen runner that has stopped, as the runner menu lists it.
    function stoppedRunner(label) { return label + " (not running)" }
    // The Ollama pull of the cleanup model suggested here.
    readonly property string pullText: {
        const s = Models.cleanupSuggestion
        const pull = Models.pull
        if (pull.running) return "Downloading " + (s.name || pull.tag) + " through Ollama: " + size(pull.completedBytes) + " of " + size(pull.totalBytes)
        if (!s.name) return "Cleanup would be slow on this computer. A cloud provider or skipping cleanup will feel faster."
        return "Suggested for this computer: " + s.name + ", " + size(s.sizeBytes) + "." + (pull.error ? " " + pull.error : "")
    }
    function fallbackOfferText(offer) { return "Add " + offer.name + " as a fallback, on this computer." }
    readonly property string addFallback: "Add"
}
