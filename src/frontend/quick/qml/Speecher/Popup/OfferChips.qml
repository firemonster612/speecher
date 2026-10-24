pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// The offers the popup carries above its capsule: the update while it needs
// the person (Updates.attention), and What's New after an update, which
// hides itself a few seconds into each popup and comes back with the next
// until it is opened or dismissed. Their wording is here (t).
Column {
    id: chips

    // The popup is up, and not asking about a selection's edit.
    property bool active
    readonly property bool offering: whatsNewChip.open || updateChip.open
    spacing: 8

    // A browser window or a check now would take focus from the dictation's
    // Target; installing is safe, since the restart waits for the dictation.
    readonly property bool dictating: Updates.restartBlocker === "dictation"
    readonly property bool failed: Updates.state === "error" || Updates.state === "checkFailed"

    readonly property var t: ({
        available: v => "Speecher " + v + " is available",
        stableReplacement: v => "Switch to Stable Release " + v + " (replaces this nightly build)",
        installAndRestart: "Install and restart",
        openReleasePage: "Open release page",
        downloading: (v, percent) => "Downloading Speecher " + v + " (" + percent + ")",
        ready: "Restart to finish updating",
        restart: ({ "": "Restart now", dictation: "Restart after this dictation", recording: "Restart after this recording" }),
        restarting: ({ "": "Restarting…", dictation: "Restarting after this dictation…", recording: "Restarting after this recording…" }),
        later: "Later",
        checkFailed: "Update check failed",
        tryAgain: "Try again",
        dismiss: "Dismiss",
        installed: v => "Speecher " + v + " is installed",
        seeWhatsNew: "See what's new"
    })

    // What's New's turn in this popup.
    property bool _whatsNewNow: false
    onActiveChanged: if (active) { _whatsNewNow = true; whatsNewTime.restart() }
    Timer {
        id: whatsNewTime
        interval: Theme.ms(6000)
        onTriggered: chips._whatsNewNow = false
    }

    OfferChip {
        id: whatsNewChip
        objectName: "whatsNewChip"
        open: chips.active && chips._whatsNewNow && Updates.whatsNewPending
        // The bare number: a nightly's suffix says nothing to someone
        // reading what changed.
        text: chips.t.installed(Status.version.split("-")[0])
        onDismissed: Updates.acknowledgeWhatsNew()
        Button { text: chips.t.seeWhatsNew; size: "small"; onClicked: Updates.showWhatsNew() }
    }

    OfferChip {
        id: updateChip
        objectName: "updateChip"
        open: chips.active && Updates.attention
        dot: chips.failed ? Theme.negative : Theme.accent
        text: {
            switch (Updates.state) {
            case "available": return Updates.stableReplacement ? chips.t.stableReplacement(Updates.availableVersion)
                                                               : chips.t.available(Updates.availableVersion)
            case "downloading": return chips.t.downloading(Updates.availableVersion, Words.percent(Updates.progress))
            case "ready": return Updates.error || chips.t.ready
            case "restartPending": return chips.t.restarting[Updates.restartBlocker]
            case "restarting": return chips.t.restarting[""]
            default: return Updates.error || chips.t.checkFailed
            }
        }
        closable: Updates.state === "available" || chips.failed
        onDismissed: Updates.dismiss()
        Reveal {
            sideways: true
            open: Updates.state === "available"
            Button {
                text: Updates.automaticDownloads ? chips.t.installAndRestart : chips.t.openReleasePage
                enabled: Updates.automaticDownloads || !chips.dictating
                size: "small"
                variant: "primary"
                onClicked: Updates.automaticDownloads ? Updates.installAndRestart() : Updates.updateNow()
            }
        }
        Reveal {
            sideways: true
            open: Updates.state === "ready"
            Row {
                spacing: 6
                Button { text: chips.t.later; size: "small"; variant: "ghost"; onClicked: Updates.later() }
                Button { text: chips.t.restart[Updates.restartBlocker]; size: "small"; variant: "primary"; onClicked: Updates.updateNow() }
            }
        }
        Reveal {
            sideways: true
            open: chips.failed
            Button {
                text: Updates.manualInstall ? chips.t.openReleasePage : chips.t.tryAgain
                enabled: !chips.dictating
                size: "small"
                onClicked: Updates.updateNow()
            }
        }
    }

    // One offer: a line of text with its buttons on a floating chip, centred
    // over the capsule. dot marks it; closable adds the round dismiss button.
    component OfferChip: Reveal {
        id: offer
        property string text
        property color dot: "transparent"
        property bool closable: true
        default property alias actions: actionRow.data
        signal dismissed()

        Item {
            width: parent.width
            height: surface.height
            Surface {
                id: surface
                anchors.horizontalCenter: parent.horizontalCenter
                width: line.implicitWidth + 28
                height: 40
                color: Theme.sheet
                Row {
                    id: line
                    anchors.centerIn: parent
                    spacing: 10
                    StatusDot { visible: offer.dot.a > 0; color: offer.dot; size: 7; anchors.verticalCenter: parent.verticalCenter }
                    Txt {
                        // The words give way so the buttons always fit the widest capsule.
                        width: Math.min(implicitWidth, offer.width - actionRow.width - close.width - 80)
                        text: offer.text
                        variant: "small"
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Row { id: actionRow; anchors.verticalCenter: parent.verticalCenter }
                    Reveal {
                        id: close
                        sideways: true
                        open: offer.closable
                        anchors.verticalCenter: parent.verticalCenter
                        CircleButton { size: 24; iconName: "x"; tooltip: chips.t.dismiss; onClicked: offer.dismissed() }
                    }
                }
            }
        }
    }
}
