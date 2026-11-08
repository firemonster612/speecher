pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Wording

// One service's model, chosen the same way wherever it is: in a Service
// Order, a Writing Profile and one run. A Local Model is picked from those
// downloaded; any other service's is typed, with the models it lists as
// suggestions (a server's or runner's once checked, ChatGPT's and Claude's),
// so a server that lists none still takes a name. inheritedText is what
// leaving it empty runs, "Same as <place> (<model>)", where a narrower
// place may leave it. chosen asks for a model, "" for the inherited one.
Loader {
    id: field

    property string role
    property string service
    property string value
    property string inheritedText
    signal chosen(string model)

    readonly property var choices: ModelText.modelChoices(role, service)

    sourceComponent: role === "speech" && service === "local" ? downloaded : typed

    Component {
        id: downloaded
        InheritSelect {
            placeholder: Words.choose
            emptyText: Schema.row("speechLocalModel").emptyHelp
            inheritedText: field.inheritedText
            choices: field.choices
            currentId: field.value
            onChosen: id => field.chosen(id)
        }
    }
    Component {
        id: typed
        SuggestField {
            suggestionsText: Words.suggestions
            text: field.value
            suggestions: field.choices
            placeholderText: field.inheritedText
            // Empty runs the inherited model, so at rest that reads as the
            // value, as an InheritSelect's does; editing, a hint.
            placeholderTextColor: activeFocus ? Theme.textTertiary : Theme.text
            onCommitted: v => { if (v.trim() !== field.value) field.chosen(v.trim()) }
        }
    }
}
