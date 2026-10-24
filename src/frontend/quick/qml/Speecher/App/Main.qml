import QtQuick
import QtQuick.Window
import QtQuick.Templates as T
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// The main window: sidebar on the window background, the current page on an
// inset panel, the search palette above both, and toasts above that. The host
// opens it, hides it on close and opens the setup assistant itself.
// Once removing Speecher has deleted the settings, only quitting is left: the
// window shows only the removal's report, and closing it quits, as the
// report's button does, whether removal finishes before the close or after.
ClientWindow {
    id: window
    width: 1240
    height: 820
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: Words.shell.appName
    readonly property bool settingsDeleted: System.removal.settingsDeleted === true
    readonly property bool leftToQuit: settingsDeleted && !visible
    // Search opened while removal ran goes with the rest of the window.
    onSettingsDeletedChanged: if (settingsDeleted) searchPalette.close()
    onLeftToQuitChanged: if (leftToQuit) System.quit()

    // Opaque, so the search palette's blurred copy of it hides what is
    // under; translucent, the window's own paper shows the desktop instead.
    Rectangle {
        id: content
        anchors.fill: parent
        radius: window.cornerRadius
        color: Theme.translucent ? "transparent" : Theme.paper

        Sidebar {
            id: sidebar
            objectName: "sidebar"
            visible: !window.settingsDeleted
            width: Theme.sidebarWidth
            height: parent.height
            onSearchRequested: searchPalette.open()
        }

        Sheet {
            visible: !window.settingsDeleted
            side: sidebar

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

    RemovalReport {
        objectName: "removalReport"
        visible: window.settingsDeleted
        anchors.centerIn: content
        width: Math.min(640, content.width - 96)
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

    Shortcut { sequence: "Ctrl+K"; enabled: !window.settingsDeleted; onActivated: searchPalette.shown ? searchPalette.close() : searchPalette.open() }
    Shortcut { sequence: "Ctrl+F"; enabled: !window.settingsDeleted; onActivated: searchPalette.open() }
}
