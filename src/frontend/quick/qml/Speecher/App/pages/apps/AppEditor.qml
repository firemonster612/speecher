pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// One App's or one App Type's settings, as rows on a wash under its row:
// for an App what it is recognized by, its type, writing profile and
// Pasting, each starting "Same as …" until changed, the vocabulary limited
// to it, and Remove app; for an
// App Type its writing profile and Pasting. Every change saves at once
// through Apps; a refusal shows under the rows.
Item {
    id: editor

    // An entry of Apps.entries, or of Apps.types for an App Type.
    property var entry: ({})
    property bool isType: false
    property string refusal

    readonly property var t: Schema.apps.t
    readonly property bool isOther: isType && entry.id === "general"
    readonly property var profiles: Library.profiles.map(p => ({ id: p.id, label: p.name, icon: Library.profileIcon(p.id) }))
    readonly property string otherProfile: (Apps.types.find(o => o.id === "general") || { profile: "" }).profile
    // The type that decides what an App follows.
    readonly property var appType: Schema.apps.type(isType ? entry.id : (entry.effective || {}).type)

    // "Same as …" for each of an App's own values, by where the value would
    // come from without it.
    function inheritedProfile() {
        const inherited = entry.inherited || {}
        const name = Library.profileName(inherited.profile || "")
        return inherited.profileFrom === "type" ? t.sameAs(appType.label, name)
             : inherited.profileFrom === "fallback" ? t.sameAs(Schema.apps.type("general").label, name)
             : t.automatic(name)
    }
    function inheritedPasting() {
        const inherited = entry.inherited || {}
        return inherited.pastingFrom === "terminal" ? t.sameAs(Schema.apps.type("terminal").label, Schema.apps.pastingLabel(inherited.pasting))
             : inherited.pastingFrom === "type" ? t.sameAs(appType.label, Schema.apps.pastingLabel(inherited.pasting))
             : t.sameAs(appType.label, t.theSteps)
    }
    // The Vocabulary Entries limited to the App, corrections learned in it
    // included.
    readonly property int vocabularyCount: isType ? 0 : Library.vocabulary.filter(e => Library.limitedToApp(e, entry)).length
    function save(patch) {
        refusal = Schema.apps.refusals[isType ? Apps.updateType(entry.id, patch) : Apps.update(entry.id, patch)] || ""
    }
    // The match an App goes by, worded: an id as it is, text with what it
    // is looked for in.
    function matchText(match) {
        return match.kind === "id" ? match.value
             : (match.kind === "name" ? t.byName : t.byTitle) + " “" + match.value + "”"
    }
    // What the Pasting choice does here, for the line under it.
    readonly property string result: {
        const method = (isType ? entry.pasting : (entry.effective || {}).pasting) || "standard_paste"
        const terminalWins = isType && entry.id === "ai_coding" && !!(Apps.types.find(o => o.id === "terminal") || {}).pasting
        return Schema.pasting.ruleResult(System.rulePlan(method), method, terminalWins)
    }

    width: parent ? parent.width : implicitWidth
    implicitHeight: rows.implicitHeight + Theme.space2 * 2

    Rectangle {
        anchors.fill: parent
        anchors.margins: Theme.washInset
        radius: Theme.washRadius
        color: Theme.fill
    }

    Column {
        id: rows
        y: Theme.space2
        width: parent.width

        SettingRow {
            shown: !editor.isType
            title: editor.t.recognizedAs
            help: (editor.entry.matches || []).map(m => editor.matchText(m)).join(", ")
        }
        SettingRow {
            shown: !editor.isType
            title: editor.t.type
            help: editor.entry.titleOnly ? editor.t.titleOnlyType : ""
            Select {
                visible: !editor.entry.titleOnly
                options: [{ id: "", label: editor.t.automatic(Schema.apps.type((editor.entry.inherited || {}).type).one) }]
                    .concat(Schema.apps.types.map(o => ({ id: o.id, label: o.label, icon: o.icon })))
                currentId: editor.entry.type || ""
                onChosen: id => editor.save({ type: id })
            }
        }
        SettingRow {
            title: editor.t.profile
            help: editor.isOther ? editor.t.otherProfileHelp : ""
            Select {
                objectName: "appProfile"
                options: (editor.isOther ? [] : [{ id: "", label: editor.isType ? editor.t.sameAs(Schema.apps.type("general").label, Library.profileName(editor.otherProfile))
                                                                                  : editor.inheritedProfile() }])
                    .concat(editor.profiles)
                currentId: editor.entry.profile || ""
                onChosen: id => editor.save({ profile: id })
            }
        }
        SettingRow {
            title: editor.t.pasting
            help: editor.result + (editor.isType && editor.entry.id === "terminal" ? ". " + editor.t.terminalsNote : "")
            Select {
                objectName: "appPasting"
                options: (editor.isType ? [{ id: "", label: Schema.apps.pastingChoices[0].label, icon: Schema.apps.pastingChoices[0].icon }]
                                        : [{ id: "", label: editor.inheritedPasting() }].concat([Schema.apps.pastingChoices[0]]))
                    .concat(Schema.apps.pastingChoices.slice(1))
                currentId: editor.entry.pasting || ""
                onChosen: id => editor.save({ pasting: id })
            }
        }
        SettingRow {
            shown: !editor.isType
            title: Schema.page("vocabulary").title
            help: Schema.vocabulary.limitedHere(editor.vocabularyCount, "app")
            Button {
                objectName: "appVocabulary"
                text: Schema.vocabulary.showThem
                size: "small"
                onClicked: Navigation.follow("vocabulary", "vocabularyFor:app/" + editor.entry.id)
            }
        }
        Reveal {
            open: editor.refusal.length > 0
            Txt { x: Theme.rowInset; width: parent.width - Theme.rowInset * 2; text: editor.refusal; variant: "small"; tone: "negative"; wrapMode: Text.WordWrap }
        }
        SettingRow {
            shown: !editor.isType
            title: editor.t.remove
            help: editor.t.removeHelp
            Button {
                text: editor.t.remove
                iconName: "trash-2"
                variant: "danger"
                size: "small"
                onClicked: {
                    const name = editor.entry.name
                    const undo = Apps.remove(editor.entry.id)
                    if (undo >= 0) Toaster.show(Words.deleted(name), { icon: "trash-2", actionText: Words.undo, action: () => Apps.undo(undo) })
                }
            }
        }
    }
}
