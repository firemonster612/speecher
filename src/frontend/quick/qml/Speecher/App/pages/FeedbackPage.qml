import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Demo
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import Speecher.Popup

Page {
    id: page
    title: Schema.page("feedback").title
    subtitle: Schema.page("feedback").subtitle

    // The speech service and refinement provider in use, whose text the
    // live rows show; each row says when its service sends none.
    readonly property string speech: Settings.values.speechProvider || ""
    readonly property bool refines: (Settings.values.refinementProvider || "none") !== "none"
                                    || Library.profiles.some(p => !!p.refinement)

    // A small stage with the real popup on it, fed by a demo dictation that
    // loops, so each change below shows up straight away.
    Section {
        plain: true
        // As tall as the popup gets at the Live text length, with the same
        // room above it as below.
        Plate {
            id: stage
            implicitHeight: preview.tallestLive + 36
            height: implicitHeight
            Grow on implicitHeight {}
            clip: true

            DictationPopup {
                id: preview
                width: parent.width
                height: parent.height - 18
                source: demo
            }

            DictationState {
                id: demo
                elapsed: 4
                level: voice.level
                receipt: ({ title: "Sent to the app", detail: "", outcome: "inserted", page: "" })
            }
            DemoVoice { id: voice; running: demo.state === "listening" }

            readonly property var sentence: "Quick update on the launch, the onboarding flow is ready for review and I'd love feedback before Friday".split(" ")
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
                ScriptAction { script: demo.state = Settings.values.resultDuration === 0 ? "idle" : "delivered" }
                PauseAnimation { duration: Theme.ms(Settings.values.resultDuration) }
                ScriptAction { script: demo.state = "idle" }
                PauseAnimation { duration: Theme.ms(700) }
                ScriptAction { script: { stage.fed = 0; demo.words = ""; demo.elapsed = 0; demo.state = "listening" } }
            }
            Component.onCompleted: { demo.state = "listening" }
        }
    }

    Section {
        title: Schema.row("showLiveText").section
        SchemaRow {
            rowId: "showLiveText"
            note: ModelText.streams(page.speech) ? spec.help : spec.afterStopHelp.arg(ModelText.service(page.speech).label)
        }
        SchemaRow { rowId: "previewWords" }
        SchemaRow {
            rowId: "showRefinementText"
            note: page.refines ? spec.help : spec.noRefinerHelp
        }
        SchemaRow { rowId: "resultDuration" }
        SchemaRow {
            id: voiceDisplay
            rowId: "boringMode"
            SegmentedControl {
                options: voiceDisplay.spec.options
                currentId: Settings.values.boringMode ? "bars" : "ribbon"
                onActivated: id => Settings.set("boringMode", id === "bars")
            }
        }
    }

    Section {
        title: Schema.row("playSounds").section
        SchemaRow { rowId: "playSounds" }
        SchemaRow { rowId: "pauseMedia" }
    }
}
