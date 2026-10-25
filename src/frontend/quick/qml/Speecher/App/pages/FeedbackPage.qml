import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Demo
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import Speecher.Popup

Page {
    title: Schema.page("feedback").title
    subtitle: Schema.page("feedback").subtitle

    // A small stage with the real popup on it, fed by a demo dictation that
    // loops, so each change below shows up straight away.
    Section {
        plain: true
        Plate {
            id: stage
            height: 172
            clip: true

            DictationPopup {
                width: parent.width
                height: parent.height - 18
                source: demo
            }

            DictationState {
                id: demo
                elapsed: 4
                level: voice.level
                receipt: Dictation.sample.receipt
            }
            DemoVoice { id: voice; running: demo.state === "listening" }

            readonly property var sentence: Dictation.sample.words.split(" ")
            property int fed: 0
            Timer {
                interval: Theme.ms(260)
                repeat: true
                running: demo.state === "listening"
                onTriggered: {
                    if (stage.fed >= stage.sentence.length) { stop(); stage.finish(); return }
                    demo.words = stage.sentence.slice(0, ++stage.fed).join(" ")
                    if (stage.fed % 4 === 0) demo.elapsed += 1
                }
            }
            function finish() { loop.restart() }
            SequentialAnimation {
                id: loop
                ScriptAction { script: demo.state = "refining" }
                PauseAnimation { duration: Theme.ms(1600) }
                ScriptAction { script: demo.state = Settings.values.resultDuration === "0" ? "idle" : "delivered" }
                PauseAnimation { duration: Theme.ms(2200) }
                ScriptAction { script: demo.state = "idle" }
                PauseAnimation { duration: Theme.ms(700) }
                ScriptAction { script: { stage.fed = 0; demo.words = ""; demo.elapsed = 0; demo.state = "listening" } }
            }
            Component.onCompleted: { demo.state = "listening" }
        }
    }

    Section {
        title: Schema.row("showLiveText").section
        SchemaRow { rowId: "showLiveText" }
        SchemaRow { rowId: "previewWords" }
        SchemaRow { rowId: "showRefinementText" }
        SchemaRow { rowId: "resultDuration" }
        SchemaRow { rowId: "reviewSelectionEdits" }
    }

    Section {
        title: Schema.row("playSounds").section
        SchemaRow { rowId: "playSounds" }
        SchemaRow { rowId: "pauseMedia" }
    }
}
