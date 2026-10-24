pragma Singleton
import QtQuick
import Speecher.Launch

// Every colour, size, font and motion value the UI uses. Components read
// these and never hard-code their own, so a palette or timing change is one
// edit here.
QtObject {
    id: theme

    // The desktop's scheme is Qt's, unless --theme=light|dark says otherwise
    // on the command line, as the preview tool does with --time-scale=<n>.
    // ThemeSetting (in Speecher.App) binds dark to the Theme setting, which
    // follows systemDark on System.
    // The time scale stretches every animation, which the preview tool uses
    // to capture smooth video; the real app leaves it at 1.
    readonly property bool systemDark: Launch.arg("theme", "") ? Launch.arg("theme", "") === "dark"
                                                              : Application.styleHints.colorScheme === Qt.ColorScheme.Dark
    property bool dark: systemDark
    readonly property real timeScale: Number(Launch.arg("time-scale", "1"))
    // Reduced motion keeps fades and drops movement: multiply every offset an
    // animation travels by movement. Core sets it from the system setting.
    property bool reduceMotion: Launch.arg("reduce-motion", "false") === "true"
    readonly property real movement: reduceMotion ? 0 : 1

    // Palette ------------------------------------------------------------
    // A stenographer's pad: pale green-grey paper, blue ruled lines, a red
    // margin rule, ink. Blue pencil marks what you can act on; the red of the
    // margin means live sound or a failure. Settings sit in cards a
    // shade lighter than the page, so groups read as raised sheets on the
    // pad. Both schemes are here so the theme picker can draw the one not in
    // use; everything else reads the current one through the colours below.
    readonly property var lightScheme: ({
        paper: "#E4E8DF", sheet: "#EEF1EA", raised: "#FBFCF9", text: "#1D2320",
        rule: Qt.rgba(0.25, 0.38, 0.5, 0.16), accent: "#2B54C9", negative: "#D23F34"
    })
    readonly property var darkScheme: ({
        paper: "#1A1F1D", sheet: "#202624", raised: "#29302D", text: "#E3E8E2",
        rule: Qt.rgba(0.78, 0.85, 0.9, 0.10), accent: "#93AEF5", negative: "#F0695C"
    })
    readonly property var scheme: dark ? darkScheme : lightScheme

    readonly property color paper: scheme.paper      // window and sidebar
    readonly property color sheet: scheme.sheet      // the page
    // Cards, buttons, menus: anything that sits a shade above the page.
    readonly property color raised: scheme.raised
    readonly property color rule: scheme.rule

    readonly property color sheetBorder: dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0.1, 0.16, 0.12, 0.12)
    readonly property color surface: dark ? "#272E2B" : "#EBEFE7"
    readonly property color surfaceHover: dark ? "#2C3431" : "#E4E9E0"
    readonly property color surfacePressed: dark ? "#323A37" : "#DCE2D7"
    readonly property color border: dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0.1, 0.16, 0.12, 0.16)
    readonly property color borderStrong: dark ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(0.1, 0.16, 0.12, 0.28)
    readonly property color fill: dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0.1, 0.18, 0.14, 0.05)
    readonly property color fillStrong: dark ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(0.1, 0.18, 0.14, 0.09)
    readonly property color scrim: dark ? Qt.rgba(0.05, 0.07, 0.06, 0.55) : Qt.rgba(0.12, 0.16, 0.14, 0.22)
    readonly property color shadow: dark ? Qt.rgba(0, 0, 0, 0.45) : Qt.rgba(0.1, 0.15, 0.12, 0.16)
    // The low shadow under a card resting on the page.
    readonly property color cardShadow: dark ? Qt.rgba(0, 0, 0, 0.25) : Qt.rgba(0.1, 0.16, 0.12, 0.07)
    // The hairline around a switch knob or slider handle, so it holds its
    // shape on a pale track.
    readonly property color knobEdge: Qt.rgba(0, 0, 0, 0.16)

    // Ink: text, and the fill of primary buttons and lit keys.
    readonly property color text: scheme.text
    readonly property color textSecondary: dark ? "#A2ACA5" : "#56605A"
    readonly property color textTertiary: dark ? "#717B75" : "#868F89"
    readonly property color textDisabled: dark ? "#4E5753" : "#B4BBB5"
    readonly property color textHover: dark ? "#D1D5D0" : "#272F2B"
    readonly property color textPressed: dark ? "#C5CAC4" : "#242C28"

    // Blue pencil: what you can act on.
    readonly property color accent: scheme.accent
    readonly property color accentHover: dark ? "#A9BFF8" : "#2347AE"
    readonly property color accentText: dark ? "#14192A" : "#FFFFFF"
    readonly property color accentSoft: Qt.rgba(accent.r, accent.g, accent.b, dark ? 0.16 : 0.10)
    readonly property color accentRing: Qt.rgba(accent.r, accent.g, accent.b, 0.4)

    readonly property color positive: dark ? "#6CCB8F" : "#2E7D4F"
    readonly property color warning: dark ? "#E3B65C" : "#9A6A12"
    // The margin's red: live sound (the record dot, the level meter, the
    // popup's margin rule) and failures.
    readonly property color negative: scheme.negative

    // A key cap's base, the darker edge under its face; lit for an inked cap.
    readonly property color keyBase: dark ? "#151918" : "#CCCFC7"
    readonly property color keyBaseLit: dark ? "#8E918D" : "#303A35"

    // How far a disabled control fades.
    readonly property real disabledOpacity: 0.45

    // The colour for a tone name, as Txt, Badge and status lines use them:
    // primary, secondary, tertiary, disabled, accent, positive,
    // warning, negative, onAccent; neutral reads as secondary.
    function tone(name) {
        switch (name) {
        case "secondary": case "neutral": return textSecondary
        case "tertiary": return textTertiary
        case "disabled": return textDisabled
        case "accent": return accent
        case "positive": return positive
        case "warning": return warning
        case "negative": return negative
        case "onAccent": return accentText
        default: return text
        }
    }
    function soft(c, a) { return Qt.rgba(c.r, c.g, c.b, a === undefined ? (dark ? 0.14 : 0.1) : a) }
    // c as a CSS rgba() string, for rich text.
    function css(c, a) { return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + "," + (a === undefined ? c.a : a) + ")" }

    // An app with no icon of its own gets its initial on one of these muted
    // inks, picked from its name, so a list of apps reads as names rather
    // than confetti.
    readonly property var avatarInks: ["#5D6B8A", "#7A5F55", "#4F7363", "#76688A", "#806F45", "#4D6F7A", "#8A5A66", "#5E6E55"]
    readonly property color onAvatar: "#FFFFFF"

    // Type ---------------------------------------------------------------
    // Atkinson Hyperlegible Next, drawn by the Braille Institute for
    // readers with low vision: one family, set on a classic scale.
    readonly property string fontFamily: "Atkinson Hyperlegible Next"
    readonly property string iconFamily: "lucide"
    // Commands, prompts and snippets people type or paste.
    readonly property string monoFamily: "monospace"

    readonly property int fontDisplay: 26
    readonly property int fontTitle: 21
    readonly property int fontHeading: 16
    // Dictated words: the popup's live text and a transcript.
    readonly property int fontTranscript: 15
    readonly property int fontBody: 14
    readonly property int fontSmall: 12
    readonly property int fontCaption: 11

    // Size ---------------------------------------------------------------
    // Radius follows hierarchy: the window's page is softest, controls are
    // firm, rows and sections have none. A shape set concentrically inside
    // another takes the outer radius less the gap between them.
    readonly property int radiusSmall: 4
    readonly property int radius: 6
    readonly property int radiusLarge: 8
    readonly property int radiusPanel: 12
    // The fill under a row on a card (hover, press, selection, a form) sits
    // this far inside the card, so its corners follow the card's.
    readonly property int washInset: 3
    readonly property int washRadius: radiusLarge - washInset
    // A window's own corners and the shadow margin round it, which also
    // serves as its resize grip.
    readonly property int radiusWindow: 14
    readonly property int windowShadow: 14
    // The strip along a window's top that moves it and holds its buttons.
    // The brand mark and the window buttons are square and centred in it, so
    // the mark sits as far from the window's top edge as from its left.
    readonly property int titleBarHeight: 52
    readonly property int titleBarButton: 28
    readonly property int titleBarInset: (titleBarHeight - titleBarButton) / 2

    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 20
    readonly property int space6: 24
    readonly property int space8: 32

    readonly property int controlHeight: 32
    // A dropdown and a text field at the right end of a settings row.
    readonly property int selectWidth: 220
    readonly property int fieldWidth: 260
    readonly property int rowHeight: 52
    // How far a row's content sits in from the edge of its card.
    readonly property int rowInset: space4
    // Between the sections of a page.
    readonly property int sectionGap: 22
    readonly property int sidebarWidth: 232
    readonly property int contentMaxWidth: 860
    // Width of a side column, for pages that set a label beside content.
    readonly property int marginWidth: 190

    // Motion -------------------------------------------------------------
    // Durations already include the time scale. Use these with the curves
    // below for every animation so the whole app moves as one. Most motion
    // stays under 300 ms; page is for whole-page changes only.
    readonly property int fast: 120 * timeScale
    readonly property int base: 200 * timeScale
    readonly property int slow: 280 * timeScale
    readonly property int page: 340 * timeScale
    function ms(value) { return value * timeScale }
    // A scale something passes through as it presses, grows or pops in;
    // reduced motion flattens it to 1, as movement does an offset.
    function motionScale(value) { return 1 + (value - 1) * movement }
    // How long the pointer rests on a control before its tooltip shows.
    readonly property int tipDelay: 450 * timeScale
    // How long a toast stays before it leaves on its own.
    readonly property int toastTime: 3600 * timeScale

    // Decelerates hard: things arriving.
    readonly property var easeOut: [0.16, 1, 0.3, 1, 1, 1]
    // Even in and out: things moving between two places.
    readonly property var easeInOut: [0.65, 0, 0.35, 1, 1, 1]
    // A small overshoot: knobs, thumbs and pills that land.
    readonly property var springy: [0.34, 1.45, 0.64, 1, 1, 1]
    // Accelerates away: things leaving.
    readonly property var easeIn: [0.5, 0, 0.75, 0, 1, 1]
    // Soft at both ends: pulses and breathing that loop.
    readonly property var breathe: [0.37, 0, 0.63, 1, 1, 1]
    // Constant speed: only for motion that never settles, such as a
    // spinner's turn or a sweep that loops.
    readonly property var steady: [0, 0, 1, 1, 1, 1]
}
