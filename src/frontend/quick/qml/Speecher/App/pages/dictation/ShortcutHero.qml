import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The dictation shortcut, big: Dictation's one picture, and where the
// shortcut is set, there and in setup. Clicking the keys records new ones.
// Under them, that they can be clicked, or why the last keys were refused.
Plate {
    id: hero

    readonly property var spec: Schema.row("globalShortcut")
    readonly property string refused: Settings.errors.globalShortcut || ""
    readonly property var t: ({
        chooseShortcut: "Click to choose a shortcut",
        changeShortcut: "Click to change"
    })

    height: 132
    SearchLanding { rowIds: ["globalShortcut"]; target: hero; onLanded: recorder.forceActiveFocus() }

    ShortcutRecorder {
        id: recorder
        objectName: "dictationShortcut"
        rowId: "globalShortcut"
        size: "large"
        emptyText: hero.t.chooseShortcut
        width: Math.min(parent.width - Theme.space6 * 2, 480)
        height: 72
        anchors.horizontalCenter: parent.horizontalCenter
        y: Theme.space4
        Accessible.name: hero.spec.label
    }
    MorphText {
        objectName: "shortcutCaption"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: recorder.bottom
        anchors.topMargin: Theme.space3
        width: Math.min(implicitWidth, parent.width - Theme.space6 * 2)
        elide: Text.ElideRight
        text: hero.refused || (recorder.keys.length > 0 && !recorder.listening ? hero.t.changeShortcut : "")
        tone: hero.refused ? "negative" : "secondary"
    }
}
