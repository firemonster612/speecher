pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "text"

Page {
    id: page
    title: Schema.page("pasting").title
    subtitle: Schema.page("pasting").subtitle

    Section {
        plain: true
        PasteRoute {
            steps: Library.pasteRoute
            caption: Library.pasteCaption
            appName: Library.pastingText.routeApp
            sample: Library.pastingText.routeSample
            startLabel: Library.pastingText.routeStart
            firstLabel: Library.pastingText.routeFirst
            fallbackLabel: Library.pastingText.routeFallback
        }
    }

    Section {
        title: Schema.row("outputMethod").section
        SchemaRow { rowId: "outputMethod" }
        // The same check as General's, with the setup's own menu after it.
        HealthRow {
            rowId: "virtualKeyboard"
            checkId: "virtualKeyboardHealth"
            IconButton {
                iconName: "ellipsis"
                size: 28
                tooltip: Words.more
                enabled: System.virtualKeyboard !== "notSetUp"
                opacity: enabled ? 1 : 0
                anchors.verticalCenter: parent.verticalCenter
                Behavior on opacity { Ease {} }
                onClicked: keyboardMenu.open()
                Menu {
                    id: keyboardMenu
                    width: 240
                    items: System.virtualKeyboardActions
                    onPicked: id => id === "remove" ? System.removeVirtualKeyboard() : System.disableVirtualKeyboard()
                }
            }
        }
        SchemaRow { rowId: "globalPasteRule" }
        SchemaRow { rowId: "outputFormat" }
        SchemaRow { rowId: "restoreClipboardAfterTyping" }
    }

    Section {
        title: Schema.row("categoryPasteRule_terminal").section
        caption: Library.pastingText.categoriesCaption
        AccessibilityNotice { text: Library.pastingText.categoriesNeedAccessibility }
        // Two columns of rows, so seven types don't make a tall stack.
        Row {
            id: typeColumns
            readonly property int half: Math.ceil(Library.appCategories.length / 2)
            width: parent.width
            Repeater {
                model: [Library.appCategories.slice(0, typeColumns.half), Library.appCategories.slice(typeColumns.half)]
                Column {
                    id: typeColumn
                    required property var modelData
                    width: typeColumns.width / 2
                    Repeater {
                        model: typeColumn.modelData
                        SchemaRow {
                            required property var modelData
                            rowId: "categoryPasteRule_" + modelData.id
                            help: modelData.examples
                            enabled: System.accessibility
                        }
                    }
                }
            }
        }
    }

    AppPasteRules {}

    AppRecognitionRules { onRevealRequested: item => page.reveal(item) }
}
