pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "speech"

Page {
    id: page
    objectName: "speechPage"
    title: Schema.page("speech").title
    subtitle: Schema.page("speech").subtitle

    readonly property string primary: Settings.values.speechProvider || ""
    readonly property var fallbacks: Settings.values.speechFallbackList || []
    readonly property var fallbackSpec: Schema.row("speechFallbackList")
    readonly property var modelsSpec: Schema.row("speechModels")
    readonly property string primaryLabel: ModelText.service(primary).label

    Component.onCompleted: Models.checkComputer()

    // Whether a service is the primary or one of its fallbacks, so its
    // settings matter.
    function uses(id) { return id === primary || fallbacks.indexOf(id) >= 0 }
    // Where a service stands in the chain, for its section's caption.
    function role(id) { return ModelText.roleText(id === primary ? 0 : fallbacks.indexOf(id) + 1) }
    // A new primary leaves the fallback list.
    function choose(id) {
        Settings.set("speechProvider", id)
        Settings.set("speechFallbackList", fallbacks.filter(f => f !== id))
    }
    // The models behind the service; a Local Model lists the rated catalog,
    // suggestion first.
    readonly property var serviceModels: {
        if (primary !== "local") return ModelText.service(primary).models
        const rated = m => ({ name: m.name, note: m.id === Models.suggestedModel ? ModelText.suggested : "", bars: ModelText.bars(m) })
        return Models.catalog.filter(m => m.id === Models.suggestedModel)
            .concat(Models.catalog.filter(m => m.id !== Models.suggestedModel)).map(rated)
    }

    Section {
        id: serviceSection
        title: Schema.row("speechProvider").label
        caption: Schema.row("speechProvider").help
        SearchLanding { rowIds: ["speechProvider", "speechRating"]; target: serviceSection }
        ServiceCards {
            options: States.withTones(ModelText.speechChoices(Models.speechServices))
            currentId: page.primary
            unratedText: ModelText.unrated
            onActivated: id => page.choose(id)
            onHelpLinkActivated: (id, at) => serviceLanguages.show(at, ModelText.service(id).languages)
        }
        LanguageList { id: serviceLanguages; objectName: "serviceLanguages" }
    }

    Section {
        id: fallbackSection
        title: page.fallbackSpec.headingText.arg(page.primaryLabel)
        caption: page.fallbackSpec.help
        SearchLanding { rowIds: ["speechFallbackList"]; target: fallbackSection }
        FallbackList {
            ids: page.fallbacks
            options: Models.speechServices.filter(s => s.id !== page.primary).map(s => ({ id: s.id, label: s.label }))
            statusOf: (id, index) => {
                const s = ModelText.fallbackStatus(ModelText.service(id), index, page.primaryLabel)
                return { text: s.text, tone: States.tone(s.state) }
            }
            maxCount: page.fallbackSpec.maxCount
            emptyText: page.fallbackSpec.emptyText.arg(page.primaryLabel)
            addLabel: page.fallbackSpec.addLabel
            addHelp: page.fallbackSpec.addHelp
            addPlaceholder: Words.choose
            moveUpText: Words.moveUp
            moveDownText: Words.moveDown
            removeText: Words.remove
            onEdited: ids => Settings.set("speechFallbackList", ids)
        }
    }

    Section {
        title: Schema.row("spokenLanguage").section
        SchemaRow { rowId: "spokenLanguage" }
        Reveal {
            id: languageProblem
            readonly property string text: Models.spokenLanguageProblem
            open: text.length > 0
            Notice { inset: Theme.rowInset; text: languageProblem.text }
        }
        SchemaRow { rowId: "codexFinalRetranscribe"; shown: page.uses("codex") }
    }

    Reveal {
        open: page.uses("local")
        Section {
            title: Schema.row("speechLocalModel").section
            caption: page.role("local") + ModelText.ratedAsText(Models.speechRatings.local)
            SchemaRow {
                id: localRow
                readonly property bool none: Models.downloaded.length === 0
                rowId: "speechLocalModel"
                help: none ? localRow.spec.emptyHelp : localRow.spec.help
                Switcher {
                    width: Theme.selectWidth
                    currentIndex: localRow.none ? 1 : 0
                    Select {
                        placeholder: Words.choose
                        options: Models.downloaded.map(id => ModelText.catalogModel(id))
                                                  .map(m => ({ id: m.id, label: m.name, help: m.bestFor }))
                        currentId: Models.localModelChoice
                        onChosen: id => Settings.set("speechLocalModel", id)
                    }
                    Item {
                        implicitHeight: openModels.implicitHeight
                        Button {
                            id: openModels
                            anchors.right: parent.right
                            text: localRow.spec.actionLabel
                            variant: "primary"
                            onClicked: Navigation.go("models")
                        }
                    }
                }
            }
        }
    }

    Reveal {
        open: page.uses("endpoint")
        Section {
            title: Schema.row("speechEndpointUrl").section
            caption: page.role("endpoint")
            SpeechEndpointRows {}
        }
    }

    Reveal {
        open: page.serviceModels.length > 0
        Section {
            title: page.modelsSpec.section
            Disclosure {
                id: models
                title: page.modelsSpec.label
                help: page.primary === "local" ? page.modelsSpec.localHelp : page.modelsSpec.help.arg(page.primaryLabel)
                trailingText: String(page.serviceModels.length)
                SearchLanding {
                    rowIds: ["speechModels"]
                    target: models
                    onLanded: { models.open = true; models.highlight() }
                }
                RowList {
                    id: modelRows
                    values: page.serviceModels
                    keyOf: m => m.name
                    delegate: ListRow {
                        id: modelRow
                        required property string key
                        required property int index
                        readonly property var entry: modelRows.entry(key)
                        first: index === 0 && modelRows.first
                        title: entry.name
                        detail: entry.note || ""
                        detailLines: 3
                        trailing: RatingBars { bars: modelRow.entry.bars || [] }
                    }
                }
                Item { width: 1; height: Theme.space2 }
            }
        }
    }

    SubpageRows { pageId: "speech" }
}
