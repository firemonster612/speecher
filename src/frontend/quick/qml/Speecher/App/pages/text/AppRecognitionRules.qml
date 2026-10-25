pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Launch
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.App

// Application recognition: which app type and Writing Profile an app gets.
// Your own matches as rows, with the built-in ones as chips to start from.
// --pasting-form=match opens the form, --built-ins=open the built-ins.
Section {
    id: section

    readonly property var categoryOptions: [{ id: "", label: Library.pastingText.automatic }].concat(Library.appCategories.map(c => ({ id: c.id, label: c.label, icon: c.icon })))
    readonly property var profileOptions: [{ id: "", label: Library.pastingText.automatic }].concat(Library.profiles.map(p => ({ id: p.id, label: p.name, icon: p.icon })))
    // The page scrolls to item, for the form a built-in chip opens.
    signal revealRequested(Item item)

    title: Schema.row("appRecognitionRules").label
    caption: Schema.row("appRecognitionRules").help
    actions: [ Button { text: Library.pastingText.addMatch; iconName: "plus"; size: "small"; onClicked: matchForm.startAdd("") } ]
    SearchLanding { rowIds: ["appRecognitionRules"]; target: section }

    InlineForm {
        cancelText: Words.cancel
        id: matchForm
        property string category: ""
        property string profile: ""
        function startAdd(match) { matchText.text = match; category = ""; profile = ""; open = true }
        title: Library.pastingText.newMatch
        saveText: Library.pastingText.addMatch
        canSave: matchText.text.trim().length > 0 && (category.length > 0 || profile.length > 0)
        open: Launch.arg("pasting-form", "") === "match"
        onCancelled: open = false
        onSaved: {
            Library.add("recognitionRules", { match: matchText.text.trim(), category: category, profile: profile })
            open = false
        }
        FormField {
            label: Library.pastingText.matchApp
            help: Library.pastingText.matchAppHelp
            TextField { id: matchText; width: parent.width; iconName: "scan-search" }
        }
        Row {
            width: parent.width
            spacing: Theme.space4
            FormField {
                width: (parent.width - parent.spacing) / 2
                label: Library.pastingText.appType
                help: Library.pastingText.appTypeHelp
                Select { width: parent.width; options: section.categoryOptions; currentId: matchForm.category; onChosen: id => matchForm.category = id }
            }
            FormField {
                width: (parent.width - parent.spacing) / 2
                label: Library.pastingText.profile
                help: Library.pastingText.profileHelp
                Select { width: parent.width; options: section.profileOptions; currentId: matchForm.profile; onChosen: id => matchForm.profile = id }
            }
        }
    }

    // Column heads over the matches, inset to line up with the rows' content.
    Reveal {
        id: heads
        readonly property real typeX: Theme.rowInset + (width - Theme.rowInset * 2) * 0.42
        open: Library.recognitionRules.length > 0
        Item {
            width: parent.width
            height: 40
            Txt { x: Theme.rowInset; anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.space2; text: Library.pastingText.app; variant: "small"; tone: "secondary" }
            Txt { x: heads.typeX; anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.space2; text: Library.pastingText.appType; variant: "small"; tone: "secondary" }
            Txt { x: heads.typeX + 196; anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.space2; text: Library.pastingText.profile; variant: "small"; tone: "secondary" }
        }
    }
    RowList {
        id: matches
        values: Library.recognitionRules
        keyOf: r => r.match
        delegate: ListRow {
            id: matchRow
            required property string key
            required property int index
            readonly property var rule: matches.entry(key)
            width: ListView.view.width
            first: index === 0 && matches.first
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                AppIcon { name: matchRow.rule.match.split(".").pop(); size: 24; anchors.verticalCenter: parent.verticalCenter }
                Txt { text: matchRow.rule.match; variant: "label"; anchors.verticalCenter: parent.verticalCenter }
            }
            Select {
                x: heads.typeX - Theme.rowInset
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 184
                options: section.categoryOptions
                currentId: matchRow.rule.category
                onChosen: id => Library.update("recognitionRules", matchRow.index, { category: id })
            }
            Select {
                x: heads.typeX - Theme.rowInset + 196
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 170
                options: section.profileOptions
                currentId: matchRow.rule.profile
                onChosen: id => Library.update("recognitionRules", matchRow.index, { profile: id })
            }
            actions: [
                IconButton { iconName: "trash-2"; size: 28; tooltip: Library.pastingText.deleteMatch; onClicked: Library.remove("recognitionRules", matchRow.index) }
            ]
        }
    }
    Reveal {
        open: Library.recognitionRules.length === 0
        EmptyState {
            iconName: "scan-search"
            title: Library.pastingText.noMatches
            text: Library.pastingText.noMatchesHelp
            topPadding: Theme.space5
            bottomPadding: Theme.space5
        }
    }

    // Built-in matches as chips: hover one for what it sets, click it to
    // start a match of your own that overrides it.
    Disclosure {
        objectName: "builtInMatches"
        title: Library.pastingText.builtInMatches
        help: Library.pastingText.builtInMatchesHelp
        trailingText: Library.builtInMatchesCount
        open: Launch.arg("built-ins", "") === "open"

        Item {
            width: parent.width
            height: builtInSearch.height + Theme.space2
            SearchField { clearText: Words.clear; id: builtInSearch; x: Theme.rowInset; width: parent.width - Theme.rowInset * 2; placeholderText: Library.pastingText.searchBuiltIn }
        }
        Flow {
            x: Theme.rowInset
            width: parent.width - Theme.rowInset * 2
            spacing: 6
            topPadding: Theme.space2
            bottomPadding: Theme.space4
            add: Transition { Ease { property: "opacity"; from: 0; to: 1 } }
            move: Transition { enabled: Theme.movement > 0; Ease { properties: "x,y"; duration: Theme.slow } }
            // Every chip stays; ones the search leaves out fade, then give
            // up their place so the rest move along.
            Repeater {
                model: Library.builtInRecognitionRules
                Chip {
                    id: builtIn
                    required property var modelData
                    readonly property bool found: !builtInSearch.text || modelData.match.includes(builtInSearch.text.toLowerCase())
                    opacity: found ? 1 : 0
                    visible: found || opacity > 0
                    Behavior on opacity { Ease { duration: Theme.fast } }
                    text: modelData.match
                    iconName: (Library.appCategories.find(c => c.id === builtIn.modelData.category) || { icon: "" }).icon
                    onClicked: { matchForm.startAdd(modelData.match); section.revealRequested(matchForm) }
                    Tip {
                        shown: builtIn.hovered
                        text: Library.builtInMatchSummary(builtIn.modelData)
                        detail: Library.pastingText.builtInMatchTip
                    }
                }
            }
        }
    }
}
