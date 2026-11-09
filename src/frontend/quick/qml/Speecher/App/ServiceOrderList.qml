pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// One job's Service Order, speech or refinement, first to last: each
// service with its status, a Fix that opens its Services card in a sheet
// while it can't run, Check again while no check has vouched for its
// sign-in, and under it its own choices (ServiceChoiceRow).
// Reordering, adding and removing save the order at once; a service always
// stays.
Column {
    id: list

    // "speech" or "refinement".
    required property string role
    // Each service's own choices under it; folded away, only the services
    // and their states show.
    property bool choicesShown: true
    readonly property var spec: Schema.row(role === "speech" ? "speechServices" : "refinementServices")
    readonly property var services: role === "speech" ? Models.speechServices : Models.refinementProviders

    function save(entries) { Settings.set(spec.id, entries) }
    // Saves one field of one service's entry, the rest as they are.
    function setField(id, field, value) {
        save(ModelText.entries(role).map(e => {
            if (e.service !== id) return e
            const changed = Object.assign({}, e)
            changed[field] = value
            return changed
        }))
    }

    width: parent ? parent.width : implicitWidth

    OrderedList {
        objectName: list.role + "ServiceOrder"
        ids: ModelText.order(list.role)
        // Each with its ratings, which Add service shows under its name.
        options: list.services.map(s => ({ id: s.id, label: s.label, icon: ModelText.serviceOf(list.role, s.id).icon,
                                           help: ModelText.ratingLine(list.role, s.id) }))
        statusOf: (id, index) => {
            const s = ModelText.orderStatus(ModelText.serviceOf(list.role, id), index)
            return { text: s.text, tone: States.tone(s.state) }
        }
        minCount: 1
        addLabel: list.spec.addLabel
        addHelp: list.spec.addHelp
        allAddedText: list.spec.allAddedText
        addPlaceholder: Words.choose
        moveUpText: Words.moveUp
        moveDownText: Words.moveDown
        removeText: Words.remove
        onEdited: ids => list.save(ids.map(id => ModelText.entry(list.role, id)))
        rowControl: Component {
            Row {
                id: actions
                required property string modelData
                readonly property var service: ModelText.serviceOf(list.role, modelData)
                readonly property bool unknown: ModelText.unchecked(service)
                spacing: Theme.space2
                Button {
                    objectName: "check_" + actions.modelData
                    visible: actions.unknown
                    text: Words.checkAgain
                    size: "small"
                    busy: actions.unknown && ModelText.checkingSignIn(list.role, actions.service)
                    onClicked: ModelText.checkSignIn(list.role, actions.modelData)
                }
                Button {
                    objectName: "fix_" + actions.modelData
                    text: Words.fix
                    size: "small"
                    visible: ModelText.hasFix(actions.service, actions.unknown)
                    onClicked: sheet.openFor(list.role, actions.modelData)
                }
            }
        }
        rowDetail: Component {
            Reveal {
                id: detail
                property string modelData
                property int place
                open: list.choicesShown
                ServiceChoiceRow {
                    role: list.role
                    modelData: detail.modelData
                    place: detail.place
                    onEdited: (field, value) => list.setField(detail.modelData, field, value)
                    onLanguagesRequested: (at, codes) => languages.show(at, codes)
                }
            }
        }
    }

    FixSheet { id: sheet }
    LanguageList { id: languages; objectName: "serviceLanguages" }
}
