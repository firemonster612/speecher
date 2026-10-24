import QtQuick
import QtQuick.Effects
import QtQuick.Templates as T
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend

// The dictation popup: one capsule that grows, shrinks and changes its
// content as a dictation moves from listening to delivered. It reads the
// dictation from source (the live one by default; the settings page passes a
// demo) and the popup rows from Settings.
Item {
    id: popup

    property DictationState source: Dictation
    readonly property string phase: source.state
    readonly property bool shown: phase !== "idle"
    readonly property bool live: phase === "listening" || phase === "paused" || phase === "transcribing" || phase === "refining"
    readonly property string liveText: phase === "refining" && popup.source.refined.length ? popup.source.refined : popup.source.words
    readonly property bool textOn: phase === "refining" ? Settings.values.showRefinementText !== false
                                                         : Settings.values.showLiveText !== false
    readonly property bool showText: live && textOn && liveText.length > 0
    // The last working phase, so its label holds while the bar fades out.
    property string workPhase: "transcribing"
    onPhaseChanged: if (phase === "transcribing" || phase === "refining") workPhase = phase

    // The capsule's size for the current phase. Width and height animate to
    // it, so the shape morphs between phases.
    readonly property size target: {
        if (phase === "review") return Qt.size(widest, review.implicitHeight + 36)
        if (phase === "delivered" || phase === "error") return Qt.size(Math.min(widest, Math.max(320, receipt.implicitWidth + 40)), 64)
        if (showText) return Qt.size(540, 58 + words.implicitHeight + 14)
        return Qt.size(phase === "listening" || phase === "paused" ? 360 : 300, 58)
    }

    // Where the margin rule falls inside the slip.
    readonly property int marginX: 54
    // A long edit scrolls past this height rather than growing the capsule.
    readonly property int reviewTextMax: 220
    // The capsule's width in review, the widest phase.
    readonly property int widest: 580
    // The most the capsule ever grows to: the review with its edit at the
    // scroll limit, the tallest phase. A window this size never resizes. It
    // adds up the review's fixed parts rather than subtracting the edit from
    // the column, which lags the edit by a frame while it lays out.
    readonly property size largest: Qt.size(widest, reviewHeading.height + reviewActions.height + review.spacing * 2 + reviewTextMax + 36)

    // Where the capsule settles for the current phase, in the popup's
    // coordinates. It moves with the phase and when the live words take
    // another line, not on every frame of the morph between them.
    readonly property rect capsule: Qt.rect((width - target.width) / 2, height - target.height, target.width, target.height)

    // As big as the capsule, or as it is while it grows or shrinks to the
    // target, so a scene that sizes to the popup never cuts it off.
    implicitWidth: Math.max(holder.width, target.width)
    implicitHeight: Math.max(holder.height, target.height)

    Item {
        id: holder
        width: popup.target.width
        height: popup.target.height
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        Behavior on width { Ease { duration: Theme.page; curve: Theme.springy } }
        Behavior on height { Ease { duration: Theme.page; curve: Theme.springy } }

        // Arrives from below with a small overshoot, leaves quickly.
        opacity: popup.shown ? 1 : 0
        scale: popup.shown ? 1 : Theme.motionScale(0.86)
        transform: Translate { y: popup.shown ? 0 : 18 * Theme.movement; Behavior on y { Ease { duration: popup.shown ? Theme.page : Theme.base; curve: popup.shown ? Theme.springy : Theme.easeIn } } }
        transformOrigin: Item.Bottom
        Behavior on opacity { Ease { duration: Theme.base } }
        Behavior on scale { Ease { duration: popup.shown ? Theme.page : Theme.base; curve: popup.shown ? Theme.springy : Theme.easeIn } }

        RectangularShadow {
            anchors.fill: shell
            radius: shell.radius
            blur: 24
            spread: -4
            offset.y: 8
            color: Theme.shadow
        }
        Rectangle {
            id: shell
            anchors.fill: parent
            radius: Theme.radiusPanel
            color: Theme.sheet
            border.width: 1
            border.color: Theme.borderStrong
            clip: true

            // The slip's margin rule: the mark sits left of it, words and
            // sound run to its right.
            Rectangle {
                x: popup.marginX
                width: 1
                height: parent.height
                color: Theme.soft(Theme.negative, popup.phase === "listening" ? 0.55 : 0.35)
                Behavior on color { EaseColor { duration: Theme.base } }
            }

            // Live: words above, the control bar below ----------------------
            Item {
                id: liveLayer
                anchors.fill: parent
                opacity: popup.live ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { Ease { duration: popup.live ? Theme.base : Theme.fast } }

                LiveWords {
                    id: words
                    x: popup.marginX + 16
                    y: 16
                    width: parent.width - x - 22
                    text: popup.liveText
                    maxWords: Settings.values.previewWords || 12
                    shimmer: popup.phase === "refining" && popup.source.refined.length === 0
                    opacity: popup.showText ? 1 : 0
                    Behavior on opacity { Ease {} }
                }

                Item {
                    id: bar
                    width: parent.width
                    height: 58
                    anchors.bottom: parent.bottom

                    VoiceOrb {
                        id: orb
                        x: (popup.marginX - width) / 2
                        anchors.verticalCenter: parent.verticalCenter
                        phase: popup.phase
                        level: popup.source.level
                    }

                    Waveform {
                        id: wave
                        x: popup.marginX + 16
                        width: clock.x - x - 12
                        anchors.verticalCenter: parent.verticalCenter
                        height: 26
                        bars: Math.max(8, Math.floor(width / 5.5))
                        level: popup.source.level
                        active: popup.phase === "listening"
                        opacity: popup.phase === "listening" || popup.phase === "paused" ? 1 : 0
                        Behavior on opacity { Ease {} }
                    }

                    Column {
                        x: popup.marginX + 16
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1
                        opacity: popup.phase === "transcribing" || popup.phase === "refining" ? 1 : 0
                        Behavior on opacity { Ease {} }
                        MorphText {
                            text: popup.workPhase === "refining" ? popup.source.text.refining : popup.source.text.transcribing
                            variant: "label"
                            strong: true
                        }
                        Txt {
                            text: popup.workPhase === "refining" ? popup.source.text.refiningWith : popup.source.speechService
                            variant: "small"
                            tone: "secondary"
                        }
                    }

                    MorphText {
                        id: clock
                        anchors.right: buttons.left
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: Math.floor(popup.source.elapsed / 60) + ":" + String(popup.source.elapsed % 60).padStart(2, "0")
                        variant: "mono"
                        tone: popup.phase === "paused" ? "warning" : "secondary"
                        opacity: popup.phase === "listening" || popup.phase === "paused" ? 1 : 0
                        Behavior on opacity { Ease {} }
                    }

                    Row {
                        id: buttons
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Reveal {
                            sideways: true
                            open: popup.phase === "listening" || popup.phase === "paused"
                            anchors.verticalCenter: parent.verticalCenter
                            CircleButton {
                                iconName: popup.phase === "paused" ? "play" : "pause"
                                tooltip: popup.phase === "paused" ? Words.resume : Words.pause
                                onClicked: popup.source.pauseRequested()
                            }
                        }
                        CircleButton {
                            iconName: "x"
                            tooltip: Words.cancel
                            onClicked: popup.source.cancelRequested()
                        }
                    }
                }
            }

            // Delivered or failed: one line of outcome ----------------------
            Row {
                id: receipt
                x: (popup.marginX - 30) / 2
                anchors.verticalCenter: parent.verticalCenter
                spacing: (popup.marginX - 30) / 2 + 16
                opacity: popup.phase === "delivered" || popup.phase === "error" ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { Ease { duration: Theme.slow } }

                VoiceOrb { id: receiptOrb; phase: popup.phase; size: 30; anchors.verticalCenter: parent.verticalCenter }
                Column {
                    // The words take what the widest capsule has left once the
                    // orb, the action and the spacing are in, and elide past it,
                    // so a long error never pushes its action out.
                    readonly property real room: popup.widest - 40 - receiptOrb.width - receiptEnd.width
                                                 - (receiptAction.visible ? receiptAction.implicitWidth + receipt.spacing * 3 : receipt.spacing * 2)
                    width: Math.min(Math.max(receiptTitle.implicitWidth, receiptDetail.implicitWidth), room)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Txt {
                        id: receiptTitle
                        width: parent.width
                        text: popup.phase === "error" ? popup.source.error.title : popup.source.receipt.title
                        variant: "label"
                        strong: true
                        elide: Text.ElideRight
                    }
                    Txt {
                        id: receiptDetail
                        width: parent.width
                        text: popup.phase === "error" ? popup.source.error.detail : popup.source.receipt.detail
                        variant: "small"
                        tone: "secondary"
                        elide: Text.ElideRight
                    }
                }
                Button {
                    id: receiptAction
                    visible: popup.phase === "error" && popup.source.error.action.length > 0
                    text: popup.source.error.action || ""
                    size: "small"
                    variant: "primary"
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: popup.source.errorActionRequested()
                }
                Item { id: receiptEnd; width: 2; height: 1 }
            }

            // Review: the edit to a selection, waiting for Replace -----------
            VoiceOrb {
                x: (popup.marginX - width) / 2
                y: 16
                phase: "review"
                opacity: popup.phase === "review" ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { Ease { duration: Theme.slow } }
            }
            Column {
                id: review
                x: popup.marginX + 16
                y: 18
                width: parent.width - x - 20
                spacing: 14
                opacity: popup.phase === "review" ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { Ease { duration: Theme.slow } }

                Column {
                    id: reviewHeading
                    spacing: 2
                    Txt { text: popup.source.text.reviewTitle; variant: "label"; strong: true }
                    Txt { text: popup.source.text.youSaid; variant: "small"; tone: "secondary" }
                }

                Flickable {
                    width: parent.width
                    height: Math.min(edit.implicitHeight, popup.reviewTextMax)
                    contentHeight: edit.implicitHeight
                    interactive: contentHeight > height
                    boundsBehavior: Flickable.StopAtBounds
                    // A mouse drags nothing on a desktop; touch still does.
                    acceptedButtons: Qt.NoButton
                    WheelScroll {}
                    clip: true
                    T.ScrollBar.vertical: ScrollBar {}
                    DiffText { id: edit; segments: popup.source.review.segments; variant: "transcript" }
                }

                // Each button names its key, so the edit can be answered
                // without the pointer.
                Row {
                    id: reviewActions
                    anchors.right: parent.right
                    spacing: 8
                    Button {
                        text: Words.cancel
                        trailing: KeyCombo { keys: ["Esc"]; size: "small" }
                        onClicked: popup.source.cancelRequested()
                    }
                    Button {
                        text: popup.source.text.askForMore
                        trailing: KeyCombo { keys: Settings.values.globalShortcut || []; size: "small" }
                        onClicked: popup.source.followUpRequested()
                    }
                    Button {
                        text: popup.source.text.replace
                        variant: "primary"
                        trailing: KeyCombo { keys: ["↵"]; size: "small" }
                        onClicked: popup.source.replaceRequested()
                    }
                }
            }
        }
    }
}
