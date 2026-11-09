pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import "HistoryWords.js" as W

// A transcript's lines, a row each: for a Recording, when its text was
// finalised, who said it and what, me in stronger ink than them; for a
// file (timed false), its paragraphs alone. Used by History's reader and
// by Recordings while a recording runs. A line's words can be selected and
// copied. Find highlights the lines that hold any
// part of it, parted as History's search parts it (quoted phrases whole),
// and steps through them with Return and Shift+Return. live follows new lines
// while the list is at its end and offers Jump to latest once scrolled up.
// With audio, a line plays from just before its time, since a line's time is
// when its text was finalised.
Column {
    id: reader

    // A list or a ListModel of { atMs, speaker, text }.
    property var model: []
    property bool live: false
    // Lines with a time and a speaker, as a Recording's are.
    property bool timed: true
    // The History Entry whose audio a line plays; 0 for none.
    property real audioEntry: 0
    property int maxHeight: 420
    property alias findText: find.text
    readonly property alias findField: find

    // A list from C++ is a sequence, not a JS array, so a ListModel is
    // told apart by its get().
    readonly property bool _listModel: typeof model.get === "function"
    readonly property int count: _listModel ? model.count : model.length
    function lineAt(i) { return _listModel ? model.get(i) : model[i] }
    // What Find holds, as History's search parts it, each lower case.
    readonly property var terms: History.searchTerms(find.text)
    // Indexes of the lines that hold any of them, as History's results show
    // a recording's lines.
    readonly property var matches: {
        const found = []
        if (!terms.length) return found
        for (let i = 0; i < count; ++i) {
            const line = lineAt(i).text.toLowerCase()
            if (terms.some(term => line.indexOf(term) >= 0)) found.push(i)
        }
        return found
    }
    property int current: 0
    property bool following: true
    onMatchesChanged: current = 0

    function step(by) {
        if (!matches.length) return
        current = (current + by + matches.length) % matches.length
        list.positionViewAtIndex(matches[current], ListView.Center)
    }
    // Shows the line finalised at atMs, or the last before it, in the middle.
    function showAt(atMs) {
        let index = 0
        for (let i = 0; i < count && lineAt(i).atMs <= atMs; ++i) index = i
        list.positionViewAtIndex(index, ListView.Center)
    }
    function jumpToLatest() {
        following = true
        list.positionViewAtEnd()
    }
    // Text with each part that Find holds in bold, every piece escaped;
    // parts that overlap are one.
    function marked(text) {
        const lower = text.toLowerCase()
        const bold = new Array(text.length).fill(false)
        for (const term of terms) {
            for (let at = lower.indexOf(term); at >= 0; at = lower.indexOf(term, at + 1)) bold.fill(true, at, at + term.length)
        }
        let out = ""
        for (let at = 0; at < text.length;) {
            let end = at
            while (end < text.length && bold[end] === bold[at]) ++end
            const run = RichText.escape(text.slice(at, end))
            out += bold[at] ? "<b>" + run + "</b>" : run
            at = end
        }
        return out
    }

    spacing: Theme.space2
    width: parent ? parent.width : implicitWidth

    Row {
        x: Theme.rowInset
        width: parent.width - Theme.rowInset * 2
        spacing: Theme.space3
        SearchField {
            id: find
            objectName: "transcriptFind"
            width: Math.min(Theme.fieldWidth, parent.width - found.width - Theme.space3)
            placeholderText: W.text.findInTranscript
            clearText: W.text.clearSearch
            Keys.onReturnPressed: event => reader.step(event.modifiers & Qt.ShiftModifier ? -1 : 1)
            Keys.onEnterPressed: event => reader.step(event.modifiers & Qt.ShiftModifier ? -1 : 1)
        }
        Row {
            id: found
            spacing: Theme.space1
            anchors.verticalCenter: parent.verticalCenter
            opacity: reader.matches.length || find.text.length ? 1 : 0
            Behavior on opacity { Ease {} }
            Txt {
                text: W.foundOf(reader.matches.length ? reader.current + 1 : 0, reader.matches.length)
                variant: "small"
                tone: "secondary"
                anchors.verticalCenter: parent.verticalCenter
            }
            IconButton { iconName: "chevron-up"; size: 26; tooltip: W.text.previous; enabled: reader.matches.length > 0; onClicked: reader.step(-1) }
            IconButton { iconName: "chevron-down"; size: 26; tooltip: W.text.next; enabled: reader.matches.length > 0; onClicked: reader.step(1) }
        }
    }

    ListView {
        id: list
        objectName: "transcriptLines"
        width: parent.width
        height: Math.min(Math.max(contentHeight, 1), reader.maxHeight)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: reader.model
        T.ScrollBar.vertical: ScrollBar {}
        onCountChanged: if (reader.live && reader.following) Qt.callLater(positionViewAtEnd)
        onMovementEnded: reader.following = atYEnd
        delegate: Item {
            id: line
            required property int index
            // A line of a model being emptied is gone before its row.
            readonly property var entry: reader.lineAt(index) || ({ atMs: 0, speaker: "", text: "" })
            readonly property bool matched: reader.matches.length > 0 && reader.matches[reader.current] === index
            width: list.width
            implicitHeight: words.implicitHeight + Theme.space2
            HoverHandler { id: hover }
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: Theme.washInset
                anchors.rightMargin: Theme.washInset
                radius: Theme.washRadius
                color: line.matched ? Theme.fill : "transparent"
                Behavior on color { EaseColor {} }
            }
            Txt {
                id: time
                visible: reader.timed
                x: Theme.rowInset
                y: Theme.space1
                text: W.lineClock(line.entry.atMs)
                variant: "code"
                tone: "tertiary"
            }
            Txt {
                id: speaker
                visible: reader.timed
                x: time.x + 84
                y: Theme.space1
                width: 52
                text: line.entry.speaker
                tone: "secondary"
                strong: line.entry.speaker === "me"
            }
            // A TextEdit, not a Txt, so its words can be selected and copied.
            TextEdit {
                id: words
                x: reader.timed ? speaker.x + speaker.width + Theme.space2 : Theme.rowInset
                y: Theme.space1
                width: parent.width - x - Theme.rowInset - (play.visible ? play.width + Theme.space2 : 0)
                readOnly: true
                selectByMouse: true
                activeFocusOnPress: true
                // A TextEdit has no lineHeight; the transcript type's 1.25 comes as CSS.
                text: "<div style=\"line-height:125%\">" + reader.marked(line.entry.text) + "</div>"
                textFormat: TextEdit.RichText
                wrapMode: TextEdit.Wrap
                color: Theme.text
                selectionColor: Theme.accent
                selectedTextColor: Theme.accentText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTranscript
                font.weight: line.entry.speaker === "me" ? Font.DemiBold : Font.Normal
            }
            IconButton {
                id: play
                visible: reader.audioEntry > 0
                opacity: hover.hovered || activeFocus ? 1 : 0
                anchors.right: parent.right
                anchors.rightMargin: Theme.rowInset
                y: Theme.space1 - 4
                iconName: "play"
                size: 26
                tooltip: W.text.playFromHere
                onClicked: History.play(reader.audioEntry, Math.max(0, line.entry.atMs - 3000))
            }
        }
    }

    // Where a live transcript is: following, or scrolled up with the way back.
    Item {
        visible: reader.live
        width: parent.width
        implicitHeight: Math.max(followText.implicitHeight, jump.implicitHeight)
        Txt {
            id: followText
            anchors.right: parent.right
            anchors.rightMargin: Theme.rowInset
            text: W.text.following
            variant: "small"
            tone: "tertiary"
            opacity: reader.following ? 1 : 0
            Behavior on opacity { Ease {} }
        }
        Button {
            id: jump
            anchors.right: parent.right
            anchors.rightMargin: Theme.rowInset
            text: W.text.jumpToLatest
            iconName: "arrow-down"
            size: "small"
            opacity: reader.following ? 0 : 1
            visible: opacity > 0
            Behavior on opacity { Ease {} }
            onClicked: reader.jumpToLatest()
        }
    }
}
