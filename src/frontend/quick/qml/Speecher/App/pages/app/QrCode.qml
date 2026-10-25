import QtQuick
import QtQuick.Window
import Speecher.Theme

// A QR code for text, drawn by core's qrCodeImage through the host's
// image://qr provider. Black on white in both themes: scanners read
// dark-on-light codes most reliably. Empty text shows the white card alone.
Rectangle {
    id: root
    property string text

    implicitWidth: 196
    implicitHeight: 196
    radius: Theme.radiusLarge
    color: "white"
    border.width: 1
    border.color: Theme.border

    Image {
        anchors.fill: parent
        anchors.margins: 6
        // The code carries its own quiet zone. Asked for at the screen's
        // pixels and never smoothed, so every module stays sharp.
        source: root.text ? "image://qr/" + Qt.btoa(root.text) : ""
        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
        fillMode: Image.PreserveAspectFit
        smooth: false
        cache: false
    }
}
