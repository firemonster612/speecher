pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// Application recognition: which app type and Writing Profile an app gets.
// Your own matches as rows, with the built-in ones as chips to start from.
// --pasting-form=match opens the form, --built-ins=open the built-ins.
Section {
    id: section

    readonly property var categoryOptions: [{ id: "", label: Library.pastingText.automatic }].concat(Library.appCategories.map(c => ({ id: c.id, label: c.label, icon: c.icon })))
    readonly property var profileOptions: [{ id: "", label: Library.pastingText.automatic }].concat(Library.profiles.map(p => ({ id: p.id, label: p.name, icon: Library.profileIcon(p.id) })))
    // The page scrolls to item, for the form a built-in chip opens.
    signal revealRequested(Item item)

    title: Schema.row("appRecognitionRules").label
    caption: Schema.row("appRecognitionRules").help
    actions: [ Button { text: Library.pastingText.addMatch; iconName: "plus"; size: "small"; onClicked: matchForm.startAdd("") } ]
    SearchLanding { rowIds: ["appRecognitionRules"]; target: section }

    InlineForm {
        cancelText: Words.cancel
        id: matchForm
        // The match being edited, as saved, or empty while adding.
        property string editing
        property string category: ""
        property string profile: ""
        function start(match, type, profileId, key) {
            matchText.text = match; category = type; profile = profileId; editing = key
            error = ""; open = true
        }
        function startAdd(match) { start(match, "", "", "") }
        function startEdit(rule) { start(rule.match, rule.category, rule.profile, rule.match) }
        title: editing ? Library.pastingText.editMatch : Library.pastingText.newMatch
        saveText: editing ? Words.save : Library.pastingText.addMatch
        canSave: matchText.text.trim().length > 0 && (category.length > 0 || profile.length > 0)
        onCancelled: open = false
        onSaved: {
            const rule = { match: matchText.text.trim(), category: category, profile: profile }
            const refused = editing ? Library.update("recognitionRules", editing, rule) : Library.add("recognitionRules", rule)
            error = Library.refusalText("recognitionRules", refused)
            if (!refused) open = false
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

    // Column heads over the matches, where the first row puts its choices.
    Reveal {
        id: heads
        property real typeX
        property real profileX
        open: Library.recognitionRules.length > 0
        Item {
            width: parent.width
            height: 40
            Txt { x: Theme.rowInset; anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.space2; text: Library.pastingText.app; variant: "small"; tone: "secondary" }
            Txt { x: heads.typeX; anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.space2; text: Library.pastingText.appType; variant: "small"; tone: "secondary" }
            Txt { x: heads.profileX; anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.space2; text: Library.pastingText.profile; variant: "small"; tone: "secondary" }
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
            // Saves a choice from the row, or says why it can't: a match
            // with neither app type nor profile does nothing.
            function change(patch) {
                const refused = Library.update("recognitionRules", key, patch)
                if (refused) Toaster.show(Library.refusalText("recognitionRules", refused), { icon: "triangle-alert", tone: "negative" })
            }
            width: ListView.view.width
            first: index === 0 && matches.first
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                AppIcon { id: appIcon; name: matchRow.rule.match.split(".").pop(); size: 24; anchors.verticalCenter: parent.verticalCenter }
                Txt {
                    width: Math.min(implicitWidth, choices.x - Theme.space3 - appIcon.width - parent.spacing)
                    text: matchRow.rule.match
                    variant: "label"
                    elide: Text.ElideMiddle
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            // The app type and Writing Profile choices, 42% across where the
            // row has room. Where it hasn't, they move left and narrow so
            // they end before the row's Edit and Delete buttons, leaving the
            // app's name at least 35%.
            Row {
                id: choices
                readonly property real fullWidth: 184 + spacing + 170
                width: Math.min(fullWidth, parent.width * 0.65)
                x: Math.min((matchRow.width - Theme.rowInset * 2) * 0.42, parent.width - width)
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12
                Select {
                    id: typeChoice
                    width: (choices.width - choices.spacing) * 184 / (choices.fullWidth - choices.spacing)
                    options: section.categoryOptions
                    currentId: matchRow.rule.category
                    onChosen: id => matchRow.change({ category: id })
                }
                Select {
                    id: profileChoice
                    objectName: "matchProfile"
                    width: choices.width - choices.spacing - typeChoice.width
                    options: section.profileOptions
                    currentId: matchRow.rule.profile
                    onChosen: id => matchRow.change({ profile: id })
                }
            }
            Binding { when: matchRow.index === 0; target: heads; property: "typeX"; value: Theme.rowInset + choices.x }
            Binding { when: matchRow.index === 0; target: heads; property: "profileX"; value: Theme.rowInset + choices.x + profileChoice.x }
            actions: [
                IconButton { iconName: "pencil"; size: 28; tooltip: Words.edit; onClicked: { matchForm.startEdit(matchRow.rule); section.revealRequested(matchForm) } },
                IconButton {
                    iconName: "trash-2"; size: 28; tooltip: Library.pastingText.deleteMatch
                    onClicked: {
                        const name = matchRow.rule.match
                        const undo = Library.remove("recognitionRules", name)
                        if (undo >= 0) Toaster.show(Words.deleted(name), { icon: "trash-2", actionText: Words.undo, action: () => Library.undo(undo) })
                    }
                }
            ]
        }
    }
    Reveal {
        open: Library.recognitionRules.length === 0
        EmptyState {
            compact: true
            title: Library.pastingText.noMatches
            text: Library.pastingText.noMatchesHelp
        }
    }

    // Built-in matches as chips: hover one for what it sets, click it to
    // start a match of your own that overrides it.
    Disclosure {
        objectName: "builtInMatches"
        title: Library.pastingText.builtInMatches
        help: Library.pastingText.builtInMatchesHelp
        trailingText: Library.builtInMatchesCount

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
