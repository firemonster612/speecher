import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The refinement server's rows, among Your servers on Services, in its Fix
// sheet and on setup's Refinement step: its URL, format and key, and the
// connection check, whose models Refinement's order suggests. The CLI Proxy
// API server a sign-in names is offered as the URL. Each row saves as it
// changes.
Column {
    width: parent ? parent.width : implicitWidth

    SchemaRow { id: urlRow; rowId: "refinementEndpointUrl" }
    // Fills in the proxy's address once, which then sends the proxy's key.
    SettingRow {
        readonly property string proxy: Settings.values.cliproxyBaseUrl || ""
        shown: proxy.length > 0 && !(Settings.values.refinementEndpointUrl || "").startsWith(proxy)
        title: urlRow.spec.cliproxyLabel
        help: urlRow.spec.cliproxyHelp.arg(proxy + "/v1")
        Button {
            objectName: "useCliproxyServer"
            text: Words.use
            size: "small"
            onClicked: Settings.act("useCliproxyServer")
        }
    }
    SchemaRow { rowId: "refinementEndpointFormat" }
    SchemaRow { rowId: "refinementEndpointApiKey" }
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
