pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend

// Where the updater is, from checking through downloading to restarting, as
// the version's card. Each state fades in as the last one leaves, and the
// card grows or shrinks to fit it.
Item {
    id: root

    property string channelLabel

    readonly property list<StateLine> _lines: [idle, checking, upToDate, available, downloading, ready, failed]
    readonly property StateLine _current: _lines.find(line => line.shown) ?? idle

    width: parent ? parent.width : implicitWidth
    implicitHeight: Math.max(Theme.rowHeight, _current.implicitHeight + Theme.space4 * 2)
    Grow on implicitHeight {}

    StateLine {
        id: idle
        stateId: "idle"
        title: Updates.lastChecked
        actions: [ Button { text: Updates.text.checkNow; size: "small"; onClicked: Updates.check() } ]
    }
    StateLine {
        id: checking
        stateId: "checking"
        busy: true
        title: Updates.checkingText(root.channelLabel)
    }
    StateLine {
        id: upToDate
        stateId: "upToDate"
        title: Updates.text.upToDate
        detail: Updates.lastChecked
        actions: [ Button { text: Updates.text.checkNow; size: "small"; onClicked: Updates.check() } ]
    }
    StateLine {
        id: available
        stateId: "available"
        dot: Theme.accent
        title: Updates.availableTitle
        detail: Updates.availableSize
        BulletList { items: Updates.availableHighlights; variant: "small" }
        actions: [
            Button { text: Updates.text.releaseNotes; variant: "ghost"; size: "small"; trailingIcon: "external-link"; onClicked: Updates.openReleaseNotes() },
            Button {
                text: Updates.automaticDownloads ? Updates.text.updateNow : Updates.text.openReleasePage
                variant: "primary"
                size: "small"
                onClicked: Updates.install()
            }
        ]
    }
    StateLine {
        id: downloading
        stateId: "downloading"
        title: Updates.downloadingTitle
        detail: Updates.text.downloadingDetail
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
        title: Updates.text.readyTitle
        detail: Updates.readyDetail
        actions: [
            Button { text: Updates.text.later; variant: "ghost"; size: "small"; onClicked: Updates.later() },
            Button { text: Updates.text.restartNow; variant: "primary"; size: "small"; onClicked: Updates.restart() }
        ]
    }
    StateLine {
        id: failed
        stateId: "error"
        dot: Theme.negative
        title: Updates.text.checkFailed
        detail: Updates.error
        actions: [ Button { text: Updates.text.tryAgain; size: "small"; onClicked: Updates.check() } ]
    }

    // One updater state: a title and detail, extra content under them and
    // actions on the right. dot marks a state that needs you; busy shows a
    // spinner. It shows while Updates.state is its stateId.
    component StateLine: Item {
        id: line
        required property string stateId
        property color dot: "transparent"
        property bool busy: false
        property string title
        property string detail
        default property alias extra: extraColumn.data
        property alias actions: actionRow.data
        readonly property bool shown: Updates.state === stateId

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
                    Spinner { visible: line.busy; size: 14; anchors.verticalCenter: parent.verticalCenter }
                    StatusDot { visible: line.dot.a > 0; pulse: false; size: 7; color: line.dot; anchors.verticalCenter: parent.verticalCenter }
                    Txt { text: line.title; variant: "label" }
                }
                Txt { visible: text.length > 0; width: parent.width; text: line.detail; variant: "small"; tone: "secondary"; wrapMode: Text.WordWrap }
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
