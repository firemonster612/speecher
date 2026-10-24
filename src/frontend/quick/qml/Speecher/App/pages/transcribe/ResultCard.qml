import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// One finished file on its own card: its name, length and words with copy
// and export, then the transcript folded to five lines until opened. A
// failed file says why and offers Retry; when a retry succeeds, the error
// gives way to the transcript. A file that finished with a problem on the
// way (refinement fell back, saving failed) says so under its transcript.
Card {
    id: card

    required property var result
    required property int index
    readonly property bool failed: !!result.failed
    property bool expanded: false
    // Export picked a format; the view asks where to save it.
    signal exportRequested(string format)

    ListRow {
        first: true
        iconName: card.result.kind === "video" ? "file-video" : "file-audio"
        title: card.result.name
        detail: [card.result.durationMs >= 0 ? TranscribeWords.durationLabel(card.result.durationMs) : "",
                 card.failed ? "" : TranscribeWords.wordsLabel(card.result.words)].filter(Boolean).join(", ")
        trailing: Switcher {
            width: implicitWidth
            travel: 0
            currentIndex: card.failed ? 1 : 0
            Row {
                spacing: Theme.space2
                CopyButton {
                    anchors.verticalCenter: parent.verticalCenter
                    tooltip: Words.copy
                    copiedText: Words.copied
                    onCopied: Transcription.copy(card.index)
                }
                MenuButton {
                    id: exportOne
                    label: TranscribeWords.export_
                    leadingIcon: "download"
                    size: "small"
                    items: card.result.timings ? TranscribeWords.exportFormats : TranscribeWords.exportFormats.slice(0, 1)
                    onPicked: id => card.exportRequested(id)
                    Connections {
                        target: Transcription
                        function onExported(index) {
                            if (index === card.index) exportOne.confirm(TranscribeWords.saved)
                        }
                    }
                }
            }
            Button {
                text: Transcription.retrying === card.index ? TranscribeWords.retrying : TranscribeWords.retry
                busy: Transcription.retrying === card.index
                enabled: Transcription.retrying < 0 || Transcription.retrying === card.index
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
                text: card.result.text
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
                text: card.expanded ? TranscribeWords.showLess : TranscribeWords.showAll
                trailingIcon: card.expanded ? "chevron-up" : "chevron-down"
                size: "small"
                variant: "ghost"
                onClicked: card.expanded = !card.expanded
            }
            Txt {
                anchors.right: parent.right
                anchors.verticalCenter: more.verticalCenter
                width: Math.min(implicitWidth, parent.width - more.width - Theme.space4)
                text: card.result.savedPath ? TranscribeWords.savedLabel(card.result.savedPath) : ""
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

    // A finished file's problem on the way.
    Reveal {
        open: !card.failed && !!card.result.error
        Notice { text: card.result.error || ""; inset: Theme.rowInset }
    }
}
