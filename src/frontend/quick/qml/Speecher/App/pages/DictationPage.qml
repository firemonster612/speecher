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
        recordsFrom: name => "Records from " + name,
        noMicrophone: "No microphone found",
        liveText: on => on ? "Live text on" : "Live text off",
        sounds: on => on ? "sounds on" : "sounds off"
    })

    SubpageRows {
        pageId: "dictation"
        lines: ({
            shortcuts: page.keys.length ? page.t.startsDictation(page.keys) : page.t.noShortcut,
            microphone: Microphones.deviceName ? page.t.recordsFrom(Microphones.deviceName) : page.t.noMicrophone,
            feedback: page.t.liveText(!!Settings.values.showLiveText) + ", " + page.t.sounds(!!Settings.values.playSounds)
        })
    }
}
