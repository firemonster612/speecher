pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "history"
import "history/HistoryWords.js" as T

// Everything History kept, newest first, under Home: a search with filters,
// then a card of entries a day, each opening in place. A Recording opens in
// its reader instead, in the list's place ("recording:<id>" opens it from a
// link); one still running opens Recordings. "historyKind:<kind>" opens it
// filtered to a kind. Clear… deletes a span of dictations and files, as
// Privacy & data's Clear history does. With nothing to list it says why:
// History keeps nothing or only stats, nothing kept yet, or nothing
// matches. Search's "Search History for …" opens it with those words
// searched.
Page {
    id: page
    title: Schema.page("history").title
    subtitle: Schema.page("history").subtitle

    readonly property var t: T.text
    readonly property string keeps: Settings.values.historyKeeps ?? "stats"
    // Where What Speecher keeps is set, wherever its row lives.
    function openPrivacy() { Navigation.follow(Schema.row("historyKeeps").page, "historyKeeps") }
    // Entries with words to list, whatever the query.
    readonly property bool listing: History.kept > 0
    readonly property bool narrowed: search.text.length > 0 || filters.active
    // The entry open, one at a time; 0 for none.
    property real openId: 0
    property bool askKeepText: false
    // The last delete's Undo, while it can still be undone.
    property int lastUndo: -1
    // The row items by their place in History.results, for Up and Down.
    property var rowItems: ({})
    // The Recording open in the reader, 0 for none, and the line it opened at.
    property real readingId: 0
    property real readAt: -1

    // The results a day at a time: [{ day, rows: [{ entry, index }] }],
    // index being the entry's place in History.results.
    readonly property var days: {
        const groups = []
        History.results.forEach((entry, index) => {
            if (!groups.length || groups[groups.length - 1].day !== entry.day) groups.push({ day: entry.day, rows: [] })
            groups[groups.length - 1].rows.push({ entry: entry, index: index })
        })
        return groups
    }

    function read(id, atMs) {
        const entry = History.results.find(r => r.id === id) || History.entry(id)
        if (entry.live) {
            Navigation.follow("recordings", "recordingControl")
            return
        }
        readAt = atMs
        readingId = id
    }
    function remove(id) {
        if (openId === id) openId = 0
        const entry = History.results.find(r => r.id === id)
        // A Recording asks first, in its reader, as it may take its files.
        if (entry && entry.kind === "recording") {
            read(id, -1)
            reader.askDelete = true
            return
        }
        const token = History.remove(id)
        if (token < 0) return
        lastUndo = token
        // A file's audio and saved transcript stay where they are.
        Toaster.show(entry && entry.kind === "file" ? page.t.removed : Words.deleted(T.shortened(entry ? entry.text : "")),
                     { icon: "trash-2", actionText: Words.undo, action: () => page.undo() })
    }
    function undo() {
        if (lastUndo >= 0 && History.undo(lastUndo)) lastUndo = -1
    }
    function stepFocus(index, step) {
        const row = rowItems[index + step]
        if (row) {
            row.rowItem.forceActiveFocus()
            page.reveal(row)
        }
    }
    function clearSearch() {
        search.clear()
        filters.reset()
    }

    // A link lands here as "recording:<id>", which opens that recording's
    // reader, as "historyKind:<kind>", which shows that kind alone, or as
    // "historyFind:<words>" from the search palette, which types them into
    // the search.
    function _land(rowId) {
        if (rowId.startsWith("recording:")) {
            read(Number(rowId.slice("recording:".length)), -1)
        } else if (rowId.startsWith("historyKind:")) {
            readingId = 0
            search.clear()
            filters.reset()
            filters.kind = rowId.slice("historyKind:".length)
            Navigation.pendingRow = ""
        } else if (rowId.startsWith("historyFind:")) {
            search.text = rowId.slice("historyFind:".length)
            filters.reset()
            Navigation.pendingRow = ""
            search.forceActiveFocus()
        }
    }
    Component.onCompleted: {
        _land(Navigation.pendingRow)
        if (listing && !readingId && !search.activeFocus) search.forceActiveFocus()
    }
    Connections { target: Navigation; function onPendingRowChanged() { page._land(Navigation.pendingRow) } }
    // Fetches the next page as the end comes into view.
    Connections {
        target: page.flickable
        function onContentYChanged() {
            const flick = page.flickable
            if (flick.contentY + flick.height > flick.contentHeight - 600) History.fetchMore()
        }
    }
    Binding {
        target: History
        property: "query"
        value: ({ text: search.text, kind: filters.kind, app: filters.app, profile: filters.profile, when: filters.when })
    }
    Shortcut { sequences: [StandardKey.Find, "/"]; enabled: page.listing && !page.readingId; onActivated: search.forceActiveFocus() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: page.lastUndo >= 0; onActivated: page.undo() }

    headerActions: MenuButton {
        objectName: "historyClear"
        label: page.t.clear
        leadingIcon: "trash-2"
        items: page.t.clearRanges
        onPicked: id => { if (!clearConfirm.ask(id)) confirm(page.t.nothingToClear) }
    }

    Switcher {
        width: parent.width
        currentIndex: page.readingId > 0 ? 1 : 0

        Column {
            spacing: Theme.sectionGap
            // What History keeps, and where that is set.
            Flow {
                width: parent.width
                spacing: Theme.space2
                Txt { text: T.keeping(page.keeps, Number(Settings.values.historyTextDays), Number(Settings.values.historyAudioDays)); tone: "secondary" }
                TextLink { text: page.t.settings; onActivated: page.openPrivacy() }
            }

            ClearConfirm { id: clearConfirm; ruled: false; inset: 0 }

            Reveal {
                open: page.listing
                Column {
                    id: finding
                    width: parent.width
                    spacing: Theme.space3
                    SearchLanding { rowIds: ["historyList"]; target: finding; onLanded: search.forceActiveFocus() }
                    SearchField {
                        id: search
                        objectName: "historySearch"
                        width: parent.width
                        placeholderText: page.t.search
                        clearText: page.t.clearSearch
                        Keys.onEscapePressed: event => {
                            if (search.text.length) search.clear()
                            else if (filters.active) filters.reset()
                            else page.forceActiveFocus()
                        }
                        Keys.onDownPressed: page.stepFocus(-1, 1)
                    }
                    HistoryFilters { id: filters; width: parent.width }
                    Txt {
                        visible: page.narrowed
                        text: T.matches(History.matches)
                        variant: "small"
                        tone: "secondary"
                    }
                }
            }

            Repeater {
                model: page.days
                Section {
                    id: day
                    required property var modelData
                    title: T.dayHeading(modelData.day, Insights.today)
                    Repeater {
                        model: day.modelData.rows
                        HistoryRow {
                            id: historyRow
                            required property var modelData
                            required property int index
                            readonly property int place: modelData.index
                            entry: modelData.entry
                            first: index === 0
                            open: page.openId === entry.id
                            onToggled: page.openId = open ? 0 : entry.id
                            onReadRequested: atMs => page.read(entry.id, atMs)
                            onDeleteRequested: page.remove(entry.id)
                            onStepFocus: step => page.stepFocus(place, step)
                            Component.onCompleted: page.rowItems[place] = historyRow
                            Component.onDestruction: if (page.rowItems[place] === historyRow) delete page.rowItems[place]
                        }
                    }
                }
            }

            // Nothing to list, and why.
            Reveal {
                open: !page.listing
                Column {
                    width: parent.width
                    spacing: Theme.space3
                    EmptyState {
                        width: parent.width
                        iconName: "history"
                        title: page.keeps === "nothing" ? page.t.offTitle : page.keeps === "stats" ? page.t.statsTitle : page.t.emptyTitle
                        text: page.keeps === "nothing" ? page.t.offText : page.keeps === "stats" ? page.t.statsText : page.t.emptyText
                        Button {
                            visible: page.keeps === "nothing"
                            text: page.t.settings
                            size: "small"
                            onClicked: page.openPrivacy()
                        }
                        Button {
                            visible: page.keeps === "stats"
                            text: page.t.keepText
                            size: "small"
                            onClicked: page.askKeepText = true
                        }
                    }
                    InlineConfirm {
                        open: page.askKeepText
                        ruled: false
                        text: page.t.keepTextQuestion
                        detail: T.keepTextDetail(Number(Settings.values.historyTextDays))
                        confirmText: page.t.keepTextConfirm
                        confirmIcon: "check"
                        variant: "primary"
                        cancelText: Words.cancel
                        onCancelled: page.askKeepText = false
                        onConfirmed: {
                            Settings.set("historyKeeps", "text")
                            page.askKeepText = false
                        }
                    }
                }
            }
            Reveal {
                open: page.listing && History.results.length === 0 && !History.searching
                EmptyState {
                    width: parent.width
                    iconName: "search"
                    title: T.noMatch(search.text)
                    Button { visible: page.narrowed; text: page.t.clearFilters; size: "small"; onClicked: page.clearSearch() }
                }
            }
        }

        RecordingReader {
            id: reader
            entryId: page.readingId
            startAt: page.readAt
            find: search.text
            onClosed: page.readingId = 0
        }
    }
}
