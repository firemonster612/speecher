pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend

// Step two: the file being heard, its progress and the queue, beside the
// transcript so far, which follows the newest words.
Row {
    id: view

    readonly property var t: Transcription.text

    width: parent ? parent.width : implicitWidth
    spacing: Theme.space4

    Section {
        id: progress
        width: (parent.width - parent.spacing) / 2
        title: Transcription.phase
        caption: Transcription.processingCount

        Column {
            x: Theme.rowInset
            width: parent.width - 2 * Theme.rowInset
            topPadding: Theme.space4
            bottomPadding: Theme.space4
            spacing: Theme.space3
            Item {
                width: parent.width
                height: fileName.height
                MorphText {
                    id: fileName
                    width: parent.width - percent.width - Theme.space4
                    text: Transcription.processingTitle
                    variant: "heading"
                    elide: Text.ElideRight
                }
                MorphText {
                    id: percent
                    anchors.right: parent.right
                    y: fileName.y + (fileName.height - height) / 2
                    text: Words.percent(Transcription.progress)
                    variant: "label"
                    tone: "secondary"
                }
            }
            ProgressWave {
                width: parent.width
                height: 44
                progress: Transcription.progress
                seed: Transcription.currentFile ? Transcription.currentFile.seed : 1
            }
        }
        RowList {
            id: queue
            values: Transcription.batch.files
            keyOf: f => f.name
            delegate: ListRow {
                id: queued
                required property string key
                required property int index
                readonly property var file: queue.entry(key)
                readonly property string queueState: Transcription.queueState(index)
                width: ListView.view.width
                first: index === 0 && queue.first
                iconName: file.kind === "video" ? "file-video" : "file-audio"
                title: key
                detail: Transcription.fileDetail(file)
                trailing: StatusText {
                    text: queued.queueState === "current" ? Transcription.phase : Transcription.queueLabels[queued.queueState]
                    busy: queued.queueState === "current"
                    tone: queued.queueState === "current" ? "accent" : queued.queueState === "done" ? "neutral" : "tertiary"
                    indicator: "none"
                }
            }
        }
    }

    Section {
        width: (parent.width - parent.spacing) / 2
        minimumHeaderHeight: progress.naturalHeaderHeight
        title: view.t.partialName
        caption: view.t.partialHelp

        Item {
            width: parent.width
            height: progress.naturalCardHeight
            Flickable {
                id: liveView
                x: Theme.rowInset
                y: Theme.space4
                width: parent.width - 2 * Theme.rowInset
                height: parent.height - 2 * Theme.space4
                contentHeight: live.contentHeight + Theme.space2
                contentY: Math.max(0, contentHeight - height)
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                // A mouse drags nothing on a desktop; touch still does.
                acceptedButtons: Qt.NoButton
                WheelScroll {}
                clip: true
                Behavior on contentY { enabled: Theme.movement > 0; Ease {} }

                Txt {
                    text: view.t.partialPlaceholder
                    tone: "tertiary"
                    opacity: live.length === 0 ? 1 : 0
                    Behavior on opacity { Ease { duration: Theme.fast } }
                }
                TextEdit {
                    id: live
                    width: liveView.width
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WordWrap
                    text: Transcription.partial
                    color: Theme.text
                    selectedTextColor: Theme.accentText
                    selectionColor: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTranscript
                    Accessible.name: view.t.partialName

                    Caret { edit: live }
                }
            }
        }
    }
}
