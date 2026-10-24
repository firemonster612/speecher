pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme

// The one way to pick a provider or service, in settings, setup and
// Transcribe alike: a grid of cards, each with the provider's icon, its name,
// a line of help, its status and its ratings, so they can be compared before
// choosing. The chosen card takes the pencil wash, outline and check. It
// sits inside a Section's card, inset from its edges, and takes as many
// columns as fit, the fewest rows with the fewest gaps; a narrow place gets
// fewer, smaller columns rather than another layout. Arrow keys move the
// choice through the grid.
//   options: [{ id, label, help?, icon?, status?, tone?, bars?, note? }]
//            tone colours the status (warning, negative, positive). bars
//            are RatingBars' bars; null shows unratedText, and an option
//            without bars (such as None) shows no rating at all. note is a
//            line under the ratings, such as what they were rated on.
FocusScope {
    id: root

    property var options: []
    property string currentId
    property string unratedText
    signal activated(string id)

    readonly property int currentIndex: options.findIndex(o => o.id === currentId)
    readonly property int minimumCardWidth: 184
    readonly property int columns: {
        const n = options.length
        const fit = Math.max(1, Math.floor((grid.width + grid.spacing) / (minimumCardWidth + grid.spacing)))
        // Fewer rows first; of the column counts that give as few, the one
        // that leaves the fewest empty places.
        let best = 1
        for (let c = 2; c <= Math.min(fit, n); ++c) {
            if (Math.ceil(n / c) < Math.ceil(n / best)) best = c
        }
        const rows = Math.ceil(n / best)
        while (best > 1 && Math.ceil(n / (best - 1)) === rows) --best
        return best
    }

    // Every card is one height, so statuses and ratings line up across the
    // grid: a status or note takes the room of the tallest one.
    readonly property real statusHeight: _tallest("statusHeight")
    readonly property real noteHeight: _tallest("noteHeight")
    // Counts the cards made, so _tallest looks again as they arrive.
    property int _made: 0
    function _tallest(measure) {
        let tallest = 0
        for (let i = 0; i < _made && i < cards.count; ++i) {
            const card = cards.itemAt(i)
            if (card) tallest = Math.max(tallest, card[measure])
        }
        return tallest
    }

    implicitWidth: 520
    implicitHeight: grid.implicitHeight + Theme.space3 * 2
    width: parent ? parent.width : implicitWidth
    opacity: enabled ? 1 : Theme.disabledOpacity
    Behavior on opacity { Ease {} }
    activeFocusOnTab: true
    Accessible.role: Accessible.List
    Keys.onLeftPressed: { focusVisible.keyed(); _step(-1) }
    Keys.onRightPressed: { focusVisible.keyed(); _step(1) }
    Keys.onUpPressed: { focusVisible.keyed(); _step(-columns) }
    Keys.onDownPressed: { focusVisible.keyed(); _step(columns) }
    function _step(by) {
        const i = currentIndex < 0 ? 0 : Math.max(0, Math.min(options.length - 1, currentIndex + by))
        activated(options[i].id)
    }
    FocusVisible { id: focusVisible }

    Grid {
        id: grid
        x: Theme.space3
        y: Theme.space3
        width: parent.width - Theme.space3 * 2
        columns: root.columns
        spacing: Theme.space2

        Repeater {
            id: cards
            model: root.options
            onItemAdded: ++root._made
            onItemRemoved: --root._made
            delegate: ChoiceCard {
                id: card
                required property var modelData
                required property int index
                readonly property bool rated: modelData.bars !== undefined
                readonly property real statusHeight: status.height
                readonly property real noteHeight: note.text ? note.implicitHeight : 0

                // Narrow where three share a step, as in setup.
                readonly property bool narrow: width < 220

                width: (grid.width - grid.spacing * (root.columns - 1)) / root.columns
                height: padding * 2 + ratings.y + ratings.height + (root.noteHeight > 0 ? Theme.space1 + root.noteHeight : 0)
                // Tighter where the cards are narrow.
                padding: narrow ? 10 : width < 260 ? 12 : 14
                selected: index === root.currentIndex
                focused: focusVisible.shown && selected
                Accessible.name: modelData.label
                // Everything the card shows, as sentences.
                Accessible.description: {
                    const rating = !rated ? "" : modelData.bars === null ? root.unratedText : bars.spoken
                    const said = [modelData.help, modelData.status, rating, modelData.note].filter(t => !!t)
                    return said.length ? said.map(t => t.replace(/\.$/, "")).join(". ") + "." : ""
                }
                onActivated: { focusVisible.pointed(); root.forceActiveFocus(); root.activated(modelData.id) }

                Icon {
                    id: glyph
                    name: card.modelData.icon || "circle"
                    size: 18
                    color: card.selected ? Theme.accent : Theme.textSecondary
                }
                Txt {
                    id: label
                    x: glyph.width + Theme.space2
                    width: parent.width - x - 24
                    anchors.verticalCenter: glyph.verticalCenter
                    text: card.modelData.label
                    variant: "label"
                    elide: Text.ElideRight
                }
                // Two lines kept for help, so a card with one lines up with
                // its neighbours.
                Txt {
                    id: help
                    readonly property real room: Math.ceil(helpMetrics.lineSpacing * lineHeight * maximumLineCount)
                    y: glyph.height + Theme.space2
                    width: parent.width
                    text: card.modelData.help || ""
                    variant: "small"
                    tone: "secondary"
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    FontMetrics { id: helpMetrics; font: help.font }
                }
                // Wrapped, and bounded at two lines, so a long problem stays
                // on its card; assistive tech hears all of it.
                StatusText {
                    id: status
                    y: help.y + help.room + Theme.space1
                    width: parent.width
                    wrap: true
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    text: card.modelData.status || ""
                    // A state core has not settled yet has no words, and
                    // no dot either.
                    tone: text ? card.modelData.tone || "neutral" : "neutral"
                }
                Item {
                    id: ratings
                    y: status.y + root.statusHeight + (card.narrow ? Theme.space1 : Theme.space2)
                    width: parent.width
                    height: bars.implicitHeight
                    RatingBars {
                        id: bars
                        // Pips and names a little narrower where the card is.
                        pipWidth: card.narrow ? 6 : card.width < 260 ? 8 : 9
                        labelWidth: card.narrow ? 50 : 58
                        // An unrated card keeps the bars' room, empty, so
                        // every card is one height.
                        bars: card.modelData.bars || [{ label: "", value: null }, { label: "", value: null }]
                        color: card.selected ? Theme.accent : Theme.textSecondary
                        opacity: card.rated && card.modelData.bars !== null ? 1 : 0
                        Behavior on opacity { Ease {} }
                    }
                    Txt {
                        text: root.unratedText
                        variant: "small"
                        tone: "tertiary"
                        opacity: card.rated && card.modelData.bars === null ? 1 : 0
                        Behavior on opacity { Ease {} }
                    }
                }
                Txt {
                    id: note
                    y: ratings.y + ratings.height + Theme.space1
                    width: parent.width
                    text: card.modelData.note || ""
                    variant: "caption"
                    tone: "tertiary"
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }
        }
    }
}
