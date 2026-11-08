import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import "HistoryWords.js" as T

// History's filters beside its search: the kind, the app, the profile and
// when, each "any" until chosen. The apps and profiles are those History
// holds.
Flow {
    id: root
    property string kind: "all"
    property string app: ""
    property string profile: ""
    property string when: "any"
    // Narrower than a settings row's, so the four fit on one line.
    readonly property int selectWidth: 170
    readonly property bool active: kind !== "all" || app !== "" || profile !== "" || when !== "any"

    function reset() {
        kind = "all"
        app = ""
        profile = ""
        when = "any"
    }

    spacing: Theme.space2
    SegmentedControl {
        objectName: "historyKind"
        options: T.text.kinds
        currentId: root.kind
        onActivated: id => root.kind = id
    }
    Select {
        objectName: "historyApp"
        width: root.selectWidth
        options: [{ id: "", label: T.text.anyApp }].concat(History.apps.map(name => ({ id: name, label: name })))
        currentId: root.app
        onChosen: id => root.app = id
    }
    Select {
        objectName: "historyProfile"
        width: root.selectWidth
        options: [{ id: "", label: T.text.anyProfile }].concat(History.profiles.map(p => ({ id: p.id, label: p.name })))
        currentId: root.profile
        onChosen: id => root.profile = id
    }
    Select {
        objectName: "historyWhen"
        width: root.selectWidth
        options: T.text.whens
        currentId: root.when
        onChosen: id => root.when = id
    }
}
