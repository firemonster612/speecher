pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// Where the updater is, from checking through downloading to restarting, as
// the version's card. Each state fades in as the last one leaves, and the
// card grows or shrinks to fit it.
Item {
    id: root

    property string channelLabel

    readonly property var t: ({
        // when is a Date, invalid before the first check.
        checked: when => isNaN(when.getTime()) ? "Not checked yet"
            : new Date().toDateString() === when.toDateString() ? "Last checked at " + when.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
            : "Last checked on " + when.toLocaleDateString(Qt.locale(), Locale.ShortFormat),
        checking: channel => channel ? "Checking the " + channel + " channel…" : "Checking for updates…",
        checkNow: "Check now",
        upToDate: "Speecher is up to date",
        stableReplacementDetail: "Replaces the Nightly Build you have now.",
        updateNow: "Update now",
        downloadingDetail: "Keep dictating; it takes effect when Speecher restarts.",
        readyDetail: v => "Speecher " + v + " is downloaded and ready.",
        failed: "Update failed"
    })

    readonly property list<StateLine> _lines: [idle, checking, upToDate, available, downloading, ready, restarting, failed]
    readonly property StateLine _current: _lines.find(line => line.shown) ?? idle

    width: parent ? parent.width : implicitWidth
    implicitHeight: Math.max(Theme.rowHeight, _current.implicitHeight + Theme.space4 * 2)
    Grow on implicitHeight {}

    StateLine {
        id: idle
        stateId: "idle"
        title: root.t.checked(Updates.lastChecked)
        actions: [ Button { text: root.t.checkNow; size: "small"; onClicked: Updates.check() } ]
    }
    StateLine {
        id: checking
        stateId: "checking"
        busy: true
        title: root.t.checking(root.channelLabel)
    }
    StateLine {
        id: upToDate
        stateId: "upToDate"
        title: root.t.upToDate
        detail: root.t.checked(Updates.lastChecked)
        actions: [ Button { text: root.t.checkNow; size: "small"; onClicked: Updates.check() } ]
    }
    StateLine {
        id: available
        stateId: "available"
        dot: Theme.accent
        title: Updates.stableReplacement ? Words.updates.stableReplacement(Updates.availableVersion) : Words.updates.available(Updates.availableVersion)
        detail: Updates.stableReplacement ? root.t.stableReplacementDetail : ""
        actions: [
            Button {
                text: Updates.automaticDownloads ? root.t.updateNow : Words.updates.openReleasePage
                variant: "primary"
                size: "small"
                onClicked: Updates.updateNow()
            }
        ]
    }
    StateLine {
        id: downloading
        stateId: "downloading"
        title: Words.updates.downloading(Updates.availableVersion)
        detail: root.t.downloadingDetail
        Item {
            width: parent.width
            height: percent.implicitHeight
            ProgressBar {
                width: parent.width - percentColumn.width - Theme.space3
                height: 6
                value: Updates.progress
                anchors.verticalCenter: parent.verticalCenter
            }
            // A fixed column, so the bar does not jitter as the digits roll.
            Item {
                id: percentColumn
                anchors.right: parent.right
                width: 40
                height: parent.height
                MorphText {
                    id: percent
                    anchors.right: parent.right
                    text: Words.percent(Updates.progress)
                    variant: "mono"
                    tone: "secondary"
                }
            }
        }
    }
    StateLine {
        id: ready
        stateId: "ready"
        dot: Theme.accent
        title: Words.updates.ready
        // A restart that could not start says why.
        detail: Updates.error || root.t.readyDetail(Updates.availableVersion)
        // Later puts the restart off, which also takes the update from the
        // popup and the sidebar until another version comes.
        actions: [
            Button { text: Words.updates.later; variant: "ghost"; size: "small"; onClicked: Updates.later() },
            Button { text: Words.updates.restart[Updates.restartBlocker]; variant: "primary"; size: "small"; onClicked: Updates.updateNow() }
        ]
    }
    // A restart asked for waits for the dictation or recording in the way.
    StateLine {
        id: restarting
        stateId: "restarting"
        shown: Updates.state === "restarting" || Updates.state === "restartPending"
        busy: true
        title: Words.updates.restarting[Updates.state === "restartPending" ? Updates.restartBlocker : ""]
    }
    StateLine {
        id: failed
        stateId: "error"
        shown: Updates.state === "error" || Updates.state === "checkFailed"
        dot: Theme.negative
        title: Updates.state === "checkFailed" ? Words.updates.checkFailed : root.t.failed
        detail: Updates.error
        // Where only installing by hand can work, the release page is the retry.
        actions: [ Button { text: Updates.manualInstall ? Words.updates.openReleasePage : Words.updates.tryAgain; size: "small"; onClicked: Updates.updateNow() } ]
    }

    // One updater state: a title and detail, extra content under them and
    // actions on the right. dot marks a state that needs you; busy shows a
    // spinner. It shows while Updates.state is its stateId, unless shown
    // says otherwise.
    component StateLine: Item {
        id: line
        required property string stateId
        property color dot: "transparent"
        property bool busy: false
        property string title
        property string detail
        default property alias extra: extraColumn.data
        property alias actions: actionRow.data
        property bool shown: Updates.state === stateId

        x: Theme.rowInset
        y: Theme.space4
        width: parent.width - Theme.rowInset * 2
        implicitHeight: Math.max(textColumn.implicitHeight, actionRow.height)
        visible: opacity > 0
        opacity: shown ? 1 : 0
        Behavior on opacity { Ease { duration: line.shown ? Theme.slow : Theme.fast } }

        Column {
            id: textColumn
            anchors.left: parent.left
            anchors.right: actionRow.left
            anchors.rightMargin: actionRow.width > 0 ? Theme.space5 : 0
            spacing: Theme.space2
            Column {
                width: parent.width
                spacing: 3
                Row {
                    spacing: Theme.space2
                    Reveal { sideways: true; open: line.busy; anchors.verticalCenter: parent.verticalCenter; Spinner { size: 14 } }
                    Reveal {
                        sideways: true
                        open: line.dot.a > 0
                        anchors.verticalCenter: parent.verticalCenter
                        StatusDot { pulse: false; size: 7; color: line.dot }
                    }
                    Txt { text: line.title; variant: "label" }
                }
                Reveal {
                    open: line.detail.length > 0
                    Txt { width: parent.width; text: line.detail; variant: "small"; tone: "secondary"; wrapMode: Text.WordWrap }
                }
            }
            Column { id: extraColumn; width: parent.width; visible: children.length > 0 }
        }
        Row {
            id: actionRow
            anchors.right: parent.right
            spacing: 6
        }
    }
}
