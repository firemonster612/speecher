import QtQuick
import Speecher.Controls
import Speecher.Wording

// A job's Service Order as its page shows it, under the order row's label
// and help: on Speech to text and Refinement, and on the setup steps that
// are those pages.
Section {
    id: section

    // "speech" or "refinement".
    required property string role
    readonly property var spec: Schema.row(role === "speech" ? "speechServices" : "refinementServices")

    title: spec.label
    caption: spec.help
    SearchLanding { rowIds: [section.spec.id]; target: section }
    ServiceOrderList { role: section.role }
}
