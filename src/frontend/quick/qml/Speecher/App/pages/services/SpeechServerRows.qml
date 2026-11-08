import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.App

// The speech server's rows, among Your servers on Services, in its Fix
// sheet and on setup's Transcription step: the server URL, path and key,
// and the connection check, whose models Speech to text's order suggests.
// Each row saves as it changes.
Column {
    width: parent ? parent.width : implicitWidth

    SchemaRow { rowId: "speechEndpointUrl" }
    SchemaRow { rowId: "speechEndpointPath" }
    SchemaRow { rowId: "speechEndpointApiKey" }
    // The last verdict shows beside the button, where a new one rolls in.
    SchemaRow {
        id: endpointTest
        readonly property var check: ModelText.endpointCheck(Models.speechEndpoint)
        rowId: "speechEndpointTest"
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
