import QtQuick
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// What the setup steps choose, shared by every step, and where each step
// stands. The drafts follow Settings until a step changes them; finish()
// writes them back. A step is done when its check passes, and one rule gates
// Continue, Finish, Skip and the rail: a step can be reached once every step
// before it is done. A check only asks for what its step shows and lets you
// fix there. Refinement has no check, since its default is a valid answer.
// goTo asks the assistant to show another step, by id.
//
// The shortcut step has no draft: its rows write Settings as you record, as
// the existing assistant binds shortcuts live, so the shortcut can be tried
// before setup ends. The custom endpoint's URL and the microphone are written
// live the same way, so the microphone test listens to the one picked.
QtObject {
    id: choices

    property string speech: Settings.values.speechProvider || ""
    property string cleanup: Settings.values.refinementProvider || "none"
    property bool clipboardOnly: Settings.values.globalPasteRule === "clipboard_only"

    signal goTo(string stepId)

    // Setup.steps, each with done and verdict: what was chosen once it
    // passes, or what is missing.
    readonly property var steps: Setup.steps.map(s => Object.assign({}, s, _status(s.id)))

    function _status(id) {
        switch (id) {
        case "transcription": {
            const service = Models.serviceChoices.find(s => s.id === speech)
            // A model still downloading counts: the download carries on while setup does.
            const downloading = speech === "local" && Models.downloads[Models.localModelChoice] !== undefined
            const done = !!service && (service.state === "ready" || downloading)
            return { done: done, verdict: !service ? Setup.text.notChosen : service.state === "ready" ? service.label : service.status }
        }
        case "microphone": {
            // Allowed, a microphone that is there, and speech heard from it.
            const access = System.check("microphoneAccess")
            const microphone = Settings.values.audioDevice || ""
            const device = Audio.deviceOptions.find(o => o.id === microphone)
            const present = microphone === "" ? Audio.hasMicrophones : Audio.devices.some(d => d.id === microphone)
            return { done: access.state === "ok" && present && Audio.heard,
                     verdict: access.state !== "ok" ? Health.status(access) : present && !Audio.heard ? Audio.notHeardText : device ? device.label : "" }
        }
        case "accessibility":
        case "delivery": {
            const check = System.check(id === "delivery" ? "virtualKeyboardHealth" : "desktopAccessibility")
            if (id === "delivery" && clipboardOnly) return { done: true, verdict: Setup.text.clipboardOnly }
            return { done: check.state === "ok", verdict: Health.status(check) }
        }
        case "refinement":
            return { done: true, verdict: Library.providerChoices.find(p => p.id === cleanup).label }
        case "shortcut": {
            const keys = Settings.values.globalShortcut || []
            const check = System.check("globalShortcuts")
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
        if (clipboardOnly) Settings.set("globalPasteRule", "clipboard_only")
        else if (Settings.values.globalPasteRule === "clipboard_only") Settings.set("globalPasteRule", "standard_paste")
    }
}
