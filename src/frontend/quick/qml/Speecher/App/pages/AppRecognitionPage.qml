pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Controls
import Speecher.Wording
import Speecher.App
import "text"

// Application recognition on a page of its own, since paste rules and
// writing profiles both go by the app type and profile it sets.
Page {
    id: page
    title: Schema.page("appRecognition").title
    subtitle: Schema.page("appRecognition").subtitle

    AppRecognitionRules { onRevealRequested: item => page.reveal(item) }
}
