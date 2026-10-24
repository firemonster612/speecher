pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Launch
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "text"

Page {
    id: page
    objectName: "vocabularyPage"
    title: Schema.page("vocabulary").title
    subtitle: Schema.page("vocabulary").subtitle

    // Preview state: --vocab=terms|corrections|replacements picks the list,
    // --vocab-form=add|edit opens its form.

    // The list on show.
    property string tab: Launch.arg("vocab", "terms")

    // Each list with the schema rows it shows, so a search jump to one of
    // them opens the list.
    readonly property var kinds: [
        { id: "terms", label: Library.termsCount,
          caption: Schema.row("vocabularyEntries").help,
          rowIds: ["vocabularyEntries", "vocabularyKeyTerm", "vocabularyContext", "vocabularyProfiles", "vocabularyLimit"] },
        { id: "corrections", label: Library.correctionsCount,
          caption: [Schema.row("learnedCorrections").help, Library.correctionsInUseText].filter(s => s).join(" "),
          rowIds: ["correctionLearningControl", "learnedCorrections"] },
        { id: "replacements", label: Library.replacementsCount,
          caption: Library.replacementsCaption,
          rowIds: ["bindingRules"] }
    ]
    // What the chosen list holds, above its card.
    component Caption: Txt { x: 2; width: parent.width - 4; variant: "small"; tone: "secondary"; wrapMode: Text.WordWrap }

    function _openFor(rowId) {
        const kind = kinds.find(k => k.rowIds.includes(rowId))
        if (kind) tab = kind.id
    }
    Component.onCompleted: _openFor(Navigation.pendingRow)
    Connections { target: Navigation; function onPendingRowChanged() { page._openFor(Navigation.pendingRow) } }

    Column {
        width: parent.width
        spacing: Theme.space3

        // The switch between the three lists, and the chosen list's actions,
        // which slide along with it.
        Item {
            width: parent.width
            height: Math.max(tabs.height, actions.height)
            SegmentedControl {
                id: tabs
                objectName: "vocabTabs"
                anchors.verticalCenter: parent.verticalCenter
                options: page.kinds.map(k => ({ id: k.id, label: k.label }))
                currentId: page.tab
                onActivated: id => page.tab = id
            }
            Switcher {
                id: actions
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - tabs.width - Theme.space4
                currentIndex: tabs.currentIndex
                Row {
                    layoutDirection: Qt.RightToLeft
                    spacing: Theme.space2
                    Button { text: Library.vocabularyText.addTerm; iconName: "plus"; size: "small"; onClicked: terms.startAdd() }
                    Button { text: Library.vocabularyText.importTerms; iconName: "file-up"; size: "small"; onClicked: Library.importTerms() }
                }
                Row {
                    layoutDirection: Qt.RightToLeft
                    Button {
                        text: Library.vocabularyText.undoCorrection
                        iconName: "undo-2"
                        size: "small"
                        enabled: Library.corrections.length > 0
                        onClicked: Library.undoLastCorrection()
                    }
                }
                Row {
                    layoutDirection: Qt.RightToLeft
                    spacing: Theme.space2
                    Button { text: Library.vocabularyText.addReplacement; iconName: "plus"; size: "small"; onClicked: replacements.startAdd() }
                    Button { text: Library.vocabularyText.importReplacements; iconName: "file-up"; size: "small"; onClicked: Library.importReplacements() }
                }
            }
        }

        Switcher {
            currentIndex: tabs.currentIndex

            Column {
                spacing: Theme.space3
                Caption { text: page.kinds[0].caption }
                VocabTerms { id: terms; startForm: Launch.arg("vocab-form", "") }
            }
            Column {
                spacing: Theme.space3
                Caption { text: page.kinds[1].caption }
                VocabCorrections {}
            }
            Column {
                spacing: Theme.space3
                Caption { text: page.kinds[2].caption }
                VocabReplacements { id: replacements; startForm: Launch.arg("vocab-form", "") }
            }
        }
    }
}
