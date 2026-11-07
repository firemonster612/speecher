import QtQuick
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// What the setup steps choose, shared by every step, and where each step
// stands. The drafts follow Settings until a step changes them; finish()
// writes them back. A step is done when its check passes, or for
// accessibility and delivery settles (Setup.settled), or when the delivery
// steps chosen don't need it; on macOS declining Accessibility is a valid
// choice that leaves the words on the clipboard. One rule gates
// Continue, Finish, Skip and the rail: a step can be reached once every step
// before it is done. A check only asks for what its step shows and lets you
// fix there. Refinement has no check, since its default is a valid answer.
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
    // passes, or what is missing. Windows asks for no accessibility.
    readonly property var steps: Setup.steps
        .filter(s => s.id !== "accessibility" || Qt.platform.os !== "windows")
        .map(s => Object.assign({}, s, _status(s.id)))

    // Whether the delivery steps chosen need accessibility: inserting does,
    // and on macOS pressing ⌘V does too.
    readonly property bool accessibilityNeeded: insert || (press && Qt.platform.os === "osx")

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
            const done = !!service && (service.state === "ready" || downloading)
            return { done: done, verdict: !service ? Setup.text.notChosen : done ? service.label
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
            // Pressing the keys on Linux needs the virtual keyboard set up.
            const keyboard = Health.find(System.checks, "virtualKeyboardHealth")
            if (press && System.hasVirtualKeyboard && keyboard.state !== "ok")
                return { done: Setup.settled(keyboard), verdict: Health.status(keyboard) }
            return { done: true, verdict: ["insert", "press", "keep"].filter(step => choices[step])
                                              .map(step => Setup.text.stepNames[step]).join(", ") }
        }
        case "accessibility": {
            const check = Health.find(System.checks, "desktopAccessibility")
            if (!accessibilityNeeded) return { done: true, verdict: Setup.text.notNeeded }
            if (Qt.platform.os === "osx") {
                const need = Schema.row("insertThroughAccessibility").need
                return { done: true, verdict: check.state === "ok" ? need.ready : need.notReady }
            }
            return { done: Setup.settled(check), verdict: Health.status(check) }
        }
        case "refinement":
            return { done: true, verdict: ModelText.provider(cleanup).label }
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
