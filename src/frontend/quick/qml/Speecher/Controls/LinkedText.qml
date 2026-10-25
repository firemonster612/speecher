import QtQuick
import Speecher.Theme
import "richtext.js" as RichText

// A Txt with one part of it a pencil link, such as the count in "Very
// accurate, 67 languages." that opens the list of them. plainText is the
// whole text and link the part of it that links, its first occurrence;
// without a link it is plain text. The link is underlined and followed by
// a click, which goes no further, so a card around it is not chosen; Tab
// reaches the text, and Return or Space follows the link.
Txt {
    id: root

    property string plainText
    property string link
    signal activated()

    readonly property int _at: link.length ? plainText.indexOf(link) : -1
    readonly property bool _linked: _at >= 0

    text: !_linked ? plainText
        : RichText.escape(plainText.slice(0, _at)) + "<a href=\"link\">" + RichText.escape(link) + "</a>"
          + RichText.escape(plainText.slice(_at + link.length))
    textFormat: _linked ? Text.StyledText : Text.PlainText
    linkColor: Theme.accent
    onLinkActivated: { focusVisible.pointed(); activated() }

    activeFocusOnTab: _linked
    Accessible.role: _linked ? Accessible.Link : Accessible.StaticText
    Accessible.name: plainText
    Accessible.onPressAction: if (_linked) activated()
    Keys.onReturnPressed: { focusVisible.keyed(); activated() }
    Keys.onSpacePressed: { focusVisible.keyed(); activated() }
    FocusVisible { id: focusVisible }
    FocusRing { target: root; shown: focusVisible.shown; targetRadius: Theme.radiusSmall }

    HoverHandler {
        enabled: root.hoveredLink.length > 0
        cursorShape: Qt.PointingHandCursor
    }
}
