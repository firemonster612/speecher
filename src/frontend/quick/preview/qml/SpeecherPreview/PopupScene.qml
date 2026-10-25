import QtQuick
import QtQuick.Window
import Speecher.Launch
import Speecher.Theme
import Speecher.Demo
import Speecher.Backend
import Speecher.Popup

// A desktop with a chat app and the dictation popup over it. With
// --popup-demo=story (the default) it plays one dictation from start to
// paste; any other value (listening, paused, refining, delivered, review,
// error) holds that state for a screenshot.
Window {
    id: scene
    width: 1200
    height: 760
    visible: true
    title: "Popup preview"

    readonly property string demo: Launch.arg("popup-demo", "story")

    // A plain desktop, so the eye goes to the popup.
    color: Theme.dark ? "#2B302E" : "#C9CFC6"

    FakeChatApp {
        id: chat
        x: 120; y: 50
        width: scene.width - 240
        height: scene.height - 190
    }

    DictationPopup {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 26
    }

    // Story -----------------------------------------------------------------
    readonly property string first: "Hey team, quick update on the launch. The new onboarding flow is ready for review and I'd love some feedback before Friday."
    readonly property string second: "Also let's sync tomorrow at ten to go over the last few bugs."
    readonly property string polished: "Hey team, quick update on the launch: the new onboarding flow is ready for review, and I'd love feedback before Friday. Let's also sync tomorrow at 10 to go over the last few bugs."

    property var _queue: []
    property string _feedTarget: "words"
    function feed(text, target, interval) {
        _queue = text.split(" ")
        _feedTarget = target
        feeder.interval = Theme.ms(interval)
        feeder.start()
    }
    Timer {
        id: feeder
        repeat: true
        onTriggered: {
            if (!scene._queue.length) { stop(); return }
            const next = scene._queue[0]
            scene._queue = scene._queue.slice(1)
            const cur = Dictation[scene._feedTarget]
            Dictation[scene._feedTarget] = cur.length ? cur + " " + next : next
        }
    }
    DemoVoice {
        running: Dictation.state === "listening"
        onLevelChanged: Dictation.level = level
    }
    Timer {
        interval: Theme.ms(1000)
        repeat: true
        running: Dictation.state === "listening"
        onTriggered: Dictation.elapsed += 1
    }

    SequentialAnimation {
        id: story
        // Plays forever, so the scene can be left open and watched.
        loops: Animation.Infinite
        PauseAnimation { duration: Theme.ms(700) }
        ScriptAction { script: { chat.composerText = ""; Dictation.words = ""; Dictation.elapsed = 0; Dictation.state = "listening"; scene.feed(scene.first, "words", 230) } }
        PauseAnimation { duration: Theme.ms(5600) }
        ScriptAction { script: { Dictation.state = "paused"; Dictation.level = 0 } }
        PauseAnimation { duration: Theme.ms(1300) }
        ScriptAction { script: { Dictation.state = "listening"; scene.feed(scene.second, "words", 230) } }
        PauseAnimation { duration: Theme.ms(3300) }
        ScriptAction { script: { Dictation.state = "transcribing"; Dictation.level = 0 } }
        PauseAnimation { duration: Theme.ms(1300) }
        ScriptAction { script: { Dictation.refined = ""; Dictation.state = "refining" } }
        PauseAnimation { duration: Theme.ms(900) }
        ScriptAction { script: scene.feed(scene.polished, "refined", 55) }
        PauseAnimation { duration: Theme.ms(2300) }
        ScriptAction {
            script: {
                Dictation.receipt = Dictation.deliveredReceipt
                Dictation.state = "delivered"
                chat.composerText = scene.polished
                chat.flash = true
            }
        }
        PauseAnimation { duration: Theme.ms(900) }
        ScriptAction { script: chat.flash = false }
        PauseAnimation { duration: Theme.ms(2200) }
        ScriptAction { script: Dictation.state = "idle" }
        PauseAnimation { duration: Theme.ms(1200) }
    }

    Component.onCompleted: {
        switch (demo) {
        case "story": story.start(); break
        case "listening":
        case "paused":
            Dictation.words = first + " " + second
            Dictation.elapsed = 9
            Dictation.level = 0.6
            Dictation.state = demo
            break
        case "refining":
            Dictation.words = first + " " + second
            Dictation.refined = polished.split(" ").slice(0, 22).join(" ")
            Dictation.state = "refining"
            break
        case "delivered":
            chat.composerText = polished
            Dictation.receipt = Dictation.deliveredReceipt
            Dictation.state = "delivered"
            break
        case "review":
            chat.composerText = "Can you send me the deck when you get a chance? I need it for the meeting."
            chat.selecting = true
            Dictation.review = {
                instruction: "make it more casual",
                segments: [
                    { kind: "removed", text: "Can you send me the deck when you get a chance?" },
                    { kind: "same", text: " " },
                    { kind: "added", text: "Mind sending the deck over when you can?" },
                    { kind: "same", text: " I need it for the " },
                    { kind: "removed", text: "meeting." },
                    { kind: "same", text: " " },
                    { kind: "added", text: "call tomorrow." }
                ]
            }
            Dictation.state = "review"
            break
        case "error":
            Dictation.error = Dictation.sampleError
            Dictation.state = "error"
            break
        }
    }
}
