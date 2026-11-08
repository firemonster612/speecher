pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// Test: captures the app you switch to, as dictation would, and shows what
// Speecher makes of it: every App of yours that matches, in the order they
// count, and each setting with where it comes from. Nothing is saved.
Reveal {
    id: test

    readonly property var t: Schema.apps.t
    readonly property var result: Apps.tested
    readonly property bool found: !!result.name

    function start() {
        open = true
        Apps.testNext(5)
    }
    function close() { Apps.cancelCapture(); open = false }

    // "from Gmail", "from Browsers", "from Terminals" or "from Pasting":
    // where a tested value came from.
    function fromText(source, widest) {
        const place = source.layer === "app" ? (Apps.entries.find(e => e.id === source.app) || { name: "" }).name
                    : source.layer === "terminal" ? Schema.apps.type("terminal").label
                    : source.layer === "type" ? Schema.apps.type(result.type).label
                    : source.layer === "builtIn" ? t.builtInList : widest
        return t.from(place)
    }
    function valueFrom(value, source, widest) { return [value, fromText(source, widest)].filter(s => s).join(", ") }
    readonly property var lines: !found ? [] : [
        { label: t.type, value: Schema.apps.type(result.type).one },
        { label: t.profile, value: valueFrom(Library.profileName(result.profile), result.from.profile, Schema.apps.type("general").label) },
        { label: t.pasting, value: valueFrom(Schema.pasting.stepsText(result.steps), result.from.steps, t.pastingPage) },
        { label: t.pasteKeys, value: valueFrom(Schema.pasting.keysLabel(result.keys), result.from.keys, t.pastingPage),
          shown: Schema.pasting.keyChoices.length > 0 && result.steps.press },
        { label: t.history, value: result.history ? valueFrom(Schema.apps.historyLabel(result.history), result.from.history, "")
                                                  : t.historyAsPrivacy }
    ].filter(l => l.shown !== false)

    Item {
        width: parent.width
        implicitHeight: body.implicitHeight + Theme.space5 * 2

        Rectangle {
            anchors.fill: parent
            anchors.margins: Theme.washInset
            radius: Theme.washRadius
            color: Theme.fill
        }
        Column {
            id: body
            x: Theme.rowInset
            y: Theme.space5
            width: parent.width - Theme.rowInset * 2
            spacing: Theme.space3

            Item {
                width: parent.width
                height: Math.max(heading.height, again.height)
                Txt { id: heading; anchors.verticalCenter: parent.verticalCenter; text: test.t.testTitle; variant: "heading" }
                Row {
                    id: again
                    anchors.right: parent.right
                    spacing: Theme.space2
                    Button {
                        text: test.t.testAgain
                        iconName: "mouse-pointer-click"
                        busy: Apps.capture === "waiting"
                        enabled: Apps.capture !== "waiting"
                        onClicked: Apps.testNext(5)
                    }
                    Button { text: test.t.close; variant: "ghost"; onClicked: test.close() }
                }
            }
            Txt {
                visible: Apps.capture !== "" || !test.found
                width: parent.width
                text: Apps.capture === "waiting" ? test.t.switching(Apps.captureCountdown)
                    : Apps.capture === "failed" ? test.t.testFailed : test.t.testHelp
                variant: "small"
                tone: Apps.capture === "failed" ? "negative" : "secondary"
                wrapMode: Text.WordWrap
            }
            Column {
                visible: test.found && Apps.capture === ""
                width: parent.width
                spacing: Theme.space2
                Txt {
                    width: parent.width
                    text: test.result.name + (test.result.windowTitle ? " · " + test.result.windowTitle : "")
                    variant: "label"
                    elide: Text.ElideRight
                }
                Txt {
                    width: parent.width
                    text: test.result.matched && test.result.matched.length
                        ? test.t.matchedApps(test.result.matched.map(id => (Apps.entries.find(e => e.id === id) || { name: id }).name))
                        : test.t.matchedNone
                    variant: "small"
                    tone: "secondary"
                    wrapMode: Text.WordWrap
                }
                Repeater {
                    model: test.lines
                    Row {
                        id: line
                        required property var modelData
                        width: parent.width
                        spacing: Theme.space3
                        Txt { width: 130; text: line.modelData.label; variant: "small"; tone: "secondary" }
                        Txt { width: parent.width - 130 - parent.spacing; text: line.modelData.value; variant: "small"; wrapMode: Text.WordWrap }
                    }
                }
            }
        }
    }
}
