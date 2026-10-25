import QtQuick

// Turns done true just after the item holding it has loaded, so its
// animations can skip the values it loads with. A Timer rather than
// Qt.callLater: it goes with its holder, so it never writes into one that is
// gone, as leaving a page or setup step within a frame of opening it would.
Timer {
    property bool done: false
    interval: 0
    running: true
    onTriggered: done = true
}
