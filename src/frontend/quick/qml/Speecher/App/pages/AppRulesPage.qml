pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "text"

Page {
    id: page
    title: Schema.page("appRules").title
    subtitle: Schema.page("appRules").subtitle

    Section {
        title: Schema.row("categoryPasteRule_terminal").section
        caption: "Paste differently into every app of one type. Apps are sorted into types under Application recognition below."
        AccessibilityNotice { text: "Turn on desktop accessibility so Speecher can tell which app you're in." }
        // Two columns of rows, so seven types don't make a tall stack. The
        // rows go across in pairs, so each pair's dividers line up.
        Grid {
            id: typeGrid
            columns: 2
            width: parent.width
            Repeater {
                model: Schema.appCategories
                SchemaRow {
                    required property var modelData
                    required property int index
                    width: typeGrid.width / 2
                    first: index < typeGrid.columns
                    rowId: "categoryPasteRule_" + modelData.id
                    help: modelData.examples
                    enabled: System.accessibility
                }
            }
        }
    }

    AppPasteRules {}

    AppRecognitionRules { onRevealRequested: item => page.reveal(item) }
}
