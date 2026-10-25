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
    // follows systemDark on System, and the colours, translucency and layout
    // to theirs.
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
    // pad.
    //
    // That pad is the first of the app's colour schemes. A scheme is six
    // roles in light and dark: paper (window and sidebar), sheet (the page),
    // raised (cards), text, accent and negative, and a rule if it wants its
    // own. Every other colour below derives from the current scheme, so a
    // scheme only picks those. Pickers draw a scheme they are not using
    // through schemeOf.
    readonly property var presets: [
        { id: "steno", label: "Steno",
          light: { paper: "#E4E8DF", sheet: "#EEF1EA", raised: "#FBFCF9", text: "#1D2320",
                   rule: Qt.rgba(0.25, 0.38, 0.5, 0.16), accent: "#2B54C9", negative: "#D23F34" },
          dark: { paper: "#1A1F1D", sheet: "#202624", raised: "#29302D", text: "#E3E8E2",
                  rule: Qt.rgba(0.78, 0.85, 0.9, 0.10), accent: "#93AEF5", negative: "#F0695C" } },
        { id: "blueprint", label: "Blueprint",
          light: { paper: "#DCE5F0", sheet: "#E8EEF6", raised: "#F8FAFD", text: "#13233A", accent: "#1F5FD6", negative: "#C93A2F" },
          dark: { paper: "#0F1826", sheet: "#141F30", raised: "#1C2A3E", text: "#DCE6F4", accent: "#79AEFF", negative: "#F2766A" } },
        { id: "lagoon", label: "Lagoon",
          light: { paper: "#D8E8E5", sheet: "#E5F0EE", raised: "#F7FBFA", text: "#0F2F2B", accent: "#0A746F", negative: "#C93A2F" },
          dark: { paper: "#0E1D1C", sheet: "#132524", raised: "#1A302E", text: "#D6EBE8", accent: "#4FD1C5", negative: "#F2766A" } },
        { id: "lilac", label: "Lilac",
          light: { paper: "#E5E0F0", sheet: "#EEEAF6", raised: "#FBFAFD", text: "#241B38", accent: "#6A3FD1", negative: "#C93A2F" },
          dark: { paper: "#19152A", sheet: "#1F1A33", raised: "#282240", text: "#E6E0F5", accent: "#B79CFF", negative: "#F2766A" } },
        { id: "rose", label: "Rose",
          light: { paper: "#F0DFE3", sheet: "#F6E9EC", raised: "#FDF8F9", text: "#36151F", accent: "#C0185A", negative: "#C4362B" },
          dark: { paper: "#24151A", sheet: "#2C1A20", raised: "#36222A", text: "#F4E1E6", accent: "#FF8AB8", negative: "#FF7A6B" } },
        { id: "sand", label: "Sand",
          light: { paper: "#ECE2D1", sheet: "#F4ECDE", raised: "#FDF9F2", text: "#2F2416", accent: "#A84C14", negative: "#C4283A" },
          dark: { paper: "#1E1912", sheet: "#251F17", raised: "#2F281E", text: "#F1E6D6", accent: "#F2A65E", negative: "#FF7A85" } }
    ]
    // What the colours come from: a preset, or a desktop scheme's roles
    // (window, text, accent, negative; any may be missing).
    // ThemeSetting sets it from the Colors setting.
    property var colorSource: presets[0]
    readonly property var scheme: schemeOf(colorSource, dark)

    // The six roles of source in light or dark.
    function schemeOf(source, inDark) {
        if (source.light) return inDark ? source.dark : source.light
        return _fromDesktop(source, inDark)
    }

    // A desktop scheme as one of ours. One as light or dark as the theme keeps
    // its window colour and text; the other way round, its hue tints our own
    // paper and ink. Cards are always a step lighter than the page. The text
    // must read at AAA on the page, the cards and the sidebar over any
    // desktop: it is pushed first, and where it can go no further, the paper
    // gives way. Theme's inks push the rest until they read.
    function _fromDesktop(roles, inDark) {
        const fallback = presets[0][inDark ? "dark" : "light"]
        const window = roles.window ? Qt.color(roles.window) : null
        const own = window !== null && _isDark(window) === inDark
        const hue = Qt.color(own ? window : (roles.accent || window || fallback.accent))
        const wanted = own ? window : _paperOf(hue, inDark)
        const ink = own && roles.text ? Qt.color(roles.text) : Qt.hsla(_hue(hue), inDark ? 0.10 : 0.16, inDark ? 0.90 : 0.13, 1)
        const text = readableOn(ink, _groundsOf(wanted, inDark), 7, inDark)
        const paper = _shift(wanted, inDark ? -0.025 : 0.025, p => _groundsOf(p, inDark).every(g => contrast(text, g) >= 7))
        return {
            paper: paper, sheet: _sheetOf(paper, inDark), raised: _raisedOf(paper, inDark), text: text,
            accent: Qt.color(roles.accent || fallback.accent),
            negative: Qt.color(roles.negative || fallback.negative)
        }
    }
    function _sheetOf(paper, inDark) { return mix(paper, "white", inDark ? 0.03 : 0.37) }
    function _raisedOf(paper, inDark) { return mix(paper, "white", inDark ? 0.07 : 0.85) }
    // Everything a desktop scheme's text is laid on: the page, the cards and
    // the sidebar, translucent over a black or a white desktop.
    function _groundsOf(paper, inDark) {
        return [_sheetOf(paper, inDark), _raisedOf(paper, inDark)].concat(_overDesktop(paper, inDark))
    }
    function _hue(c) { return Math.max(0, c.hslHue) }
    // A paper with c's hue, kept quiet, as light or dark as our own.
    function _paperOf(c, inDark) {
        return Qt.hsla(_hue(c), Math.min(c.hslSaturation, inDark ? 0.22 : 0.28), inDark ? 0.11 : 0.89, 1)
    }
    function _isDark(c) { return luminance(c) < 0.18 }

    // a blended toward b by t, 0 to 1.
    function mix(a, b, t) {
        const to = Qt.color(b)
        return Qt.tint(a, Qt.rgba(to.r, to.g, to.b, t))
    }
    // WCAG's relative luminance and contrast ratio.
    function luminance(c) {
        const k = v => v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4)
        const q = Qt.color(c)
        return 0.2126 * k(q.r) + 0.7152 * k(q.g) + 0.0722 * k(q.b)
    }
    function contrast(a, b) {
        const la = luminance(a), lb = luminance(b)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }
    // fg, lightened in dark or darkened in light until it reads at ratio on
    // every one of grounds, or as far as it goes: to white or black.
    function readableOn(fg, grounds, ratio, inDark) {
        return _shift(fg, inDark ? 0.025 : -0.025, c => grounds.every(g => contrast(c, g) >= ratio))
    }
    // c with its lightness moved by step until done(c), or until it is white
    // or black.
    function _shift(c, step, done) {
        c = Qt.color(c)
        while (!done(c) && (step > 0 ? c.hslLightness < 1 : c.hslLightness > 0))
            c = Qt.hsla(_hue(c), c.hslSaturation, Math.max(0, Math.min(1, c.hslLightness + step)), 1)
        return c
    }

    // The side of the window behind the page shows the desktop through a
    // blur; ThemeSetting turns it on where the platform blurs and the
    // Translucent sidebar setting is on. Paper drawn there lets a little of
    // the blur through.
    property bool translucent: false
    readonly property color sidePaper: translucent ? Qt.rgba(paper.r, paper.g, paper.b, _sideOpacity(dark)) : paper
    function _sideOpacity(inDark) { return inDark ? 0.84 : 0.85 }
    // paper laid at the sidebar's opacity over a black and a white desktop:
    // the darkest and lightest the blur behind it can make it.
    function _overDesktop(paper, inDark) {
        return [mix("black", paper, _sideOpacity(inDark)), mix("white", paper, _sideOpacity(inDark))]
    }
    // What ink is laid on: the page and cards, and the sidebar, its paper
    // or, translucent, that paper over any desktop.
    readonly property var pageGrounds: [sheet, raised]
    readonly property var sideGrounds: translucent ? _overDesktop(paper, dark) : [paper]
    readonly property var _everyGround: pageGrounds.concat(sideGrounds)
    // The page beside the sidebar, edge to edge, instead of on a sheet framed
    // by the window; ThemeSetting follows the Layout setting.
    property bool split: false

    readonly property color paper: scheme.paper      // window and sidebar
    readonly property color sheet: scheme.sheet      // the page
    // Cards, buttons, menus: anything that sits a shade above the page.
    readonly property color raised: scheme.raised
    readonly property color rule: scheme.rule || soft(text, dark ? 0.10 : 0.14)

    // The ink and paper the shades below mix from: hover, borders and fills
    // are text laid thinly over the surface.
    readonly property color sheetBorder: soft(text, dark ? 0.07 : 0.12)
    readonly property color surface: dark ? mix(raised, sheet, 0.2) : mix(sheet, paper, 0.3)
    readonly property color surfaceHover: dark ? mix(raised, text, 0.02) : paper
    readonly property color surfacePressed: dark ? mix(raised, text, 0.05) : mix(paper, text, 0.04)
    readonly property color border: soft(text, dark ? 0.10 : 0.16)
    readonly property color borderStrong: soft(text, dark ? 0.18 : 0.28)
    readonly property color fill: soft(text, 0.05)
    readonly property color fillStrong: soft(text, 0.09)
    readonly property color scrim: dark ? Qt.rgba(0.05, 0.07, 0.06, 0.55) : Qt.rgba(0.12, 0.16, 0.14, 0.22)
    readonly property color shadow: dark ? Qt.rgba(0, 0, 0, 0.45) : Qt.rgba(0.1, 0.15, 0.12, 0.16)
    // The low shadow under a card resting on the page.
    readonly property color cardShadow: dark ? Qt.rgba(0, 0, 0, 0.25) : Qt.rgba(0.1, 0.16, 0.12, 0.07)
    // The hairline around a switch knob or slider handle, so it holds its
    // shape on a pale track.
    readonly property color knobEdge: Qt.rgba(0, 0, 0, 0.16)
    // A slider's handle. It sits on a raised card, so in dark it takes ink
    // rather than the card's own colour, which would hide it.
    readonly property color sliderHandle: dark ? text : raised

    // Ink: text, and the fill of primary buttons and lit keys. The quieter
    // inks are text faded toward the page.
    // Text reads at AAA, help and captions at AA, and tertiary labels and
    // placeholders at 3:1, wherever they are laid, whatever the scheme.
    readonly property color text: readableOn(scheme.text, _everyGround, 7, dark)
    readonly property color textSecondary: readableOn(mix(text, sheet, dark ? 0.32 : 0.28), _everyGround, 4.5, dark)
    readonly property color textTertiary: readableOn(mix(text, sheet, dark ? 0.58 : 0.5), _everyGround, 3, dark)
    readonly property color textDisabled: mix(text, sheet, dark ? 0.76 : 0.72)
    readonly property color textHover: mix(text, sheet, dark ? 0.09 : 0.05)
    readonly property color textPressed: mix(text, sheet, dark ? 0.15 : 0.035)

    // Blue pencil: what you can act on, at AA on the page and cards.
    readonly property color accent: readableOn(scheme.accent, pageGrounds, 4.5, dark)
    readonly property color accentHover: mix(accent, dark ? "white" : "black", dark ? 0.2 : 0.15)
    // Ink on the accent: white, or the accent's own deep shade, whichever
    // reads better on it.
    readonly property color accentText: {
        const deep = mix(accent, "black", 0.85)
        return contrast(deep, accent) > contrast("#FFFFFF", accent) ? deep : "#FFFFFF"
    }
    readonly property color accentSoft: Qt.rgba(accent.r, accent.g, accent.b, dark ? 0.16 : 0.10)
    readonly property color accentRing: Qt.rgba(accent.r, accent.g, accent.b, 0.4)

    readonly property color positive: dark ? "#6CCB8F" : "#2E7D4F"
    readonly property color warning: dark ? "#E3B65C" : "#9A6A12"
    // The margin's red: live sound (the record dot, the level meter, the
    // popup's margin rule) and failures, which are text, at AA wherever they
    // are laid.
    readonly property color negative: readableOn(scheme.negative, _everyGround, 4.5, dark)

    // A key cap's base, the darker edge under its face; lit for an inked cap.
    readonly property color keyBase: dark ? mix(paper, "black", 0.2) : mix(paper, text, 0.1)
    readonly property color keyBaseLit: dark ? mix(text, paper, 0.4) : mix(text, paper, 0.08)

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
    // A menu's entries sit this far inside its panel and take the panel's
    // radius less it.
    readonly property int menuInset: space1
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
    // Where text starts in from a window's side column and setup's footer.
    readonly property int sideInset: 22
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
