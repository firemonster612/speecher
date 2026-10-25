import QtQuick
import Speecher.Theme

// Text in one of the type styles. variant picks size and weight, tone picks
// colour, strong sets it a weight heavier for emphasis inside a line.
Text {
    // display, title, heading, body, label, labelSmall (a label in small
    // type, for buttons and chips), small, caption, overline (a small bold
    // label), mono (tabular figures), code (a monospace face),
    // transcript (dictated words), lead (a large sample sentence).
    property string variant: "body"
    // primary, secondary, tertiary, disabled, accent, positive, warning,
    // negative, onAccent.
    property string tone: "primary"
    property bool strong: false

    readonly property var _sizes: ({
        display: Theme.fontDisplay, title: Theme.fontTitle, heading: Theme.fontHeading,
        body: Theme.fontBody, label: Theme.fontBody, labelSmall: Theme.fontSmall, small: Theme.fontSmall,
        caption: Theme.fontCaption, overline: Theme.fontCaption, mono: Theme.fontSmall,
        code: Theme.fontSmall, transcript: Theme.fontTranscript, lead: Theme.fontTitle
    })
    readonly property var _weights: ({
        display: Font.Bold, title: Font.Bold, heading: Font.Bold,
        body: Font.Normal, label: Font.Medium, labelSmall: Font.Medium, small: Font.Normal,
        caption: Font.Medium, overline: Font.Bold, mono: Font.Medium,
        code: Font.Normal, transcript: Font.Normal, lead: Font.Normal
    })

    color: Theme.tone(tone)
    font.family: variant === "code" ? Theme.monoFamily : Theme.fontFamily
    font.pixelSize: _sizes[variant] || Theme.fontBody
    font.weight: Math.max(strong ? Font.DemiBold : Font.Normal, _weights[variant] || Font.Normal)
    font.features: variant === "mono" ? { "tnum": 1 } : ({})
    lineHeight: variant === "transcript" || variant === "lead" ? 1.25 : variant === "body" || variant === "small" ? 1.15 : 1.0
    textFormat: Text.PlainText

    Behavior on color { EaseColor {} }
}
