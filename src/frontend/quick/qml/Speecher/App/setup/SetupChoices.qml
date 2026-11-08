import QtQuick
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// What the setup steps choose, shared by every step, and where each step
// stands. The drafts follow Settings until a step changes them; finish()
// writes them back. A step is done when its check passes: speech once the
// chosen service or one of its fallbacks works, the microphone once it
// hears you, Pasting once a step that is on gets words into the app (the
// outcome Home and the Pasting page show, from the drafts), Refinement with
// None or a provider that works. Speech, the microphone and Pasting with
// nothing to deliver the words are needed: setup is complete without the
// others but not without these. No step holds back another. A check only
// asks for what its step shows and lets you fix there. goTo asks the
// assistant to show another step, by id.
//
// The shortcut step has no draft: its rows write Settings as you record, as
// the existing assistant binds shortcuts live, so the shortcut can be tried
// before setup ends. The Local Model, the Custom Endpoints' and Local
// Runner's rows, the fallback offers and the microphone are written live the
// same way, so a connection check tests the server typed and the microphone
// test listens to the one picked.
QtObject {
    id: choices

    property string speech: Settings.values.speechProvider || ""
    property string cleanup: Settings.values.refinementProvider || "none"
    // The delivery steps, by step id (DeliveryStep).
    property bool insert: Settings.values.insertThroughAccessibility ?? true
    property bool press: Settings.values.pressPasteKeys ?? true
    property bool keep: Settings.values.keepOnClipboard ?? true

    signal goTo(string stepId)

    // The delivery steps as the drafts set them, and where the words end
    // up: System.deliveryWith.
    readonly property var delivery: System.deliveryWith(insert, press, keep)
    // Setup.steps, each with done, needed (not done, and dictation can't
    // work without it) and verdict: what was chosen once it passes, or
    // what is missing.
    readonly property var steps: Setup.steps.map(s => Object.assign({ needed: false }, s, _status(s.id)))
    // Whether every needed step passes, so finishing records setup as done.
    readonly property bool complete: steps.every(s => !s.needed)

    function _status(id) {
        switch (id) {
        case "transcription": {
            const known = Models.speechServices.find(s => s.id === speech)
            const service = known ? ModelText.service(speech) : null
            // A model still downloading counts: the download carries on while
            // setup does. A sign-in being checked or a server not yet tested
            // doesn't, until it answers; one that couldn't reach the service
            // says why.
            const downloading = speech === "local" && Models.downloads[Models.localModelChoice] !== undefined
            // A fallback that works carries dictation while the service can't.
            const fallback = (Settings.values.speechFallbackList || []).filter(id => id !== speech)
                .map(id => ModelText.service(id)).find(s => s.state === "ready")
            const ready = !!service && (service.state === "ready" || downloading)
            const done = ready || (!!service && !!fallback)
            return { done: done, needed: !done,
                     verdict: !service ? Setup.text.notChosen : ready ? service.label
                                          : fallback ? Setup.text.takesOver(fallback.label)
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
            const outcome = delivery.outcome
            return { done: outcome === "delivers", needed: outcome === "nothing",
                     verdict: outcome === "delivers" ? ["insert", "press", "keep"].filter(step => choices[step])
                                                           .map(step => Setup.text.stepNames[step]).join(", ")
                                                     : Schema.pasting.outcomes[outcome] }
        }
        case "refinement": {
            // None is a valid answer; a provider counts once it works, so
            // one that can't yet is fixed or skipped, not passed.
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

    function finish() {
        Settings.set("speechProvider", speech)
        Settings.set("refinementProvider", cleanup)
        // Steps turned on first, so the last one on never goes off.
        const drafts = { insertThroughAccessibility: insert, pressPasteKeys: press, keepOnClipboard: keep }
        for (const id in drafts) if (drafts[id]) Settings.set(id, true)
        for (const id in drafts) if (!drafts[id]) Settings.set(id, false)
    }
}
