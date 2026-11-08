import QtQuick
import Speecher.Controls
import Speecher.Wording

// One of Your servers on Services, and in a Fix sheet: the speech server or
// the refinement server, each a URL the person runs or rents for that job.
Section {
    id: section

    // "speech" or "refinement".
    required property string role

    title: Schema.row(role === "speech" ? "speechEndpointUrl" : "refinementEndpointUrl").section
    Loader {
        width: parent.width
        sourceComponent: section.role === "speech" ? speechRows : refinementRows
    }
    Component { id: speechRows; SpeechServerRows {} }
    Component { id: refinementRows; RefinementServerRows {} }
}
