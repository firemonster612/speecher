import QtQuick
import Speecher.Theme

// The bubble every tooltip draws: ink-filled, with the text in the page
// colour and an optional quieter line of detail under it.
Rectangle {
    id: body

    property string text
    property string detail

    implicitWidth: column.implicitWidth + 18
    implicitHeight: column.implicitHeight + 12
    radius: Theme.radiusSmall
    color: Theme.text

    Column {
        id: column
        anchors.centerIn: parent
        spacing: 1
        Txt { text: body.text; variant: "small"; strong: body.detail.length > 0; color: Theme.sheet }
        Txt { visible: text.length > 0; text: body.detail; variant: "caption"; color: Theme.soft(Theme.sheet, 0.72) }
    }
}
