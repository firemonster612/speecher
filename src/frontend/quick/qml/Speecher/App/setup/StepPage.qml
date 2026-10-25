import QtQuick
import Speecher.Controls

// One step of the setup assistant: a Page with the step's title and what it
// is for, then the step's own content. A step that brings its own heading,
// like Welcome, sets title.
Page {
    // Set by SetupAssistant when it shows the step.
    property var info: ({})
    property SetupChoices choices

    title: info.title || ""
    subtitle: info.description || ""
}
