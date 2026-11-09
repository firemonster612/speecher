import QtQuick
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "../pages/home/words.js" as HomeWords

// The setup assistant's draft, and where each step stands. The steps are
// the pages' own sections, writing through Settings as the pages do. While
// setup is open Settings drafts the rows the steps choose (draftedRows):
// their edits wait in one draft, which every page and System's outcomes
// show too, until save() saves it; the host drops it when setup closes
// otherwise. The microphone and the shortcut are saved as they change, so
// the microphone test listens to the one picked and the shortcut can be
// tried, and so are the sign-ins, servers and models a Fix sheet changes,
// so their checks test them.
//
// A step is done when its check passes: speech once the first service, or
// one after it in the order, works; the microphone once it hears you;
// Pasting once a step that is on gets words into the app (the outcome Home
// and the Pasting page show); Refinement with Clean up my words off or a
// first service that works. Speech, the microphone and Pasting with nothing
// to deliver the words are needed: setup is complete without the others but
// not without these. No step holds back another. goTo asks the assistant to
// show another step, by id.
QtObject {
    id: choices

    readonly property var draftedRows: ["speechServices", "spokenLanguage", "refinementServices", "refinementEnabled",
                                        "insertThroughAccessibility", "pressPasteKeys", "keepOnClipboard"]

    signal goTo(string stepId)

    // Setup.steps, each with done, needed (not done, and dictation can't
    // work without it) and verdict: what was chosen once it passes, or
    // what is missing.
    readonly property var steps: Setup.steps.map(s => Object.assign({ needed: false }, s, _status(s.id)))
    // Whether every needed step passes, so finishing records setup as done.
    readonly property bool complete: steps.every(s => !s.needed)

    Component.onCompleted: Settings.startDraft(draftedRows)

    // Saves the draft: Finish and Skip setup.
    function save() { Settings.saveDraft() }

    function _status(id) {
        switch (id) {
        case "transcription": {
            const speech = ModelText.mainSpeech
            const service = speech ? ModelText.service(speech) : null
            // A model still downloading counts: the download carries on while
            // setup does. A sign-in being checked or a server not yet tested
            // doesn't, until it answers; one that couldn't reach the service
            // says why.
            const downloading = speech === "local" && Models.downloads[Models.localModelChoice] !== undefined
            // A later service that works carries dictation while the first can't.
            const fallback = ModelText.order("speech").slice(1)
                .map(id => ModelText.service(id)).find(s => s.state === "ready")
            const ready = !!service && (service.state === "ready" || downloading)
            const done = ready || (!!service && !!fallback)
            return { done: done, needed: !done,
                     verdict: !service ? Setup.text.notChosen : ready ? service.label
                                          : fallback ? HomeWords.text.takesOver(fallback.label)
                                          : service.state !== "unknown" ? ModelText.status(service)
                                          : speech === "endpoint" ? ModelText.endpointCheck(Models.speechEndpoint).text
                                          : service.signInCheckFailure || ModelText.checking }
        }
        case "microphone": {
            // Allowed, a microphone that is there, and speech heard from it.
            const access = Health.find(System.checks, "microphoneAccess")
            const microphone = Settings.values.audioDevice || ""
            const device = Microphones.deviceOptions.find(o => o.id === microphone)
            const present = microphone === "" ? Audio.hasMicrophones : Audio.devices.some(d => d.id === microphone)
            const done = access.state === "ok" && present && Audio.heard
            return { done: done, needed: !done,
                     verdict: access.state !== "ok" ? Health.status(access) : present && !Audio.heard ? Schema.row("microphoneTest").notHeardText : device ? device.label : "" }
        }
        case "delivery": {
            // Words that stay on the clipboard still get somewhere, so only
            // nothing at all is needed.
            const outcome = System.pastingOutcome
            return { done: outcome === "delivers", needed: outcome === "nothing",
                     verdict: outcome === "delivers" ? System.deliverySteps.filter(s => s.on)
                                                           .map(s => Setup.text.stepNames[s.id]).join(", ")
                                                     : Schema.pasting.outcomes[outcome] }
        }
        case "refinement": {
            // Clean up my words off is a valid answer; a service counts once
            // it works, so one that can't yet is fixed or skipped, not passed.
            const cleanup = ModelText.mainRefiner
            const provider = ModelText.provider(cleanup)
            const done = cleanup === "none" || provider.state === "ready"
            return { done: done, verdict: done ? provider.label : ModelText.status(provider) }
        }
        case "shortcut": {
            const keys = Settings.values.globalShortcut || []
            const check = Health.find(System.checks, "globalShortcuts")
            // A desktop that gives no shortcut leaves the command, which needs none.
            if (check.reason === "unsupported") return { done: true, verdict: Setup.text.commandOnly }
            return { done: keys.length > 0 && check.state === "ok", verdict: keys.length ? keys.join(" ") : Words.notSet }
        }
        default:
            return { done: true, verdict: "" }
        }
    }
}
