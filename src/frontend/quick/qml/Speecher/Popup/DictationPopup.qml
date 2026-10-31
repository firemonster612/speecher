import QtQuick
import QtQuick.Effects
import QtQuick.Templates as T
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// The dictation popup: one pill that grows, shrinks and changes its content
// as a dictation moves from listening to delivered, with its state mark (the
// orb) in its left end and its round buttons in its right. It reads the
// dictation from source (the live one by default; the settings page passes a
// demo) and the popup rows from Settings, and words it itself (t). The live
// one carries the update and What's New offers over the capsule (OfferChips).
Item {
    id: popup

    property DictationState source: Dictation
    readonly property string phase: source.state
    readonly property bool shown: phase !== "idle"
    // Faded out, so the window carrying it can go.
    readonly property bool empty: !shown && holder.opacity === 0
    readonly property bool live: ["starting", "listening", "paused", "transcribing", "refining", "delivering"].includes(phase)
    readonly property bool recording: phase === "starting" || phase === "listening" || phase === "paused"
    // The refined words show from refining until the text is out.
    readonly property bool refinedPhase: phase === "refining" || phase === "delivering" && source.refined.length > 0
    readonly property string liveText: refinedPhase && popup.source.refined.length ? popup.source.refined : popup.source.words
    readonly property bool textOn: refinedPhase ? Settings.values.showRefinementText !== false
                                                : Settings.values.showLiveText !== false
    readonly property bool showText: live && textOn && liveText.length > 0
    // A sign-in renews before the dictation goes on; the bar says so in
    // place of the sound.
    readonly property bool renewing: recording && source.renewing
    // The voice shows while listening and paused: as the scatter ribbon, or
    // in Boring mode as level bars beside the record dot.
    readonly property bool voiceShown: (phase === "listening" || phase === "paused") && !renewing
    readonly property bool boring: Settings.values.boringMode === true
    readonly property bool ribbonShown: !boring && voiceShown
    // The last working phase, so its label holds while the bar fades out
    // and while the text is delivered.
    property string workPhase: "transcribing"
    // The last of delivered and error, so its words hold while it fades out.
    property string noticePhase: "delivered"
    // A new review opens folded; one followed up keeps its view.
    property bool wholeEdit: false
    onPhaseChanged: {
        if (phase === "transcribing" || phase === "refining") workPhase = phase
        if (phase === "delivered" || phase === "error") noticePhase = phase
        if (phase === "idle") wholeEdit = false
        // An error may show under a pointer that is already resting there.
        if (phase === "error") source.errorHoldRequested(hover.hovered)
        // Each dictation starts as a new popup, even while the last one's is up.
        if (phase === "starting") offers.newPopup()
    }
    readonly property var selectionEdit: source.review
    readonly property bool canFold: (selectionEdit.folded || []).length > 0
    // A follow-up is dictated under the edit it asks more of, which stays up
    // dimmed and without its buttons.
    readonly property bool followingUp: live && selectionEdit.following === true
    readonly property bool reviewShown: phase === "review" || followingUp
    readonly property real followUpDim: 0.5
    // The receipt's mark: what happened to the text, when it is not plainly
    // delivered.
    readonly property string receiptMark: noticePhase === "error" ? "error"
        : ["copied", "fallback", "cancelled"].includes(source.receipt.outcome) ? source.receipt.outcome : "delivered"
    readonly property var notice: noticePhase === "error" ? source.error : source.receipt
    readonly property bool noticeFix: (notice.page || "").length > 0

    readonly property var t: ({
        transcribing: "Transcribing…",
        refining: "Refining…",
        reviewTitle: "Edit to your selection",
        youSaid: said => "You said " + said,
        askForMore: "Ask for more changes",
        replace: "Replace",
        wholeEdit: "Show whole edit",
        changesOnly: "Show changes only",
        renewing: "Renewing sign-in…",
        dismiss: "Dismiss",
        open: pageId => "Open " + Schema.page(pageId).title
    })

    // The capsule's size for the current phase. Width and height animate to
    // it, so the shape morphs between phases.
    readonly property size target: {
        if (phase === "review") return Qt.size(widest, review.y * 2 + review.implicitHeight)
        if (followingUp) return Qt.size(widest, review.y + review.implicitHeight + liveHeight)
        if (phase === "delivered" || phase === "error") return Qt.size(Math.min(widest, Math.max(160, receipt.x + receipt.implicitWidth + receiptEnd)),
                                                                      Math.max(barHeight, receipt.implicitHeight + 20))
        return Qt.size(liveWidth, liveHeight)
    }
    // The live bar's width, wider while words show.
    readonly property int liveWidth: showText ? 460 : recording ? 280 : 220
    // The live bar, and the words above it when they show.
    readonly property real liveHeight: showText ? barHeight + words.y + words.implicitHeight - 4 : barHeight
    // The tallest it gets while words come in: every line of live text
    // shown, for a stage that holds it.
    readonly property real tallestLive: barHeight + words.y + words.maxLines * words.lineHeight - 4
    readonly property int barHeight: 44

    // The orb sits in the pill's round left end, and words and sound start
    // after it.
    readonly property int orbX: 10
    readonly property int orbSize: 24
    readonly property int contentX: orbX + orbSize + 10
    // The room after the receipt's words, or after its buttons: about the
    // gap above and below a small button, so it sits inside the pill's
    // round end rather than against it.
    readonly property int receiptEnd: receiptButtons.visible ? 14 : 18
    // A long edit or message scrolls past this height rather than growing
    // the capsule.
    readonly property int textMax: 180
    // The capsule's width in review, the widest phase: 500, or wider when
    // the review's buttons, with their keys, need it to stay on one row.
    // From the buttons, which keep their size while hidden, unlike the row.
    // The live Dictation names their keys before a review comes, so this is
    // settled before its window shows.
    readonly property int widest: Math.max(500, Math.ceil(contentX + 16 + replaceButton.implicitWidth + askButton.implicitWidth
                                                          + cancelButton.implicitWidth + reviewActions.spacing * 2))
    // The most the capsule ever grows to: the review with its edit at the
    // scroll limit, with its buttons or, during a follow-up, with the live
    // bar and the most lines of words under it. A window this size resizes
    // only when the keys do. It adds up the review's fixed parts rather than
    // subtracting the edit from the column, which lags the edit by a frame
    // while it lays out.
    readonly property size largest: Qt.size(widest, reviewHeading.height + review.spacing + textMax
        + Math.max(review.spacing + reviewActions.height + review.y * 2,
                   review.y + barHeight + words.y + words.maxLines * words.lineHeight - 4))

    // Where the capsule is drawn, in the popup's coordinates, frame by frame
    // as it grows and shrinks between phases and with the scale (about its
    // bottom centre) and lift it arrives and leaves with.
    readonly property rect capsule: Qt.rect(holder.x + holder.width * (1 - holder.scale) / 2,
                                            holder.y + holder.height * (1 - holder.scale) + lift.y,
                                            holder.width * holder.scale, holder.height * holder.scale)
    // Room over the capsule for both offers (OfferChips), the update's and
    // What's New's.
    readonly property int offerRoom: offers.fullHeight + offers.anchors.bottomMargin
    // What takes the pointer: the capsule, and while an offer shows, the
    // room over it.
    readonly property rect touchable: offers.offering ? Qt.rect((width - widest) / 2, capsule.y - offerRoom, widest, capsule.height + offerRoom)
                                                      : capsule
    // The capsule's shadow, which reaches past it.
    readonly property alias shadow: shadow

    // As big as the capsule, or as it is while it grows or shrinks to the
    // target, so a scene that sizes to the popup never cuts it off.
    implicitWidth: Math.max(holder.width, target.width)
    implicitHeight: Math.max(holder.height, target.height)

    // Only the live dictation's popup carries them, not a page's demo, and
    // not over a review (a follow-up's too), the tallest phase.
    OfferChips {
        id: offers
        width: popup.widest
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: holder.top
        anchors.bottomMargin: 10
        active: popup.source === Dictation && popup.shown && !popup.reviewShown
    }

    Item {
        id: holder
        objectName: "capsule"
        width: popup.target.width
        height: popup.target.height
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        Behavior on width { Ease { duration: Theme.page; curve: Theme.springy } }
        Behavior on height { Ease { duration: Theme.page; curve: Theme.springy } }

        // Arrives from below with a small overshoot, leaves quickly.
        opacity: popup.shown ? 1 : 0
        scale: popup.shown ? 1 : Theme.motionScale(0.86)
        transform: Translate { id: lift; y: popup.shown ? 0 : 18 * Theme.movement; Behavior on y { Ease { duration: popup.shown ? Theme.page : Theme.base; curve: popup.shown ? Theme.springy : Theme.easeIn } } }
        transformOrigin: Item.Bottom
        Behavior on opacity { Ease { duration: Theme.base } }
        Behavior on scale { Ease { duration: popup.shown ? Theme.page : Theme.base; curve: popup.shown ? Theme.springy : Theme.easeIn } }

        // An error's countdown waits while the pointer rests on it.
        HoverHandler {
            id: hover
            onHoveredChanged: popup.source.errorHoldRequested(hovered)
        }

        RectangularShadow {
            id: shadow
            anchors.fill: shell
            radius: shell.radius
            blur: 24
            spread: -4
            offset.y: 8
            color: Theme.shadow
        }
        // A pill while it is one bar; taller, it keeps the bar's round
        // corners, so it stays one shape as it grows.
        Rectangle {
            id: shell
            anchors.fill: parent
            radius: popup.barHeight / 2
            color: Theme.sheet
            border.width: 1
            border.color: Theme.borderStrong
            clip: true

            // Live: words above, the control bar below ----------------------
            Item {
                id: liveLayer
                width: parent.width
                // Under the edit during a follow-up.
                height: popup.followingUp ? popup.liveHeight : parent.height
                anchors.bottom: parent.bottom
                opacity: popup.live ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { Ease { duration: popup.live ? Theme.base : Theme.fast } }

                LiveWords {
                    id: words
                    x: popup.contentX
                    y: 12
                    width: parent.width - x - 18
                    text: popup.liveText
                    maxWords: Settings.values.previewWords || 12
                    shimmer: popup.phase === "refining" && popup.source.refined.length === 0
                    opacity: popup.showText ? 1 : 0
                    Behavior on opacity { Ease {} }
                }

                Item {
                    id: bar
                    width: parent.width
                    height: popup.barHeight
                    anchors.bottom: parent.bottom

                    // The ribbon takes the orb's place, so the orb shows nothing then.
                    VoiceOrb {
                        id: orb
                        objectName: "orb"
                        x: popup.orbX
                        size: popup.orbSize
                        anchors.verticalCenter: parent.verticalCenter
                        phase: popup.ribbonShown ? "" : popup.phase === "delivering" ? popup.workPhase : popup.phase
                        level: popup.source.level
                    }

                    Waveform {
                        id: levelBars
                        objectName: "levelBars"
                        x: popup.contentX
                        width: clock.x - x - 10
                        anchors.verticalCenter: parent.verticalCenter
                        height: 22
                        // From the width once the capsule has settled, so
                        // the bars aren't rebuilt each frame as it grows.
                        // Delayed, since this width and the capsule's
                        // change together.
                        Binding on bars {
                            value: Math.max(8, Math.floor((levelBars.width + popup.liveWidth - bar.width) / 5.5))
                            delayed: true
                        }
                        level: popup.source.level
                        active: popup.boring && popup.phase === "listening"
                        opacity: popup.boring && popup.voiceShown ? 1 : 0
                        visible: opacity > 0
                        Behavior on opacity { Ease {} }
                    }

                    // From the orb's place to the clock. Its figure keeps
                    // 6 px in from its ends, so it starts 14 px into the pill.
                    ScatterRibbon {
                        id: ribbon
                        objectName: "ribbon"
                        x: 8
                        width: clock.x - 6 - x
                        height: parent.height
                        level: popup.source.level
                        listening: popup.phase === "listening"
                        movement: Theme.movement
                        dark: Theme.dark
                        color: Theme.text
                        freedColor: Theme.accent
                        pausedColor: Theme.textTertiary
                        opacity: popup.ribbonShown ? 1 : 0
                        visible: opacity > 0
                        Behavior on opacity { Ease { duration: Theme.fast } }
                        FrameAnimation {
                            running: ribbon.visible && ribbon.moving
                            onTriggered: ribbon.advance(frameTime)
                        }
                    }

                    MorphText {
                        x: popup.contentX
                        anchors.verticalCenter: parent.verticalCenter
                        // While recording only a renewal shows here, and holds
                        // as it fades out.
                        text: popup.recording ? popup.t.renewing : popup.workPhase === "refining" ? popup.t.refining : popup.t.transcribing
                        variant: "label"
                        strong: true
                        opacity: popup.renewing || popup.phase === "transcribing" || popup.phase === "refining" || popup.phase === "delivering" ? 1 : 0
                        Behavior on opacity { Ease {} }
                    }

                    MorphText {
                        id: clock
                        anchors.right: buttons.left
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: Math.floor(popup.source.elapsed / 60) + ":" + String(popup.source.elapsed % 60).padStart(2, "0")
                        variant: "mono"
                        tone: popup.phase === "paused" ? "warning" : "secondary"
                        opacity: popup.phase === "listening" || popup.phase === "paused" ? 1 : 0
                        Behavior on opacity { Ease {} }
                    }

                    // Each button's centre on the round right end's.
                    Row {
                        id: buttons
                        anchors.right: parent.right
                        anchors.rightMargin: (popup.barHeight - 28) / 2
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        Reveal {
                            sideways: true
                            open: popup.phase === "listening" || popup.phase === "paused"
                            anchors.verticalCenter: parent.verticalCenter
                            CircleButton {
                                size: 28
                                iconName: popup.phase === "paused" ? "play" : "pause"
                                tooltip: popup.phase === "paused" ? Words.resume : Words.pause
                                onClicked: popup.source.pauseRequested()
                            }
                        }
                        // Delivering is past cancelling.
                        Reveal {
                            sideways: true
                            open: popup.phase !== "delivering"
                            anchors.verticalCenter: parent.verticalCenter
                            CircleButton {
                                size: 28
                                iconName: "x"
                                tooltip: Words.cancel
                                onClicked: popup.source.cancelRequested()
                            }
                        }
                    }
                }
            }

            // Delivered or failed: one line of outcome ----------------------
            Row {
                id: receipt
                x: popup.orbX
                anchors.verticalCenter: parent.verticalCenter
                spacing: popup.contentX - popup.orbX - popup.orbSize
                opacity: popup.phase === "delivered" || popup.phase === "error" ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { Ease { duration: Theme.slow } }

                VoiceOrb { id: receiptOrb; phase: popup.receiptMark; size: popup.orbSize; anchors.verticalCenter: parent.verticalCenter }
                Flickable {
                    id: receiptScroll
                    objectName: "noticeScroll"
                    // The words take what the widest capsule has left once the
                    // orb, the buttons and the spacing are in, and wrap past
                    // it, so a long error never pushes its buttons out. The
                    // capsule grows taller for them up to textMax, and they
                    // scroll past it.
                    readonly property real room: popup.widest - receipt.x - popup.receiptEnd - receiptOrb.width - receipt.spacing
                                                 - (receiptButtons.visible ? receiptButtons.implicitWidth + receipt.spacing : 0)
                    width: Math.min(Math.max(receiptTitle.implicitWidth, receiptDetail.implicitWidth), room)
                    height: Math.min(receiptText.implicitHeight, popup.textMax)
                    contentHeight: receiptText.implicitHeight
                    interactive: contentHeight > height
                    boundsBehavior: Flickable.StopAtBounds
                    // A mouse drags nothing on a desktop; touch still does.
                    acceptedButtons: Qt.NoButton
                    WheelScroll {}
                    clip: true
                    T.ScrollBar.vertical: ScrollBar {}
                    anchors.verticalCenter: parent.verticalCenter
                    // A new notice starts at its first line.
                    Connections {
                        target: popup
                        function onNoticeChanged() { receiptScroll.cancelFlick(); receiptScroll.contentY = 0 }
                    }
                    Column {
                        id: receiptText
                        width: parent.width
                        spacing: 2
                        Txt {
                            id: receiptTitle
                            width: parent.width
                            text: popup.notice.title || ""
                            variant: "label"
                            strong: true
                            wrapMode: Text.Wrap
                        }
                        Txt {
                            id: receiptDetail
                            width: parent.width
                            text: popup.notice.detail || ""
                            // No room kept for a detail there isn't.
                            visible: text.length > 0
                            variant: "small"
                            tone: "secondary"
                            wrapMode: Text.Wrap
                        }
                    }
                }
                Row {
                    id: receiptButtons
                    visible: popup.noticeFix || popup.noticePhase === "error"
                    spacing: 8
                    anchors.verticalCenter: parent.verticalCenter
                    Button {
                        id: receiptAction
                        visible: popup.noticeFix
                        text: visible ? popup.t.open(popup.notice.page) : ""
                        size: "small"
                        variant: "primary"
                        onClicked: popup.source.errorActionRequested()
                    }
                    Button {
                        id: receiptDismiss
                        visible: popup.noticePhase === "error"
                        text: popup.t.dismiss
                        size: "small"
                        onClicked: popup.source.cancelRequested()
                    }
                }
            }

            // An error's countdown to dismissing itself, draining under it
            // and waiting while the pointer rests on the capsule.
            Rectangle {
                id: countdown
                property real remaining: 1
                x: popup.contentX
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 4
                width: (parent.width - x - popup.barHeight / 2) * remaining
                height: 2
                radius: height / 2
                color: Theme.soft(Theme.negative, 0.55)
                opacity: popup.phase === "error" ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { Ease { duration: Theme.slow } }
                // Core's real dismiss time, so not a Theme duration.
                Ease on remaining {
                    id: drain
                    from: 1
                    to: 0
                    duration: popup.source.error.dismissMs || 0
                    curve: Theme.steady
                    running: popup.phase === "error"
                    paused: running && hover.hovered
                }
                // Another error over this one counts down from the start, as
                // Dictation's own countdown does.
                Connections {
                    target: popup.source
                    function onErrorChanged() { if (popup.phase === "error") drain.restart() }
                }
            }

            // Review: the edit to a selection, waiting for Replace -----------
            VoiceOrb {
                x: popup.orbX
                y: review.y - 2
                size: popup.orbSize
                phase: "review"
                opacity: popup.reviewShown ? popup.followingUp ? popup.followUpDim : 1 : 0
                visible: opacity > 0
                Behavior on opacity { Ease { duration: Theme.slow } }
            }
            Column {
                id: review
                x: popup.contentX
                y: 12
                width: parent.width - x - 16
                spacing: 10
                opacity: popup.reviewShown ? popup.followingUp ? popup.followUpDim : 1 : 0
                visible: opacity > 0
                enabled: !popup.followingUp
                Behavior on opacity { Ease { duration: Theme.slow } }

                Item {
                    id: reviewHeading
                    width: parent.width
                    // From its lines, not the column, which lays out only
                    // once the window shows; the window's size counts this.
                    height: reviewTitleLine.implicitHeight + reviewTitle.spacing + reviewSaid.implicitHeight
                    Column {
                        id: reviewTitle
                        width: parent.width - reviewSummary.width - 12
                        spacing: 2
                        Txt { id: reviewTitleLine; text: popup.t.reviewTitle; variant: "label"; strong: true }
                        Txt {
                            id: reviewSaid
                            width: parent.width
                            text: popup.t.youSaid(popup.selectionEdit.instruction || "")
                            variant: "small"
                            tone: "secondary"
                            elide: Text.ElideRight
                        }
                    }
                    // How much changed, and the whole edit or only its changes.
                    Column {
                        id: reviewSummary
                        anchors.right: parent.right
                        spacing: 2
                        Txt { anchors.right: parent.right; text: popup.selectionEdit.summary || ""; variant: "small"; tone: "secondary" }
                        TextLink {
                            anchors.right: parent.right
                            visible: popup.canFold
                            text: popup.wholeEdit ? popup.t.changesOnly : popup.t.wholeEdit
                            onActivated: popup.wholeEdit = !popup.wholeEdit
                        }
                    }
                }

                Flickable {
                    width: parent.width
                    height: Math.min(edit.implicitHeight, popup.textMax)
                    contentHeight: edit.implicitHeight
                    interactive: contentHeight > height
                    boundsBehavior: Flickable.StopAtBounds
                    // A mouse drags nothing on a desktop; touch still does.
                    acceptedButtons: Qt.NoButton
                    WheelScroll {}
                    clip: true
                    T.ScrollBar.vertical: ScrollBar {}
                    DiffText {
                        id: edit
                        segments: popup.canFold && !popup.wholeEdit ? popup.selectionEdit.folded : popup.selectionEdit.whole || []
                        variant: "transcript"
                    }
                }

                // Each button names its key, so the edit can be answered
                // without the pointer. One row, right-aligned, last button
                // first; the capsule widens for long shortcuts.
                Row {
                    id: reviewActions
                    visible: !popup.followingUp
                    // The review's width, not the capsule's now: the window's
                    // size counts this row's height, and must not change
                    // between phases.
                    width: popup.widest - review.x - 16
                    layoutDirection: Qt.RightToLeft
                    spacing: 8
                    Button {
                        id: replaceButton
                        text: popup.t.replace
                        size: "small"
                        variant: "primary"
                        trailing: KeyCombo { keys: popup.selectionEdit.replaceKeys || []; size: "small" }
                        onClicked: popup.source.replaceRequested()
                    }
                    Button {
                        id: askButton
                        text: popup.t.askForMore
                        size: "small"
                        trailing: KeyCombo { keys: popup.selectionEdit.followUpKeys || []; size: "small" }
                        onClicked: popup.source.followUpRequested()
                    }
                    Button {
                        id: cancelButton
                        text: Words.cancel
                        size: "small"
                        trailing: KeyCombo { keys: popup.selectionEdit.keepKeys || []; size: "small" }
                        onClicked: popup.source.cancelRequested()
                    }
                }
            }
        }
    }
}
