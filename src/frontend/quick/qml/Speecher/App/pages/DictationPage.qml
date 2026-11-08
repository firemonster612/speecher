import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// What starts, pauses and cancels a dictation, and how shortcuts reach
// Speecher, then a way into the microphone and the popup, each row saying
// how that is set now.
Page {
    id: page
    // The desktop gives Speecher no shortcut: the command is the way in, so
    // the page says so and leads with it.
    readonly property bool noShortcuts: Health.noShortcuts(System.checks)
    title: Schema.page("dictation").title
    subtitle: noShortcuts ? Schema.page("dictation").subtitleNoShortcuts : Schema.page("dictation").subtitle

    readonly property var t: ({
        recordsFrom: name => "Records from " + name,
        liveText: on => on ? "Live text on" : "Live text off",
        sounds: on => on ? "sounds on" : "sounds off",
        chooseShortcut: "Click to choose a shortcut",
        changeShortcut: "Click to change"
    })

    // How shortcuts reach Speecher: first where the command is the way in,
    // last where it is the extra.
    component Reach: Section {
        title: Schema.row("startCommand").section
        // Home lists the same check, and opens it here.
        HealthRow {
            rowId: "globalShortcuts"
            // Where Speecher knows the desktop's own shortcut settings.
            Button {
                visible: System.canOpenShortcutSettings
                text: "System settings"
                size: "small"
                trailingIcon: "external-link"
                anchors.verticalCenter: parent.verticalCenter
                onClicked: System.openShortcutSettings()
            }
        }
        StartCommandRow {}
    }
    Reveal { open: page.noShortcuts; Reach {} }

    // The dictation shortcut, big: the page's one picture, and where it is
    // set: clicking the keys records new ones. Under them, that they can be
    // clicked, or why the last keys were refused; the cards below say what
    // pressing them does. With no shortcut to be had, the command above runs
    // one dictation, so it folds away with the rows.
    Reveal {
        open: !page.noShortcuts
        Plate {
            id: hero
            readonly property var spec: Schema.row("globalShortcut")
            readonly property string refused: Settings.errors.globalShortcut || ""
            height: 132
            SearchLanding { rowIds: ["globalShortcut"]; target: hero; onLanded: recorder.forceActiveFocus() }

            ShortcutRecorder {
                id: recorder
                objectName: "dictationShortcut"
                rowId: "globalShortcut"
                size: "large"
                emptyText: page.t.chooseShortcut
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
                text: hero.refused || (recorder.keys.length > 0 && !recorder.listening ? page.t.changeShortcut : "")
                tone: hero.refused ? "negative" : "secondary"
            }
        }
    }

    // What pressing the shortcut does: with no shortcut to be had, the
    // command above runs one dictation, so it folds away.
    Reveal {
        open: !page.noShortcuts
        Section {
            title: Schema.row("shortcutBehavior").section
            SchemaRow { rowId: "shortcutBehavior" }
        }
    }

    // Keys that act while dictating bind through the desktop too, so they
    // fold away with the shortcut where it gives none.
    Reveal {
        open: !page.noShortcuts
        Section {
            title: Schema.row("pauseShortcut").section
            caption: "These only work while a dictation is in progress, so they can reuse keys other apps have."
            SchemaRow { rowId: "pauseShortcut" }
            SchemaRow { rowId: "cancelShortcut" }
        }
    }

    // A key for once the words are out, from any app.
    Reveal {
        open: !page.noShortcuts
        Section {
            title: Schema.row("pasteAgainShortcut").section
            SchemaRow { rowId: "pasteAgainShortcut" }
        }
    }

    Reveal { open: !page.noShortcuts; Reach {} }

    SubpageRows {
        pageId: "dictation"
        lines: ({
            microphone: Microphones.deviceName ? page.t.recordsFrom(Microphones.deviceName) : Schema.row("audioDevice").noneLabel,
            feedback: page.t.liveText(Settings.values.showLiveText) + ", " + page.t.sounds(Settings.values.playSounds)
        })
    }
}
