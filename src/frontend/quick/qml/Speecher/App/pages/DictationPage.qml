import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// A way into what starts a dictation, what it records from and what it shows
// and plays, each row saying how that is set now.
Page {
    id: page
    title: Schema.page("dictation").title
    subtitle: Schema.page("dictation").subtitle

    readonly property var keys: Status.shortcutKeys
    readonly property var t: ({
        startsDictation: keys => keys.join("+") + " starts a dictation",
        noShortcut: "No dictation shortcut yet",
        commandOnly: "Starts with the speecher command",
        recordsFrom: name => "Records from " + name,
        liveText: on => on ? "Live text on" : "Live text off",
        sounds: on => on ? "sounds on" : "sounds off"
    })

    SubpageRows {
        pageId: "dictation"
        lines: ({
            shortcuts: page.keys.length ? page.t.startsDictation(page.keys)
                     : Health.find(System.checks, "globalShortcuts").reason === "unsupported" ? page.t.commandOnly : page.t.noShortcut,
            microphone: Microphones.deviceName ? page.t.recordsFrom(Microphones.deviceName) : Schema.row("audioDevice").noneLabel,
            feedback: page.t.liveText(Settings.values.showLiveText) + ", " + page.t.sounds(Settings.values.playSounds)
        })
    }
}
