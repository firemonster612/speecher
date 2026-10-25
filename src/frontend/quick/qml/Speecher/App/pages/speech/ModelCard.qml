pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// One Local Model in the library, on its own card: what it is and how it
// rates, with the one step that moves it on (download, use) or its download's
// progress. The summary is a disclosure header: click it, or Return or Space
// on it, to open the facts, pros and cons, a speed test and deletion, which
// asks in place before it removes anything. The model in use says so with a
// check; opening a card doesn't choose it.
Card {
    id: card

    required property var entry           // a Models.catalog entry
    property bool expanded: false

    readonly property var words: Models.text
    readonly property bool downloaded: Models.downloaded.indexOf(entry.id) >= 0
    readonly property bool inUse: Models.inUse === entry.id
    readonly property bool suggested: Models.suggestedModel === entry.id
    readonly property var progress: Models.downloads[entry.id]
    readonly property bool downloading: progress !== undefined
    property bool confirmingDelete: false

    // The step on the right, in the order a model moves through them, so
    // the next one slides in from the right.
    readonly property int stage: inUse ? 3 : downloaded ? 2 : downloading ? 1 : entry.fit === "tooLarge" ? 4 : 0

    // The facts under the summary, each labelled by its key in Models.text.
    // Best for is under the name already unless the suggestion took its place.
    readonly property var factKeys: (suggested ? ["bestFor"] : []).concat(["wordErrors", "speed", "languages", "licence", "file"])
    function fact(key) {
        switch (key) {
        case "bestFor": return entry.bestFor
        case "wordErrors": return words.wordErrorsValue.arg(entry.werClear).arg(entry.werEveryday)
        case "speed": return Models.speedLine(entry)
        case "languages": return entry.languages
        case "licence": return entry.licence
        case "file": return entry.fileName
        }
        return ""
    }

    function toggle() { expanded = !expanded }

    // Summary --------------------------------------------------------------
    // Only the summary opens and closes the card, so the details take clicks
    // and selections of their own.
    Item {
        id: summary
        width: parent.width
        height: 84
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: card.entry.name
        Accessible.expandable: true
        Accessible.expanded: card.expanded
        Accessible.onPressAction: card.toggle()
        Keys.onReturnPressed: { focusVisible.keyed(); card.toggle() }
        Keys.onSpacePressed: { focusVisible.keyed(); card.toggle() }
        FocusVisible { id: focusVisible }

        Rectangle {
            id: wash
            anchors.fill: parent
            anchors.margins: Theme.washInset
            radius: Theme.washRadius
            color: summaryTap.pressed ? Theme.fillStrong : summaryHover.hovered ? Theme.fill : "transparent"
            Behavior on color { EaseColor {} }
            FocusRing { target: wash; shown: focusVisible.shown }
        }
        HoverHandler { id: summaryHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { id: summaryTap; onTapped: { focusVisible.pointed(); card.toggle() } }

        Column {
            x: Theme.space5
            width: trailing.x - x - Theme.space5
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1

            Row {
                width: parent.width
                spacing: Theme.space3
                Txt { id: name; text: card.entry.name; variant: "heading" }
                Txt {
                    width: parent.width - name.width - parent.spacing
                    anchors.baseline: name.baseline
                    text: card.entry.ratingLabel
                    variant: "small"
                    tone: "secondary"
                    elide: Text.ElideRight
                }
            }
            Txt {
                width: parent.width
                text: card.suggested ? card.words.suggested : card.entry.bestFor
                variant: "small"
                tone: card.suggested ? "primary" : "secondary"
                strong: card.suggested
                elide: Text.ElideRight
            }
            Txt {
                width: parent.width
                text: Models.metaLine(card.entry)
                variant: "small"
                tone: "tertiary"
                elide: Text.ElideRight
            }
        }

        Row {
            id: trailing
            anchors.right: parent.right
            anchors.rightMargin: Theme.space4
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space5

            RatingBars {
                bars: card.entry.bars
                pipWidth: 7
                color: card.inUse ? Theme.accent : Theme.textSecondary
                anchors.verticalCenter: parent.verticalCenter
            }

            Switcher {
                width: 172
                currentIndex: card.stage
                anchors.verticalCenter: parent.verticalCenter

                Item {
                    implicitHeight: download.implicitHeight
                    Button {
                        id: download
                        anchors.right: parent.right
                        iconName: "download"
                        text: card.words.download.arg(card.entry.size)
                        size: "small"
                        onClicked: Models.download(card.entry.id)
                    }
                }
                Column {
                    spacing: Theme.space1
                    Item {
                        width: parent.width
                        height: cancel.height
                        // A plain line: it changes every tick, too often for
                        // MorphText's roll to settle.
                        Txt {
                            anchors.verticalCenter: parent.verticalCenter
                            text: card.words.progress.arg(Models.formatBytes(card.entry.sizeBytes * (card.progress || 0))).arg(card.entry.size)
                            variant: "mono"
                            tone: "secondary"
                        }
                        IconButton {
                            id: cancel
                            anchors.right: parent.right
                            size: 22
                            iconName: "x"
                            tooltip: Words.cancel
                            onClicked: Models.cancel(card.entry.id)
                        }
                    }
                    ProgressBar { width: parent.width; value: card.progress || 0 }
                }
                Item {
                    implicitHeight: use.implicitHeight
                    Button {
                        id: use
                        anchors.right: parent.right
                        size: "small"
                        variant: "primary"
                        text: card.words.useModel
                        onClicked: Models.use(card.entry.id)
                    }
                }
                Item {
                    implicitHeight: Theme.controlHeight
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.space1
                        Icon { name: "check"; size: 15; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                        Txt { text: card.words.inUse; variant: "label"; tone: "accent"; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
                Txt {
                    horizontalAlignment: Text.AlignRight
                    text: card.words.tooLarge
                    variant: "small"
                    tone: "tertiary"
                    wrapMode: Text.WordWrap
                }
            }

            Icon {
                name: "chevron-down"
                size: 16
                color: Theme.textTertiary
                rotation: card.expanded ? 180 : 0
                anchors.verticalCenter: parent.verticalCenter
                Behavior on rotation { Ease {} }
            }
        }
    }

    // Details ------------------------------------------------------------------
    Reveal {
        id: details
        open: card.expanded

        Column {
            x: Theme.space5
            width: parent.width - Theme.space5 * 2
            spacing: Theme.space4
            bottomPadding: Theme.space5

            Rectangle { width: parent.width; height: 1; color: Theme.rule }

            // Facts, three columns of label over value.
            Grid {
                id: facts
                width: parent.width
                columns: 3
                rowSpacing: Theme.space3
                columnSpacing: Theme.space4
                Repeater {
                    model: card.factKeys
                    Column {
                        id: fact
                        required property string modelData
                        width: (facts.width - facts.columnSpacing * (facts.columns - 1)) / facts.columns
                        spacing: 2
                        Txt { text: card.words[fact.modelData]; variant: "small"; tone: "secondary" }
                        Txt { width: parent.width; text: card.fact(fact.modelData); variant: "small"; wrapMode: Text.WrapAnywhere }
                    }
                }
            }

            // Pros and cons.
            Row {
                id: notes
                width: parent.width
                spacing: Theme.space4
                Repeater {
                    model: [{ items: card.entry.pros, icon: "plus" }, { items: card.entry.cons, icon: "minus" }]
                    Column {
                        id: side
                        required property var modelData
                        width: (notes.width - notes.spacing) / 2
                        spacing: Theme.space1
                        Repeater {
                            model: side.modelData.items
                            Row {
                                id: note
                                required property string modelData
                                spacing: Theme.space2
                                Icon { id: mark; y: 1; name: side.modelData.icon; size: 13; color: Theme.textTertiary }
                                Txt { width: side.width - mark.width - note.spacing; text: note.modelData; variant: "small"; wrapMode: Text.WordWrap }
                            }
                        }
                    }
                }
            }

            // Why it can't run here.
            Reveal {
                open: !!card.entry.problem
                Notice { text: card.entry.problem || ""; ruled: false }
            }

            // Test and delete, or the question before deleting.
            Reveal {
                open: card.downloaded && !card.confirmingDelete
                Row {
                    spacing: Theme.space2
                    Button {
                        size: "small"
                        iconName: "gauge"
                        text: card.words.testSpeed
                        busy: Models.speedTestRunning === card.entry.id
                        enabled: !busy
                        onClicked: Models.testSpeed(card.entry.id)
                    }
                    Button {
                        size: "small"
                        variant: "ghost"
                        iconName: "trash-2"
                        text: card.words.deleteModel
                        onClicked: card.confirmingDelete = true
                    }
                }
            }
            InlineConfirm {
                open: card.downloaded && card.confirmingDelete
                inset: 0
                ruled: false
                text: card.words.deleteQuestion.arg(card.entry.name)
                detail: card.words.deleteBody
                confirmText: card.words.deleteModel
                cancelText: Words.cancel
                onCancelled: card.confirmingDelete = false
                onConfirmed: {
                    card.confirmingDelete = false
                    Models.remove(card.entry.id)
                }
            }
        }
    }
}
