pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// What one service in an order runs with, under its name: its model, chosen
// where the service lets you (ServiceModelField), else named; a later
// speech service's Spoken Language; ChatGPT's and Claude's thinking and
// speed. edited asks for one field of the entry to change; a Local Model is
// chosen through its own row, which marks it chosen.
Flow {
    id: choice

    property string role
    // The service, and its place in the order, set by OrderedList.
    property string modelData
    property int place
    signal edited(string field, string value)
    // The count of languages a speech service listens for was followed, at
    // the link, for the list of them.
    signal languagesRequested(Item at, var codes)

    readonly property var entry: ModelText.entry(role, modelData)
    readonly property bool speech: role === "speech"
    readonly property var service: ModelText.serviceOf(role, modelData)
    readonly property var speechSpec: Schema.row("speechServices")
    readonly property var refineSpec: Schema.row("refinementServices")
    // ChatGPT and Claude refinement, which think and run at a speed.
    readonly property bool tuned: !speech && (modelData === "openai" || modelData === "anthropic")
    // The Local Model speech runs, which choosing marks chosen.
    readonly property bool localSpeech: speech && modelData === "local"

    width: parent ? parent.width : implicitWidth
    spacing: Theme.space3

    // A label and its control, side by side.
    component Labelled: Row {
        id: pair
        property string label
        default property alias control: holder.data
        spacing: Theme.space2
        Txt { text: pair.label; variant: "small"; tone: "secondary"; anchors.verticalCenter: parent.verticalCenter }
        Item {
            id: holder
            width: childrenRect.width
            height: childrenRect.height
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // How many languages a speech service listens for, linking to them.
    LinkedText {
        visible: choice.speech && (choice.service.languages || []).length > 0
        objectName: "languages_" + choice.modelData
        plainText: visible ? ModelText.languageCount(choice.service.languages) : ""
        link: visible ? ModelText.languageLink(choice.service.languages) : ""
        variant: "small"
        tone: "secondary"
        onActivated: choice.languagesRequested(this, choice.service.languages)
    }
    // Claude Voice and ChatGPT dictation name their model; nothing chooses it.
    Txt {
        visible: choice.speech && choice.modelData !== "local" && choice.modelData !== "endpoint"
                 && (choice.service.models || []).length > 0
        text: visible ? choice.service.models.map(m => m.name).join(", ") : ""
        variant: "small"
        tone: "tertiary"
    }
    Labelled {
        visible: ModelText.choosesModel(choice.role, choice.modelData)
        label: choice.refineSpec.modelLabel
        ServiceModelField {
            objectName: "serviceModel"
            role: choice.role
            service: choice.modelData
            value: choice.localSpeech ? Models.localModelChoice : choice.entry.model || ""
            onChosen: model => {
                if (choice.localSpeech) Settings.set("speechLocalModel", model)
                else choice.edited("model", model)
            }
        }
    }
    Labelled {
        visible: choice.tuned
        label: choice.refineSpec.effortLabel
        Select {
            width: 130
            options: choice.tuned ? choice.refineSpec.effortOptions[choice.modelData] : []
            currentId: choice.entry.effort || ""
            onChosen: id => choice.edited("effort", id)
        }
    }
    Labelled {
        visible: choice.tuned
        label: choice.refineSpec.speedLabel
        Select {
            width: 130
            options: choice.tuned ? choice.refineSpec.speedOptions[choice.modelData] : []
            currentId: choice.entry.speed || ""
            onChosen: id => choice.edited("speed", id)
        }
    }
    // A later speech service listens for its own Spoken Language, or, with
    // none chosen ("Same as Spoken language", id ""), the one core resolves.
    Labelled {
        visible: choice.speech && choice.place > 0
        label: choice.speechSpec.listensFor
        LanguageSelect {
            objectName: "serviceLanguage_" + choice.modelData
            placeholder: Words.choose; searchText: Words.searchLanguages; clearText: Words.clear; emptyText: Words.noLanguage
            // Only a speech service has languages to offer.
            options: choice.speech ? [{ id: "", label: choice.speechSpec.sameAsMain, icon: "link" }]
                                     .concat(ModelText.spokenLanguageOptions(choice.service)) : []
            currentId: choice.entry.language || ""
            shownId: choice.service.spokenLanguage || ""
            onChosen: id => choice.edited("language", id)
        }
    }
    Reveal {
        width: parent.width
        open: choice.modelData === "openai" && choice.entry.speed === "ultrafast"
        Notice {
            indicator: "icon"
            variant: "small"
            text: Models.supportsUltrafast(choice.entry.model || "") ? choice.refineSpec.ultrafastPlans : choice.refineSpec.ultrafastModels
        }
    }
}
