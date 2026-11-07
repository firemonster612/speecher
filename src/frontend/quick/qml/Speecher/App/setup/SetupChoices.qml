import QtQuick
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// What the setup steps choose, shared by every step, and where each step
// stands. The drafts follow Settings until a step changes them; finish()
// writes them back. A step is done when its check passes: speech once the
// chosen service or one of its fallbacks works, Pasting once a step that is
// on can get words into the app, or waits only for what this computer can't
// do before the next sign-in (Setup.settled), or once only Keep is on and
// can run, which continueWithClipboardOnly chooses. One rule gates
// Continue, Finish, Skip and the rail: a step can be reached once every step
// before it is done. A check only asks for what its step shows and lets you
// fix there. Refinement passes with None or a provider that works.
// goTo asks the assistant to show another step, by id.
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

    // Setup.steps, each with done and verdict: what was chosen once it
    // passes, or what is missing.
    readonly property var steps: Setup.steps.map(s => Object.assign({}, s, _status(s.id)))

    // Whether a delivery step can run as the drafts choose it: nothing it
    // waits for but what settles. Saving the clipboard is Keep off's need,
    // and Keep only needs the clipboard to take the words, as Press does.
    function stepRuns(id) {
        if (id === "keep") return !System.deliverySteps.some(s => (s.waiting || []).includes("clipboard"))
        const step = System.deliverySteps.find(s => s.id === id) || {}
        return (step.waiting || []).every(n => (n === "clipboardSave" && keep) || Setup.settled(n, System.checks))
    }
    // Words left on the clipboard is all that can run: Keep can, and
    // Insert and Press are off.
    function continueWithClipboardOnly() {
        insert = false
        press = false
        keep = true
        goTo(steps[steps.findIndex(s => s.id === "delivery") + 1].id)
    }

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
            return { done: ready || (!!service && !!fallback),
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
            return { done: access.state === "ok" && present && Audio.heard,
                     verdict: access.state !== "ok" ? Health.status(access) : present && !Audio.heard ? Schema.row("microphoneTest").notHeardText : device ? device.label : "" }
        }
        case "delivery": {
            const delivers = (insert && stepRuns("insert")) || (press && stepRuns("press"))
            const done = delivers || (!insert && !press && keep && stepRuns("keep"))
            return { done: done, verdict: done ? ["insert", "press", "keep"].filter(step => choices[step])
                                                     .map(step => Setup.text.stepNames[step]).join(", ")
                                               : Setup.text.nothingDelivers }
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

    // Whether the step at index can be shown: every step before it is done.
    function reachable(index) { return steps.slice(0, index).every(s => s.done) }

    function finish() {
        Settings.set("speechProvider", speech)
        Settings.set("refinementProvider", cleanup)
        // Steps turned on first, so the last one on never goes off.
        const drafts = { insertThroughAccessibility: insert, pressPasteKeys: press, keepOnClipboard: keep }
        for (const id in drafts) if (drafts[id]) Settings.set(id, true)
        for (const id in drafts) if (!drafts[id]) Settings.set(id, false)
    }
}
