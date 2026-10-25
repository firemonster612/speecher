import QtQuick
import Speecher.Theme

// A QR code for text. Black on white in both themes: scanners read
// dark-on-light codes most reliably. The modules here are a stand-in pattern
// seeded from the text; the real code comes from core's qrCodeImage.
Rectangle {
    id: root
    property string text
    property int modules: 29

    implicitWidth: 196
    implicitHeight: 196
    radius: Theme.radiusLarge
    color: "white"
    border.width: 1
    border.color: Theme.border

    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.margins: 14
        onPaint: {
            const ctx = getContext("2d")
            const n = root.modules
            const cell = width / n
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = "black"
            let seed = 0
            for (let i = 0; i < root.text.length; ++i) seed = (seed * 31 + root.text.charCodeAt(i)) >>> 0
            const random = () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296 }
            const finder = (x, y) => x < 8 && y < 8 || x >= n - 8 && y < 8 || x < 8 && y >= n - 8
            for (let y = 0; y < n; ++y)
                for (let x = 0; x < n; ++x)
                    if (!finder(x, y) && random() < 0.48) ctx.fillRect(x * cell, y * cell, cell + 0.3, cell + 0.3)
            for (const [fx, fy] of [[0, 0], [n - 7, 0], [0, n - 7]]) {
                ctx.fillRect(fx * cell, fy * cell, 7 * cell, 7 * cell)
                ctx.clearRect((fx + 1) * cell, (fy + 1) * cell, 5 * cell, 5 * cell)
                ctx.fillRect((fx + 2) * cell, (fy + 2) * cell, 3 * cell, 3 * cell)
            }
        }
    }
    onTextChanged: canvas.requestPaint()
}
