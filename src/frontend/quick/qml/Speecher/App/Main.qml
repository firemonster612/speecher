import QtQuick
import QtQuick.Window
import QtQuick.Templates as T
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend

// The main window: sidebar on the window background, the current page on an
// inset panel, the search palette above both, and toasts above that. It sets
// the theme from the Theme setting and opens the setup assistant when
// General asks for it.
ClientWindow {
    id: window
    width: 1240
    height: 820
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: Status.text.appName

    Binding {
        target: Theme
        property: "dark"
        value: Settings.values.themeControl === "dark" || (Settings.values.themeControl !== "light" && Theme.systemDark)
    }

    property Window _setup: null
    Component { id: setupWindow; SetupAssistant { onVisibleChanged: if (!visible) destroy() } }
    Connections {
        target: Settings
        function onActed(id) { if (id === "runSetup" && !window._setup) window._setup = setupWindow.createObject(window) }
    }

    // Opaque, so the search palette's blurred copy of it hides what is under.
    Rectangle {
        id: content
        anchors.fill: parent
        radius: window.cornerRadius
        color: Theme.paper

        Sidebar {
            id: sidebar
            width: Theme.sidebarWidth
            height: parent.height
            onSearchRequested: searchPalette.open()
        }

        Sheet {
            side: sidebar
            anchors.topMargin: window.titleBarHeight

            T.StackView {
                id: stack
                anchors.fill: parent
                anchors.margins: 1
                clip: true

                // Pages move the way the sidebar does: going down the list,
                // the new page rises from below; going up, it drops in.
                property int travel: 24
                replaceEnter: Transition {
                    ParallelAnimation {
                        Ease { property: "opacity"; from: 0; to: 1; duration: Theme.slow }
                        Ease { property: "y"; from: stack.travel; to: 0; duration: Theme.page }
                    }
                }
                replaceExit: Transition {
                    ParallelAnimation {
                        Ease { property: "opacity"; to: 0; duration: Theme.fast; curve: Theme.easeIn }
                        Ease { property: "y"; to: -stack.travel / 2; curve: Theme.easeIn }
                    }
                }

                property int _index: 0
                function show(pageId) {
                    const index = Schema.pages.findIndex(p => p.id === pageId)
                    travel = (index >= _index ? 24 : -24) * Theme.movement
                    _index = index
                    const url = Qt.resolvedUrl("pages/" + Schema.page(pageId).file)
                    if (depth === 0) push(url)
                    else replace(null, url)
                }
                Component.onCompleted: show(Navigation.current)
                Connections {
                    target: Navigation
                    function onCurrentChanged() { stack.show(Navigation.current) }
                }
            }
        }
    }

    SearchPalette {
        id: searchPalette
        objectName: "searchPalette"
        anchors.fill: parent
        backdrop: content
    }

    // Notes posted to Toaster rise in at the bottom, over everything.
    Toast {}
    Connections {
        target: System
        function onAnnounced(text, icon) { Toaster.show(text, { icon: icon }) }
    }

    Shortcut { sequence: "Ctrl+K"; onActivated: searchPalette.shown ? searchPalette.close() : searchPalette.open() }
    Shortcut { sequence: "Ctrl+F"; onActivated: searchPalette.open() }
}
