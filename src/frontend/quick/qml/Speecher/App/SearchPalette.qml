pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Templates as T
import QtQuick.Effects
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// Search everything: a root over a blurred window that finds pages and
// individual settings by name, keyword, option or help text. Switches can be
// flipped right in the results; Enter jumps to the setting and flashes it.
// While open it keeps keyboard focus to itself, Tab included, Escape closes
// it from anywhere inside, and closing hands focus back where it was.
FocusScope {
    id: root

    property Item backdrop
    // The window's corners, which the paper under the blurred copy follows.
    property real cornerRadius
    property bool shown: false
    property alias query: input.text
    readonly property var results: query.trim().length
        ? Schema.search(query, 40, need => System[need], _apps.concat(_vocabulary, _library)).concat(_historyResult)
        : _suggestions
    // While History holds words or keeps text, the last result looks for the
    // query in what was said, landing on History with it typed in its search
    // ("historyFind:<query>"). Keeping no new text leaves what is kept,
    // recordings among it, findable.
    readonly property bool _historySearchable: History.kept > 0 || Schema.keepsText(Settings.values.historyKeeps)
    readonly property var _historyResult: _historySearchable && query.trim().length
        ? [{ type: "history", id: "historyFind:" + query.trim(), page: "history", label: Words.shell.searchHistory(query.trim()),
             icon: Schema.page("history").icon, trail: Schema.trail("history") }]
        : []
    // The person's Apps, found by name or what they are recognized by, and
    // landing on their row on Apps (AppRow's appentry:<id>).
    readonly property var _apps: Apps.entries.map(e => ({
        type: "app", id: "appentry:" + e.id, page: "apps", section: Schema.apps.t.yourApps,
        label: e.name, keywords: e.ids.join(" ") + " app"
    }))
    // The Vocabulary's entries, found by what they spell, replace or write,
    // and landing on their row, which opens them (VocabularyRow's vocab:<id>).
    readonly property var _vocabulary: Library.vocabulary.map(e => ({
        type: "vocabulary", id: "vocab:" + e.id, page: "vocabulary", section: Schema.vocabulary.t.yourVocabulary,
        label: e.term, keywords: [e.replacement, e.context].join(" ")
    }))
    // The Writing Profiles and your own tones and cleanup levels, found by
    // name and landing on them on Writing profiles (profile:<id>, tone:<id>,
    // level:<id>).
    readonly property var _library: Library.profiles.map(p => ({
        type: "profile", id: "profile:" + p.id, page: "profiles", section: Schema.row("writingProfileBehavior").section,
        label: p.name, keywords: "writing profile " + Library.profileSummary(p)
    })).concat(Library.customTones.map(t => ({
        type: "tone", id: "tone:" + t.id, page: "profiles", section: Schema.row("customTones").label,
        label: t.name, keywords: "tone " + t.instruction
    })), Library.customCleanupLevels.map(l => ({
        type: "level", id: "level:" + l.id, page: "profiles", section: Schema.row("customCleanupLevels").label,
        label: l.name, keywords: "cleanup level " + l.instructions
    })))
    // How many results are pages and settings: Search History is an offer
    // to look elsewhere, not a match, so it is never counted.
    readonly property int _matches: results.filter(r => r.type !== "history").length
    readonly property bool _searching: query.trim().length > 0
    // The suggested rows this computer has, as search offers only those.
    readonly property var _suggestions: Schema.suggestions
        .map(id => Schema.row(id)).filter(r => r.kind !== "missing" && Schema.available(r, need => System[need]))
        .map(r => ({ type: "row", id: r.id, page: r.page, label: r.label, icon: Schema.page(r.page).icon,
                     trail: Schema.rowTrail(r) }))

    // What had focus before opening, to give it back on close.
    property Item _returnFocus: null

    function open() {
        if (!shown) _returnFocus = Window.activeFocusItem
        shown = true
        input.text = ""
        list.currentIndex = 0
        input.forceActiveFocus()
    }
    function close() {
        if (!shown) return
        shown = false
        if (_returnFocus) _returnFocus.forceActiveFocus()
        else focus = false
        _returnFocus = null
    }
    // Tab and Shift+Tab move through the palette's own controls, wrapping
    // around instead of reaching the window behind.
    function _tab(forward) {
        let next = Window.activeFocusItem
        do next = next.nextItemInFocusChain(forward)
        while (next && !_inside(next))
        if (next) next.forceActiveFocus(forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
    }
    function _inside(item) {
        for (let p = item; p; p = p.parent) if (p === card) return true
        return false
    }
    Keys.onEscapePressed: close()
    // Shortcuts, because the focused control moves focus on Tab itself
    // before the key could reach a handler here.
    Shortcut { sequences: ["Tab"]; enabled: root.shown; onActivated: root._tab(true) }
    Shortcut { sequences: ["Backtab", "Shift+Tab", "Shift+Backtab"]; enabled: root.shown; onActivated: root._tab(false) }

    function activate(index) {
        const r = results[index]
        if (!r) return
        // Closed first, so the focus it gives back is where a group that
        // closes on the way can take it from.
        close()
        if (r.type === "page") Navigation.go(r.id)
        else Navigation.goToRow(r.page, r.id)
    }

    visible: opacity > 0
    opacity: shown ? 1 : 0
    Behavior on opacity { Ease { duration: root.shown ? Theme.base : Theme.fast } }

    // Blurred, dimmed copy of the window behind the root, on paper: with the
    // window translucent the copy is too, and the sharp window would show
    // through it.
    Rectangle { anchors.fill: parent; radius: root.cornerRadius; color: Theme.paper }
    MultiEffect {
        anchors.fill: parent
        source: root.backdrop
        blurEnabled: true
        blur: root.shown ? 0.7 : 0
        blurMax: 48
        saturation: -0.2
        Behavior on blur { Ease { duration: Theme.slow } }
    }
    Rectangle { anchors.fill: parent; color: Theme.scrim }
    // A tap outside the card closes the palette; one on the card does not.
    TapHandler { onTapped: point => { if (!root._inside(root.childAt(point.position.x, point.position.y))) root.close() } }

    Item {
        id: card
        width: Math.min(620, parent.width - 80)
        height: field.height + listBox.height + footer.height
        x: (parent.width - width) / 2
        y: parent.height * 0.13 + (root.shown ? 0 : -10 * Theme.movement)
        scale: root.shown ? 1 : Theme.motionScale(0.96)
        Behavior on y { Ease { duration: Theme.slow } }
        Behavior on scale { Ease { duration: Theme.slow } }
        Behavior on height { Ease {} }

        Surface { anchors.fill: parent; radius: Theme.radiusPanel }

        Item {
            id: field
            width: parent.width
            height: 56
            Icon { x: 20; anchors.verticalCenter: parent.verticalCenter; name: "search"; size: 18; color: Theme.textSecondary }
            T.TextField {
                id: input
                objectName: "searchInput"
                height: 32
                verticalAlignment: TextInput.AlignVCenter
                x: 50
                width: parent.width - 110
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.text
                placeholderText: Words.shell.searchPlaceholder
                placeholderTextColor: Theme.textTertiary
                selectionColor: Theme.accentRing
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHeading
                background: null
                onTextChanged: list.currentIndex = 0
                Keys.onDownPressed: list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1)
                Keys.onUpPressed: list.currentIndex = Math.max(0, list.currentIndex - 1)
                Keys.onReturnPressed: root.activate(list.currentIndex)
                Keys.onEnterPressed: root.activate(list.currentIndex)
            }
            KeyCap { text: "Esc"; size: "small"; anchors.right: parent.right; anchors.rightMargin: 18; anchors.verticalCenter: parent.verticalCenter }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule }
        }

        Item {
            id: listBox
            anchors.top: field.bottom
            width: parent.width
            // Whole rows only, as many as fit under the card's top in the
            // window, so no result is cut; more scroll, with a bar to say so.
            readonly property int _fits: Math.max(3, Math.floor((root.height * 0.85 - field.height - footer.height
                                                                - list.anchors.topMargin - 8) / list.rowHeight))
            height: root.results.length ? list.anchors.topMargin + 8 + list.rowHeight * Math.min(root.results.length, _fits)
                                        : empty.height

            // Over the suggestions, and over Search History alone when no
            // setting matches.
            Txt {
                visible: !root._searching || (root._matches === 0 && root.results.length > 0)
                x: 22
                y: 12
                text: root._searching ? Words.shell.noSettings : Words.shell.suggested
                variant: "small"
                strong: true
                tone: "tertiary"
            }

            ListView {
                id: list
                readonly property int rowHeight: 50
                anchors.fill: parent
                anchors.margins: 8
                anchors.topMargin: !root._searching || root._matches === 0 ? 30 : 8
                clip: true
                model: root.results
                opacity: root.results.length ? 1 : 0
                Behavior on opacity { Ease { duration: Theme.fast } }
                boundsBehavior: Flickable.StopAtBounds
                // A mouse drags nothing on a desktop; touch still does.
                acceptedButtons: Qt.NoButton
                WheelScroll {}
                T.ScrollBar.vertical: ScrollBar { persistent: list.contentHeight > list.height }
                highlightMoveDuration: Theme.fast
                highlightMoveVelocity: -1
                highlight: Rectangle {
                    radius: Theme.radius
                    color: Theme.fill
                    border.width: 1
                    border.color: Theme.rule
                }
                delegate: Item {
                    id: hit
                    required property var modelData
                    required property int index
                    readonly property var spec: modelData.type === "row" ? Schema.row(modelData.id) : null
                    width: ListView.view.width
                    height: list.rowHeight

                    Icon {
                        x: 19
                        anchors.verticalCenter: parent.verticalCenter
                        name: hit.modelData.icon
                        size: 15
                        color: hit.ListView.isCurrentItem ? Theme.text : Theme.textTertiary
                    }
                    Column {
                        x: 54
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - x - 160
                        spacing: 2
                        Txt {
                            width: parent.width
                            elide: Text.ElideRight
                            textFormat: Text.StyledText
                            text: root.highlight(hit.modelData.label)
                            variant: "label"
                        }
                        Txt {
                            // A page says so, or where it is when it sits under another.
                            text: hit.modelData.trail || Words.shell.page
                            variant: "small"
                            tone: "tertiary"
                        }
                    }
                    // The setting's current value, or a switch to flip it here.
                    Item {
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        width: childrenRect.width
                        height: childrenRect.height
                        // Off and greyed where the row can't be used now, as
                        // on its page, and greyed while the row that turns it
                        // on is off.
                        Switch {
                            readonly property bool usable: !!hit.spec && Usable.usable(hit.spec)
                            visible: !!hit.spec && hit.spec.kind === "toggle"
                            checked: usable && !!Settings.values[hit.modelData.id]
                            enabled: usable && !!hit.spec && Schema.rowEnabled(hit.spec, Settings.values)
                            onToggled: Settings.set(hit.modelData.id, checked)
                        }
                        KeyCombo {
                            visible: !!hit.spec && hit.spec.kind === "shortcut"
                            keys: hit.spec && hit.spec.kind === "shortcut" ? (Settings.values[hit.modelData.id] || []) : []
                            size: "small"
                        }
                        // A delivery step's state, read-only: On or Off, and
                        // what it waits for while on.
                        Txt {
                            readonly property var step: hit.spec && hit.spec.step
                                ? System.deliverySteps.find(s => s.id === hit.spec.step) || null : null
                            visible: !!step
                            text: !step ? "" : !step.on ? Schema.pasting.switchState.off
                                : Schema.pasting.switchState.on + (step.waiting.length ? ", " + Schema.pasting.waitingFor(step.waiting[0]).toLowerCase() : "")
                            variant: "small"
                            tone: "secondary"
                        }
                        // The current value as plain text, as a row's trailing text.
                        Txt {
                            visible: !!hit.spec && (hit.spec.kind === "choice" || hit.spec.kind === "slider")
                            text: hit.spec ? root.valueLabel(hit.spec) : ""
                            variant: "small"
                            tone: "secondary"
                        }
                    }
                    HoverHandler { onHoveredChanged: if (hovered) list.currentIndex = hit.index }
                    TapHandler { onTapped: root.activate(hit.index) }
                }
            }

            EmptyState {
                id: empty
                opacity: root.results.length ? 0 : 1
                visible: opacity > 0
                Behavior on opacity { Ease { duration: Theme.fast } }
                iconName: "search-x"
                title: Words.noMatches(root.query)
                text: Words.shell.searchTip
            }
        }

        Item {
            id: footer
            anchors.top: listBox.bottom
            width: parent.width
            height: 40
            Rectangle { width: parent.width; height: 1; color: Theme.rule }
            Row {
                x: 18
                anchors.verticalCenter: parent.verticalCenter
                spacing: 16
                Repeater {
                    model: [{ k: ["↑", "↓"], t: Words.shell.moveHint }, { k: ["↵"], t: Words.shell.openHint }, { k: ["Esc"], t: Words.shell.closeHint }]
                    Row {
                        id: hint
                        required property var modelData
                        spacing: 6
                        KeyCombo { keys: hint.modelData.k; size: "small" }
                        Txt { text: hint.modelData.t; variant: "small"; tone: "tertiary"; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
            }
            MorphText {
                anchors.right: parent.right
                anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                text: Words.results(root._searching ? root._matches : root.results.length)
                variant: "small"
                tone: "tertiary"
            }
        }
    }

    // Underlines the parts of label that match the query.
    function highlight(label) {
        const esc = RichText.escape
        const words = query.trim().toLowerCase().split(/\s+/).filter(w => w.length)
        if (!words.length) return esc(label)
        let out = ""
        let i = 0
        const lower = label.toLowerCase()
        while (i < label.length) {
            const w = words.find(w => lower.startsWith(w, i))
            if (w) { out += "<u>" + esc(label.substr(i, w.length)) + "</u>"; i += w.length }
            else { out += esc(label[i]); i += 1 }
        }
        return out
    }
    function valueLabel(spec) { return spec ? Schema.valueLabel(spec, Settings.values[spec.id]) : "" }
}
