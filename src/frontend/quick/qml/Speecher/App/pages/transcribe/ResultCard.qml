import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// One finished file on its own card: its name, length and words with copy
// and export, then the transcript folded to five lines until opened. A
// failed file says why and offers Retry; when a retry succeeds, the error
// gives way to the transcript.
Card {
    id: card

    required property var result
    required property int index
    readonly property var t: Transcription.text
    readonly property bool failed: !!result.failed
    property bool expanded: false

    ListRow {
        first: true
        iconName: card.result.kind === "video" ? "file-video" : "file-audio"
        title: card.result.name
        detail: Transcription.durationLabel(card.result.durationMs) + (card.failed ? "" : ", " + Transcription.wordsLabel(card.result.words))
        trailing: Switcher {
            width: implicitWidth
            travel: 0
            currentIndex: card.failed ? 1 : 0
            Row {
                spacing: Theme.space2
                CopyButton {
                    anchors.verticalCenter: parent.verticalCenter
                    tooltip: card.t.copy
                    copiedText: Words.copied
                    onCopied: Transcription.copy(card.index)
                }
                MenuButton {
                    label: card.t.export_
                    leadingIcon: "download"
                    size: "small"
                    items: card.result.timings ? Transcription.exportFormats : Transcription.exportFormats.slice(0, 1)
                    onPicked: id => confirm(Transcription.exportResult(card.index, id))
                }
            }
            Button {
                text: Transcription.retrying === card.index ? card.t.retrying : card.t.retry
                busy: Transcription.retrying === card.index
                iconName: "rotate-cw"
                size: "small"
                onClicked: Transcription.retry(card.index)
            }
        }
    }

    Switcher {
        travel: 0
        currentIndex: card.failed ? 1 : 0

        // The transcript, the button that unfolds it, and where it was saved.
        ListRow {
            implicitHeight: transcript.height + more.height + Theme.space3 * 2
            MorphText {
                id: transcript
                y: Theme.space3
                width: parent.width
                wrap: true
                // A batch that didn't refine has only the raw transcript.
                text: Transcription.transcriptOf(card.result)
                variant: "transcript"
                maximumLineCount: card.expanded ? 100000 : 5
                elide: Text.ElideRight
            }
            Button {
                id: more
                readonly property bool shown: transcript.truncated || card.expanded
                x: -10
                y: transcript.y + transcript.height + Theme.space1
                opacity: shown ? 1 : 0
                enabled: shown
                Behavior on opacity { Ease {} }
                text: card.expanded ? card.t.showLess : card.t.showAll
                trailingIcon: card.expanded ? "chevron-up" : "chevron-down"
                size: "small"
                variant: "ghost"
                onClicked: card.expanded = !card.expanded
            }
            Txt {
                anchors.right: parent.right
                anchors.verticalCenter: more.verticalCenter
                width: Math.min(implicitWidth, parent.width - more.width - Theme.space4)
                text: card.result.savedPath ? Transcription.savedLabel(card.result.savedPath) : ""
                variant: "small"
                tone: "tertiary"
                elide: Text.ElideMiddle
            }
        }

        // A failed file: what went wrong.
        Notice {
            text: card.result.error || ""
            tone: "negative"
            inset: Theme.rowInset
        }
    }
}
