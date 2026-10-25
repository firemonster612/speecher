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
    objectName: "languagePopover"

    property var languages: []
    property string currentId
    property bool choosing: false
    property string searchText
    property string clearText
    property string emptyText
    signal chosen(string id)

    // Opens under item, lined up with its edge as align says. It stays a
    // child of where it was declared and is only placed at item, so it
    // stays open when item goes, as a ServiceCards card does when Models
    // changes and the cards are made again.
    function openFrom(item) {
        width = Math.max(item.width, 280)
        const under = item.mapToItem(parent, 0, item.height + 6)
        x = align === "right" ? under.x + item.width - width : under.x
        y = under.y
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

    width: Math.max(parent ? parent.width : 0, 280)
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    // Kept inside the window, over what opened it where there is no room
    // below.
    margins: Theme.space2
    // Choosing, it is declared in its button, and a press on the button is
    // the button's to answer, so it toggles rather than closing and opening
    // again. Showing only languages, it is declared in a page or card, so
    // any press outside it closes it.
    closePolicy: choosing ? T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent
                          : T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    focus: true
    onAboutToShow: {
        search.clear()
        _keyed = false
        pointerMoves._at = null
        _highlight(choosing ? _shown.findIndex(l => l.id === currentId) : 0)
    }
    onOpened: search.forceActiveFocus()

    contentItem: Column {
        spacing: Theme.menuInset
        SearchField {
            id: search
            objectName: "languageSearch"
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
        // As tall as the whole list's first ten rows, however few match, so
        // the panel holds still while the search narrows it; a row with
        // help, such as Automatic, is taller.
        Item {
            width: parent.width
            height: popover.languages.slice(0, 10)
                    .reduce((sum, l) => sum + (l.help ? helpRow.implicitHeight : plainRow.implicitHeight) + list.spacing, 0)
            MenuItem { id: plainRow; visible: false; text: "x" }
            MenuItem { id: helpRow; visible: false; text: "x"; help: "x" }
            // Only the pointer's own moves light a row: a list scrolled by
            // the arrows under a resting pointer, or opened under it, must
            // not pull the highlight to the row beneath.
            HoverHandler {
                id: pointerMoves
                enabled: popover.choosing
                property var _at: null
                onPointChanged: {
                    const at = point.position
                    const moved = _at !== null && (at.x !== _at.x || at.y !== _at.y)
                    _at = at
                    if (!moved) return
                    const index = list.indexAt(at.x, at.y + list.contentY)
                    if (index >= 0) popover._highlighted = index
                }
            }
            ListView {
                id: list
                objectName: "languageRows"
                anchors.fill: parent
                clip: true
                model: popover._shown
                spacing: 1
                boundsBehavior: Flickable.StopAtBounds
                // A mouse drags nothing on a desktop; touch still does.
                acceptedButtons: Qt.NoButton
                Accessible.role: Accessible.List
                WheelScroll {}
                ScrollBar.vertical: ScrollBar { id: bar; persistent: true }
                // The pointer moves the highlight as the arrows do, so one row
                // is lit and Return takes it.
                delegate: MenuItem {
                    id: row
                    required property var modelData
                    required property int index
                    width: ListView.view.width - bar.gutter
                    text: modelData.label
                    help: modelData.help || ""
                    iconName: modelData.icon || ""
                    flagCode: modelData.icon ? "" : modelData.id
                    current: popover.choosing && modelData.id === popover.currentId
                    highlighted: index === popover._highlighted && (popover.choosing || popover._keyed)
                    // The highlight is the one wash under the pointer (the
                    // HoverHandler above), and showing only languages a row
                    // doesn't give when pressed.
                    hoverEnabled: false
                    down: popover.choosing && pressed
                    // The search field keeps the keys.
                    focusPolicy: Qt.NoFocus
                    onClicked: if (popover.choosing) popover._take(index)
                    Accessible.role: popover.choosing ? Accessible.MenuItem : Accessible.ListItem
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
