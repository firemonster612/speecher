pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "history"
import "history/HistoryWords.js" as HistoryText

// Privacy & data, under General: every choice about what Speecher keeps or
// shares, each in one place. It edits What Speecher Keeps, its days and
// Clear history itself, lists the Apps and App Types that keep less, and
// links to what refinement sees and to learning from edits, each saying
// how it is set where it lives.
Page {
    id: page
    objectName: "privacyPage"
    title: Schema.page("privacy").title
    subtitle: Schema.page("privacy").subtitle

    // The level What Speecher Keeps is lowered to while it asks about the
    // text already kept.
    property string _lowerTo: ""

    readonly property var t: ({
        deleteKeptQuestion: "Delete the text you've kept?",
        deleteKeptDetail: n => Words.count(n, "dictation or file holds", "dictations and files hold") + " text. Keep it and it goes when its days are up.",
        keepKept: "Keep it",
        changeInApps: "Change in Apps",
        // What refinement is sent besides the words, by what is on and usable.
        sees: (text, screenshot) => text && screenshot ? "Text around your cursor and a screenshot"
                                  : text ? "Text around your cursor, no screenshot"
                                  : screenshot ? "A screenshot, not the text around your cursor"
                                  : "Only your words",
        cleanupOff: "Nothing: Clean up my words is off",
        learning: (on, count) => (on ? "On, " : "Off, ") + Words.count(count, "correction", "corrections") + " kept"
    })
    // The Apps and App Types that keep less than What Speecher Keeps, App
    // Types first, each with its choice, as Pasting lists those that paste
    // differently.
    readonly property var _leftOut: Apps.types.filter(o => o.history).map(o => ({
        id: "apptype:" + o.id, name: Schema.apps.type(o.id).label, typeIcon: Schema.apps.type(o.id).icon, icon: "", history: o.history
    })).concat(Apps.entries.filter(e => e.history).map(e => ({
        id: "appentry:" + e.id, name: e.name, typeIcon: "", icon: e.icon, history: e.history
    })))
    readonly property string _refinementSees: {
        if (!Settings.values.refinementEnabled) return t.cleanupOff
        const sends = id => !!Settings.values[id] && Usable.usable(Schema.row(id))
        return t.sees(sends("targetContextControl"), sends("includeScreenshotContext"))
    }
    // Keeping less than text asks about the text already kept.
    function chooseKeeps(id) {
        const below = id === "nothing" || id === "stats"
        if (below && History.kept > 0 && (Settings.values.historyKeeps === "text" || Settings.values.historyKeeps === "audio"))
            _lowerTo = id
        else
            Settings.set("historyKeeps", id)
    }

    // What Speecher keeps of each dictation, for how long, the apps that
    // keep less, and clearing it.
    Section {
        id: historySection
        objectName: "privacySection"
        title: Schema.row("historyKeeps").section
        SchemaRow {
            id: keepsRow
            rowId: "historyKeeps"
            stacked: true
            note: (spec.options.find(o => o.id === value) || { help: "" }).help + " " + spec.help
            SegmentedControl {
                objectName: "historyKeeps"
                options: keepsRow.spec.options
                currentId: keepsRow.value ?? ""
                onActivated: id => page.chooseKeeps(id)
            }
        }
        // Keeping less than text: what is kept goes now, or when its days are up.
        InlineConfirm {
            open: page._lowerTo !== ""
            text: page.t.deleteKeptQuestion
            detail: page.t.deleteKeptDetail(History.kept)
            confirmText: Words.delete_
            cancelText: page.t.keepKept
            onConfirmed: {
                Settings.set("historyKeeps", page._lowerTo)
                if (!History.clear("all", false))
                    Toaster.show(HistoryText.text.clearFailed, { icon: "alert-triangle", tone: "negative" })
                page._lowerTo = ""
            }
            onCancelled: {
                Settings.set("historyKeeps", page._lowerTo)
                page._lowerTo = ""
            }
        }
        SchemaRow { rowId: "historyTextDays" }
        SchemaRow { rowId: "historyAudioDays" }
        SchemaRow {
            rowId: "historyExcludedApps"
            note: page._leftOut.length ? spec.help : spec.emptyText
            Button { text: page.t.changeInApps; size: "small"; onClicked: Navigation.follow("apps", "appHistory") }
        }
        RowList {
            id: leftOutList
            objectName: "historyExcludedList"
            values: page._leftOut
            first: false
            delegate: ListRow {
                id: leftOutRow
                required property string key
                required property int index
                readonly property var entry: leftOutList.entry(key)
                objectName: "leftOut_" + key
                implicitHeight: Math.max(Theme.rowHeight, leftOutWords.implicitHeight + 22)
                clickable: true
                Accessible.name: entry.name
                onClicked: Navigation.follow("apps", key)
                AppIcon {
                    id: leftOutIcon
                    anchors.verticalCenter: parent.verticalCenter
                    name: leftOutRow.entry.typeIcon ? "" : leftOutRow.entry.name
                    iconName: leftOutRow.entry.typeIcon
                    source: leftOutRow.entry.icon ? "image://appicon/" + encodeURIComponent(leftOutRow.entry.icon) : ""
                    size: 28
                }
                Column {
                    id: leftOutWords
                    x: leftOutIcon.width + Theme.space3
                    width: parent.width - x
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    Txt { width: parent.width; text: leftOutRow.entry.name; variant: "label"; elide: Text.ElideRight }
                    Txt {
                        width: parent.width
                        text: Schema.apps.historyLabel(leftOutRow.entry.history)
                        variant: "small"
                        tone: "secondary"
                        elide: Text.ElideRight
                    }
                }
                trailing: [ Icon { anchors.verticalCenter: parent.verticalCenter; name: "chevron-right"; size: 16 } ]
            }
        }
        SchemaRow {
            rowId: "clearHistory"
            note: Insights.kept > 0
                  ? Words.count(Insights.kept, "dictation", "dictations") + " counted since " + Qt.formatDate(Insights.firstDictation, "MMMM yyyy") + ", "
                    + Words.count(History.kept, "with its text", "with their text") + "."
                  : spec.help
            MenuButton {
                objectName: "clearHistory"
                label: HistoryText.text.clear
                items: HistoryText.text.clearRanges
                onPicked: id => { if (!clearConfirm.ask(id)) confirm(HistoryText.text.nothingToClear) }
            }
        }
        ClearConfirm { id: clearConfirm }
    }

    // What it sends or holds besides History. Links, not copies: each
    // setting has one home, and this says how it is set there.
    Section {
        title: Schema.row("privacyLastText").section
        SchemaRow {
            rowId: "privacyRefinementSees"
            trailingText: page._refinementSees
            clickable: true
            onClicked: Navigation.follow("refinement", "targetContextControl")
        }
        SchemaRow {
            rowId: "privacyLearnedCorrections"
            trailingText: page.t.learning(!!Settings.values.correctionLearningControl, Library.vocabulary.filter(e => e.learned).length)
            clickable: true
            onClicked: Navigation.follow("vocabulary", "correctionLearningControl")
        }
        SchemaRow { rowId: "privacyLastText" }
    }
}
