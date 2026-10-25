pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend

// The speech service as a grid of cards: what each is, how its text arrives
// and its ratings, or why it can't run right now. The chosen card takes the
// pencil outline and check; a fallback says where it stands in their place.
// Arrow keys move the choice through the grid.
FocusScope {
    id: root

    property string currentId
    property var fallbacks: []
    signal chosen(string id)

    readonly property var services: Models.speechServices
    readonly property int columns: 2
    readonly property int currentIndex: Math.max(0, services.findIndex(s => s.id === currentId))

    function step(by) {
        chosen(services[Math.max(0, Math.min(services.length - 1, currentIndex + by))].id)
    }

    implicitHeight: grid.implicitHeight
    activeFocusOnTab: true
    Keys.onLeftPressed: { focusVisible.keyed(); step(-1) }
    Keys.onRightPressed: { focusVisible.keyed(); step(1) }
    Keys.onUpPressed: { focusVisible.keyed(); step(-columns) }
    Keys.onDownPressed: { focusVisible.keyed(); step(columns) }
    FocusVisible { id: focusVisible }

    Grid {
        id: grid
        width: parent.width
        columns: root.columns
        spacing: Theme.space3

        Repeater {
            model: root.services
            delegate: ChoiceCard {
                id: card
                required property var modelData
                readonly property int fallbackIndex: root.fallbacks.indexOf(modelData.id)
                readonly property string problem: Models.problem(modelData.id)
                readonly property var bars: Models.serviceBars(modelData.id)

                // One height for every card, so the grid's rows line up and
                // the ratings sit on one line.
                width: (grid.width - grid.spacing * (root.columns - 1)) / root.columns
                height: 176
                padding: Theme.space4
                selected: modelData.id === root.currentId
                focused: focusVisible.shown && selected
                onActivated: {
                    focusVisible.pointed()
                    root.forceActiveFocus()
                    root.chosen(modelData.id)
                }

                Icon {
                    id: glyph
                    y: 2
                    name: card.modelData.icon
                    size: 16
                    color: card.selected ? Theme.accent : Theme.textSecondary
                    Behavior on color { EaseColor {} }
                }
                Txt {
                    id: name
                    x: glyph.width + Theme.space2
                    width: place.x - x - Theme.space2
                    anchors.verticalCenter: glyph.verticalCenter
                    text: card.modelData.label
                    variant: "heading"
                    elide: Text.ElideRight
                }
                // Its place among the fallbacks, where the chosen card has
                // its check.
                MorphText {
                    id: place
                    x: parent.width - width
                    anchors.verticalCenter: glyph.verticalCenter
                    text: Models.text.fallback.arg(Math.max(0, card.fallbackIndex) + 1)
                    variant: "small"
                    tone: "secondary"
                    opacity: !card.selected && card.fallbackIndex >= 0 ? 1 : 0
                    Behavior on opacity { Ease {} }
                }

                // What it is, or why it can't run, then how its text arrives.
                Item {
                    id: about
                    y: name.height + Theme.space3
                    width: parent.width
                    height: card.problem ? problemLine.implicitHeight : summary.implicitHeight
                    Behavior on height { Ease {} }
                    Txt {
                        id: summary
                        width: parent.width
                        text: card.modelData.summary
                        variant: "small"
                        tone: "secondary"
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        opacity: card.problem ? 0 : 1
                        Behavior on opacity { Ease {} }
                    }
                    StatusText {
                        id: problemLine
                        text: card.problem
                        tone: "negative"
                        opacity: card.problem ? 1 : 0
                        Behavior on opacity { Ease {} }
                    }
                }
                Txt {
                    y: about.y + about.height + Theme.space1
                    text: Models.serviceTextShows(card.modelData.id)
                    variant: "small"
                    tone: "tertiary"
                }

                // Its ratings, or that it has none.
                RatingBars {
                    anchors.bottom: parent.bottom
                    bars: card.bars || []
                    color: card.selected ? Theme.accent : Theme.textSecondary
                    opacity: card.bars ? 1 : 0
                    Behavior on opacity { Ease {} }
                }
                Txt {
                    anchors.bottom: parent.bottom
                    text: Models.text.unrated
                    variant: "small"
                    tone: "tertiary"
                    opacity: card.bars ? 0 : 1
                    Behavior on opacity { Ease {} }
                }
            }
        }
    }
}
