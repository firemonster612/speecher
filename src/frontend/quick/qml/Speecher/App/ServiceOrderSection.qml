import QtQuick
import Speecher.Controls
import Speecher.Wording

// A job's Service Order as its page shows it, under the order row's label
// and help: on Speech to text and Refinement, and on the setup steps that
// are those pages. brief folds each service's own choices (its model,
// thinking and speed) away behind a link, for a first run.
Section {
    id: section

    // "speech" or "refinement".
    required property string role
    property bool brief: false
    property bool _choicesAsked: false
    readonly property var spec: Schema.row(role === "speech" ? "speechServices" : "refinementServices")

    title: spec.label
    caption: brief ? spec.briefHelp || spec.help : spec.help
    SearchLanding { rowIds: [section.spec.id]; target: section }
    ServiceOrderList { role: section.role; choicesShown: !section.brief || section._choicesAsked }
    SettingRow {
        objectName: "showServiceChoices"
        shown: section.brief
        title: section.spec.showChoices || ""
        clickable: true
        trailingIcon: "chevron-down"
        trailingRotation: section._choicesAsked ? 180 : 0
        onClicked: section._choicesAsked = !section._choicesAsked
    }
}
