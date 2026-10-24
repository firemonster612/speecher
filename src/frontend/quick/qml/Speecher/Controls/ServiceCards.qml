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
//   options: [{ id, label, help?, icon?, status?, tone?, bars? }]
//            tone colours the status (warning, negative, positive). bars
//            are RatingBars' bars; null shows unratedText, and an option
//            without bars (such as None) shows no rating at all.
FocusScope {
    id: root

    property var options: []
    property string currentId
    property string unratedText
    signal activated(string id)

    readonly property int currentIndex: options.findIndex(o => o.id === currentId)
    readonly property int minimumCardWidth: 236
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
            model: root.options
            delegate: ChoiceCard {
                id: card
                required property var modelData
                required property int index
                readonly property bool rated: modelData.bars !== undefined

                width: (grid.width - grid.spacing * (root.columns - 1)) / root.columns
                // One height for every card, so the statuses and ratings
                // line up across the grid.
                height: padding * 2 + ratings.y + ratings.height
                // Tighter where the cards are narrow, as in setup.
                padding: width < 260 ? 12 : 14
                selected: index === root.currentIndex
                focused: focusVisible.shown && selected
                Accessible.name: modelData.label
                Accessible.description: [modelData.help, modelData.status].filter(t => !!t).join(". ")
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
                    y: glyph.height + Theme.space2
                    width: parent.width
                    text: card.modelData.help || ""
                    variant: "small"
                    tone: "secondary"
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                Txt { id: twoLines; visible: false; text: "\n"; variant: "small" }
                StatusText {
                    id: status
                    y: help.y + twoLines.implicitHeight + Theme.space1
                    height: 18
                    width: parent.width
                    text: card.modelData.status || ""
                    // A state core has not settled yet has no words, and
                    // no dot either.
                    tone: text ? card.modelData.tone || "neutral" : "neutral"
                }
                Item {
                    id: ratings
                    y: status.y + status.height + Theme.space2
                    width: parent.width
                    height: bars.implicitHeight
                    RatingBars {
                        id: bars
                        // A pip a little narrower where the card is.
                        pipWidth: card.width < 260 ? 8 : 9
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
            }
        }
    }
}
