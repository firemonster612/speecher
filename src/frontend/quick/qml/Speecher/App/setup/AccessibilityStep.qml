import QtQuick
import Speecher.Controls
import Speecher.App

// Whether desktop accessibility is on, with the fix if it is not. The step's
// description says what it lets Speecher do.
StepPage {
    Card { HealthRow { rowId: "desktopAccessibility" } }
}
