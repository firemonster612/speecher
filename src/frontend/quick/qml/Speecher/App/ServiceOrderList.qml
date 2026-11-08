pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// One job's Service Order, speech or refinement, first to last: each
// service with its status, a Fix that opens its Services card in a sheet
// while it can't run, and under it its own choices (ServiceChoiceRow).
// Reordering, adding and removing save the order at once; a service always
// stays.
Column {
    id: list

    // "speech" or "refinement".
    required property string role
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
        options: list.services.map(s => ({ id: s.id, label: s.label, icon: ModelText.serviceOf(list.role, s.id).icon }))
        statusOf: (id, index) => {
            const s = ModelText.orderStatus(ModelText.serviceOf(list.role, id), index)
            return { text: s.text, tone: States.tone(s.state) }
        }
        minCount: 1
        maxCount: list.spec.maxCount
        addLabel: list.spec.addLabel
        addHelp: list.spec.addHelp
        addPlaceholder: Words.choose
        moveUpText: Words.moveUp
        moveDownText: Words.moveDown
        removeText: Words.remove
        onEdited: ids => list.save(ids.map(id => ModelText.entry(list.role, id)))
        rowControl: Component {
            Button {
                id: fixButton
                required property string modelData
                readonly property var service: ModelText.serviceOf(list.role, modelData)
                objectName: "fix_" + modelData
                text: Words.fix
                size: "small"
                visible: ModelText.fixable(service) && ModelText.cardOf(modelData) !== ""
                onClicked: sheet.openFor(list.role, fixButton.modelData)
            }
        }
        rowDetail: Component {
            ServiceChoiceRow {
                role: list.role
                onEdited: (field, value) => list.setField(modelData, field, value)
                onLanguagesRequested: (at, codes) => languages.show(at, codes)
            }
        }
    }

    FixSheet { id: sheet }
    LanguageList { id: languages; objectName: "serviceLanguages" }
}
