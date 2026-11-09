pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// One Vocabulary Entry in the list. A spelling shows its term, with a small
// mic when it is a key term; a replacement its phrase and the text it
// becomes; a Learned Correction is marked like a proof, the words heard
// struck through and the words written in blue pencil. Under it, a
// spelling's context or a correction's evidence; at its end, where it is
// limited to, whether it is off, and a spelling's uses. A search or a link
// lands on it by "vocab:<id>".
ListRow {
    id: row

    // An entry of Library.vocabulary.
    property var entry: ({})
    signal editRequested()

    readonly property var t: Schema.vocabulary.t
    readonly property bool replacement: entry.kind === "replacement"

    objectName: "vocab:" + (entry.id || "")
    clickable: true
    height: Math.max(Theme.rowHeight, main.implicitHeight + Theme.space6)
    Accessible.name: replacement ? entry.term + ", " + entry.replacement : entry.term || ""
    onClicked: editRequested()

    Column {
        id: main
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        spacing: 3

        Row {
            width: parent.width
            spacing: 7
            visible: !row.entry.learned
            Txt {
                id: said
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, row.replacement ? parent.width * 0.4 : parent.width - 20)
                text: row.entry.term || ""
                variant: "transcript"
                strong: !row.replacement
                elide: Text.ElideRight
            }
            Reveal {
                sideways: true
                open: !row.replacement && !!row.entry.keyTerm
                anchors.verticalCenter: parent.verticalCenter
                Icon {
                    name: "mic"
                    size: 13
                    color: Theme.textTertiary
                    Tip { text: row.t.keyTermTip; shown: tipHover.hovered }
                    HoverHandler { id: tipHover }
                }
            }
            Icon {
                visible: row.replacement
                anchors.verticalCenter: parent.verticalCenter
                name: "arrow-right"
                size: 14
                color: Theme.textTertiary
            }
            Txt {
                visible: row.replacement
                width: parent.width - said.width - 14 - parent.spacing * 2
                anchors.verticalCenter: parent.verticalCenter
                // The first line of a snippet that runs over several.
                text: (row.entry.replacement || "").split("\n")[0] + ((row.entry.replacement || "").includes("\n") ? " …" : "")
                variant: "code"
                elide: Text.ElideRight
            }
        }
        DiffText {
            visible: !!row.entry.learned
            width: parent.width
            segments: [{ kind: "removed", text: row.entry.term || "" }, { kind: "same", text: " " },
                       { kind: "added", text: row.entry.replacement || "" }]
            variant: "transcript"
        }
        Reveal {
            open: detailText.text.length > 0
            Txt {
                id: detailText
                width: parent.width
                text: Library.entryDetail(row.entry)
                variant: "small"
                tone: "secondary"
                elide: Text.ElideRight
            }
        }
    }

    trailing: Row {
        spacing: 14
        Reveal {
            sideways: true
            open: limits.text.length > 0
            anchors.verticalCenter: parent.verticalCenter
            Txt { id: limits; objectName: "vocabularyLimits"; text: Library.limitText(row.entry); variant: "small"; tone: "secondary" }
        }
        Reveal {
            sideways: true
            open: row.entry.enabled === false
            anchors.verticalCenter: parent.verticalCenter
            Txt { text: row.t.off; variant: "small"; tone: "tertiary" }
        }
        Txt {
            visible: !row.replacement
            width: 58
            anchors.verticalCenter: parent.verticalCenter
            text: Schema.vocabulary.usesText(row.entry.uses || 0)
            variant: "small"
            tone: "tertiary"
            horizontalAlignment: Text.AlignRight
        }
    }
    actions: [
        IconButton { iconName: "pencil"; size: 28; tooltip: Words.edit; onClicked: row.editRequested() },
        IconButton {
            iconName: "trash-2"; size: 28; tooltip: Words.delete_
            onClicked: {
                const name = row.entry.term
                const undo = Library.remove("vocabulary", row.entry.id)
                if (undo >= 0) Toaster.show(Words.deleted(name), { icon: "trash-2", actionText: Words.undo, action: () => Library.undo(undo) })
            }
        }
    ]
}
