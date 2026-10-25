import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The speech Custom Endpoint's rows, on the Speech page and setup's
// Transcription step: the server URL, path, key and model, and the
// connection check, whose models the Model row suggests. Each row saves as
// it changes.
Column {
    width: parent ? parent.width : implicitWidth

    SchemaRow { rowId: "speechEndpointUrl" }
    SchemaRow { rowId: "speechEndpointPath" }
    SchemaRow { rowId: "speechEndpointApiKey" }
    SchemaRow {
        id: endpointModel
        rowId: "speechEndpointModel"
        SuggestField {
            suggestionsText: Words.suggestions
            text: endpointModel.value || ""
            placeholderText: endpointModel.spec.placeholder
            suggestions: Models.speechEndpoint.models.map(m => ({ id: m, label: m }))
            onCommitted: v => Settings.set(endpointModel.rowId, v)
        }
    }
    // Core words the last verdict as this row's help; it shows beside the
    // button instead, where a new one rolls in.
    SchemaRow {
        id: endpointTest
        readonly property var check: ModelText.endpointCheck(Models.speechEndpoint)
        rowId: "speechEndpointTest"
        help: ""
        Row {
            spacing: Theme.space3
            StatusText {
                text: endpointTest.check.text
                busy: endpointTest.check.busy
                tone: endpointTest.check.tone
                indicator: endpointTest.check.dot ? "dot" : "none"
                anchors.verticalCenter: parent.verticalCenter
            }
            Button {
                text: endpointTest.spec.actionLabel
                iconName: endpointTest.spec.actionIcon
                size: "small"
                enabled: !endpointTest.check.busy
                onClicked: Models.testSpeechEndpoint()
            }
        }
    }
}
