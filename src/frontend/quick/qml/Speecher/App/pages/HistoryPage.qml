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
// then a card of entries a day, each opening in place. Clear… deletes a span
// of it, as General's Clear history does. With nothing to list it says why:
// History keeps nothing or only stats, nothing kept yet, or nothing matches.
// Search's "Search History for …" opens it with those words searched.
Page {
    id: page
    title: Schema.page("history").title
    subtitle: Schema.page("history").subtitle

    readonly property var t: T.text
    readonly property string keeps: Settings.values.historyKeeps ?? "stats"
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

    function remove(id) {
        if (openId === id) openId = 0
        const entry = History.results.find(r => r.id === id)
        const token = History.remove(id)
        if (token < 0) return
        lastUndo = token
        Toaster.show(Words.deleted(T.shortened(entry ? entry.text : "")),
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

    // A search palette result lands here as "historyFind:<words>", which
    // types them into the search.
    function _land(rowId) {
        if (!rowId.startsWith("historyFind:")) return
        search.text = rowId.slice("historyFind:".length)
        filters.reset()
        Navigation.pendingRow = ""
        search.forceActiveFocus()
    }
    Component.onCompleted: {
        if (listing) search.forceActiveFocus()
        _land(Navigation.pendingRow)
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
    Shortcut { sequences: [StandardKey.Find, "/"]; enabled: page.listing; onActivated: search.forceActiveFocus() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: page.lastUndo >= 0; onActivated: page.undo() }

    headerActions: MenuButton {
        objectName: "historyClear"
        label: page.t.clear
        leadingIcon: "trash-2"
        items: page.t.clearRanges
        onPicked: id => { if (!clearConfirm.ask(id)) confirm(page.t.nothingToClear) }
    }

    // What History keeps, and where that is set.
    Flow {
        width: parent.width
        spacing: Theme.space2
        Txt { text: T.keeping(page.keeps, Number(Settings.values.historyTextDays), Number(Settings.values.historyAudioDays)); tone: "secondary" }
        TextLink { text: page.t.settings; onActivated: Navigation.follow("general", "historyKeeps") }
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
                    onClicked: Navigation.follow("general", "historyKeeps")
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
