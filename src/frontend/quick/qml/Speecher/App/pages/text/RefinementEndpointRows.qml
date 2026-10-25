import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The refinement Custom Endpoint's rows, on the Refinement page and setup's
// Refinement step: the server, its format, URL, key and model, and the
// connection check, whose models the Model row suggests. Each row saves as
// it changes.
Column {
    width: parent ? parent.width : implicitWidth

    SchemaRow { rowId: "refinementEndpointServer" }
    SchemaRow { rowId: "refinementEndpointFormat" }
    SchemaRow { rowId: "refinementEndpointUrl" }
    SchemaRow { rowId: "refinementEndpointApiKey" }
    SchemaRow {
        id: endpointModelRow
        rowId: "refinementEndpointModel"
        SuggestField {
            suggestionsText: Words.suggestions
            text: Settings.values.refinementEndpointModel || ""
            suggestions: Models.refinementEndpoint.models.map(m => ({ id: m, label: m }))
            placeholderText: endpointModelRow.spec.placeholder
            onCommitted: v => Settings.set("refinementEndpointModel", v)
        }
    }
    SchemaRow {
        id: endpointTestRow
        readonly property var check: ModelText.endpointCheck(Models.refinementEndpoint)
        rowId: "refinementEndpointTest"
        Row {
            spacing: Theme.space3
            StatusText {
                text: endpointTestRow.check.text
                busy: endpointTestRow.check.busy
                tone: endpointTestRow.check.tone
                indicator: endpointTestRow.check.dot ? "dot" : "none"
                anchors.verticalCenter: parent.verticalCenter
            }
            Button {
                text: endpointTestRow.spec.actionLabel
                iconName: "plug-zap"
                size: "small"
                enabled: !endpointTestRow.check.busy
                onClicked: Models.testRefinementEndpoint()
            }
        }
    }
}
