pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "dictation"

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
        sounds: on => on ? "sounds on" : "sounds off"
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
        // The rest of a dictation without shortcuts: finishing, cancelling
        // and pasting again, then recording a call.
        CommandsRow {
            rowId: "controlCommands"
            shown: page.noShortcuts
            Button { text: Schema.row("controlCommands").historyLabel; size: "small"; onClicked: Navigation.follow("history") }
        }
        CommandsRow {
            rowId: "recordCommands"
            shown: page.noShortcuts
        }
    }
    Reveal { open: page.noShortcuts; Reach {} }

    // The dictation shortcut, where it is set; the cards below say what
    // pressing it does. With no shortcut to be had, the command above runs
    // one dictation, so it folds away with the rows.
    Reveal {
        open: !page.noShortcuts
        ShortcutHero {}
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

    // Recording a call has its key here too, so every shortcut is set, and
    // checked against the others, in one place.
    Reveal {
        open: !page.noShortcuts
        Section {
            title: Schema.row("recordingShortcut").section
            SchemaRow { rowId: "recordingShortcut" }
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
