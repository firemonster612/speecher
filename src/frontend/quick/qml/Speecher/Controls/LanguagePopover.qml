pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// Languages in a panel that grows from what opened it, each with its flag,
// under a search field that filters as you type, by English name, the
// language's own name or code. Up and Down move through them, Return takes
// the highlighted one and Escape closes. With choosing, it is the Spoken
// language's menu: currentId has the check and a pick comes through
// chosen. Without, it only shows them, such as the languages a service
// listens for.
//   languages: [{ id, label, icon? }]. label is the language's names,
//              "German (Deutsch)"; icon stands in for the flag, as for
//              Automatic, which is no language.
MenuPopup {
    id: popover

    property var languages: []
    property string currentId
    property bool choosing: false
    property string searchText
    property string clearText
    property string emptyText
    signal chosen(string id)

    // Opens under item, lined up with its edge as align says.
    function openFrom(item) {
        parent = item
        open()
    }

    // Lower case without accents, so "francais" finds "français".
    function _fold(s) { return s.normalize("NFD").replace(/[̀-ͯ]/g, "").toLowerCase() }
    readonly property string _query: _fold(search.text.trim())
    // Names a word of which starts with the query come first, so "fr"
    // puts French before Afrikaans.
    function _startsWord(l) { return (" " + _fold(l.label).replace(/[()]/g, " ")).includes(" " + _query) }
    readonly property var _shown: !_query ? languages
        : languages.filter(l => _startsWord(l) || l.id.startsWith(_query))
                   .concat(languages.filter(l => !_startsWord(l) && !l.id.startsWith(_query) && _fold(l.label).includes(_query)))
    property int _highlighted: 0
    // Showing only languages, the highlight shows once the arrows move it,
    // so the first row doesn't read as chosen.
    property bool _keyed: false
    function _step(by) {
        _keyed = true
        _highlight(_highlighted + by)
    }
    function _highlight(index) {
        _highlighted = Math.max(0, Math.min(_shown.length - 1, index))
        list.positionViewAtIndex(_highlighted, ListView.Contain)
    }
    function _take(index) {
        if (index < 0 || index >= _shown.length) return
        if (choosing) chosen(_shown[index].id)
        close()
    }

    readonly property int _rowHeight: 34
    width: Math.max(parent ? parent.width : 0, 280)
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    // Kept inside the window, over what opened it where there is no room
    // below.
    margins: Theme.space2
    // A press on what opened it is that item's to answer, so a button
    // toggles it rather than closing and opening it again.
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent
    focus: true
    onAboutToShow: {
        search.clear()
        _keyed = false
        _highlight(choosing ? _shown.findIndex(l => l.id === currentId) : 0)
    }
    onOpened: search.forceActiveFocus()

    contentItem: Column {
        spacing: Theme.menuInset
        SearchField {
            id: search
            width: parent.width
            placeholderText: popover.searchText
            clearText: popover.clearText
            onTextChanged: popover._highlight(0)
            Keys.onUpPressed: popover._step(-1)
            Keys.onDownPressed: popover._step(1)
            Keys.onReturnPressed: popover._take(popover._highlighted)
            Keys.onEnterPressed: popover._take(popover._highlighted)
            Keys.onEscapePressed: popover.close()
        }
        // As tall as the whole list, up to ten rows, however few match, so
        // the panel holds still while the search narrows it.
        Item {
            width: parent.width
            height: Math.min(popover.languages.length, 10) * (popover._rowHeight + list.spacing)
            ListView {
                id: list
                anchors.fill: parent
                clip: true
                model: popover._shown
                spacing: 1
                boundsBehavior: Flickable.StopAtBounds
                // A mouse drags nothing on a desktop; touch still does.
                acceptedButtons: Qt.NoButton
                Accessible.role: Accessible.List
                WheelScroll {}
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    required property int index
                    readonly property bool current: popover.choosing && modelData.id === popover.currentId
                    width: ListView.view.width
                    height: popover._rowHeight
                    radius: Theme.radiusLarge - Theme.menuInset
                    color: tap.pressed ? Theme.fillStrong
                         : index === popover._highlighted && (popover.choosing || popover._keyed) || hover.hovered ? Theme.fill
                         : "transparent"
                    Behavior on color { EaseColor {} }
                    Accessible.role: popover.choosing ? Accessible.MenuItem : Accessible.ListItem
                    Accessible.name: modelData.label
                    Accessible.checked: current
                    Accessible.onPressAction: popover._take(index)

                    Flag {
                        id: flag
                        visible: !row.modelData.icon
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        code: row.modelData.id
                    }
                    Icon {
                        visible: !!row.modelData.icon
                        anchors.centerIn: flag
                        name: row.modelData.icon || ""
                        size: 16
                    }
                    Txt {
                        x: flag.x + flag.width + 10
                        width: parent.width - x - 34
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.label
                        variant: "label"
                        elide: Text.ElideRight
                    }
                    Icon {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        name: "check"
                        size: 15
                        color: Theme.accent
                        opacity: row.current ? 1 : 0
                        scale: row.current ? 1 : Theme.motionScale(0.4)
                        Behavior on opacity { Ease { duration: Theme.fast } }
                        Behavior on scale { Ease { duration: Theme.slow; curve: Theme.springy } }
                    }
                    HoverHandler { id: hover; enabled: popover.choosing }
                    TapHandler { id: tap; enabled: popover.choosing; onTapped: popover._take(row.index) }
                }
            }
            Txt {
                x: 10
                y: Theme.space2
                width: parent.width - 20
                text: popover.emptyText
                variant: "small"
                tone: "secondary"
                wrapMode: Text.WordWrap
                opacity: popover._shown.length ? 0 : 1
                Behavior on opacity { Ease { duration: Theme.fast } }
            }
        }
    }
}
