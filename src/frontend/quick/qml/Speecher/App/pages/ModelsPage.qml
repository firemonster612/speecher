pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "speech"
import "services"

// This computer, under Services: the speech models it can download and
// run, the Local Runner cleanup runs on, and how the speech models use its
// memory, graphics card and disk.
Page {
    id: page
    title: Schema.page("models").title
    subtitle: Schema.page("models").subtitle

    Component.onCompleted: Models.checkComputer()

    readonly property var library: Schema.row("localModelBrowser")
    // The library, or only what is on this computer (or on its way).
    property string show: "all"
    // The model in use comes first, then the suggestion, then the catalog.
    // Until there is a suggestion for this computer, the catalog's own
    // ratings order it, Recommended first.
    readonly property var listed: {
        const rated = ["recommended", "good", "situational", "notRecommended"]
        const rank = m => m.id === Models.inUse ? 0 : m.id === Models.suggestedModel ? 1
                        : Models.suggestedModel ? 2 : 2 + rated.indexOf(m.rating)
        const onComputer = m => Models.downloaded.indexOf(m.id) >= 0 || Models.downloads[m.id] !== undefined
        const catalog = Models.catalog
        return catalog.filter(m => show === "all" || onComputer(m))
                      .sort((a, b) => rank(a) - rank(b) || catalog.indexOf(a) - catalog.indexOf(b))
    }

    // Without the Local Model service, nothing here could run: say so
    // instead of offering downloads, and where to get a build that has it.
    Section {
        visible: !ModelText.localSpeech
        title: page.library.label
        EmptyState {
            iconName: "cpu"
            title: page.library.notInBuild
            text: page.library.notInBuildHelp
            Button { text: page.library.getBuild; trailingIcon: "external-link"; onClicked: Updates.openAllReleases() }
        }
    }

    Section {
        id: librarySection
        visible: ModelText.localSpeech
        title: page.library.label
        // What the ratings, speeds and fits are measured against.
        caption: ModelText.computer ? ModelText.ratedFor.arg(ModelText.computer) : ModelText.ratedOnceChecked
        plain: true
        SearchLanding { rowIds: ["localModelBrowser", "speechLocalModel"]; target: librarySection }
        actions: [
            SegmentedControl {
                id: filter
                objectName: "modelFilter"
                options: ModelText.libraryFilters
                currentId: page.show
                onActivated: id => page.show = id
            }
        ]

        // Cards fold in and out as the filter changes and glide when the
        // order does.
        RowList {
            values: page.listed
            spacing: Theme.space2
            delegate: ModelCard {
                required property string key
                // From the catalog, which still has a card the filter is folding away.
                entry: ModelText.catalogModel(key)
                expanded: key === (Models.inUse || Models.suggestedModel)
            }
        }
        Reveal {
            open: page.listed.length === 0
            EmptyState {
                iconName: "hard-drive-download"
                title: Schema.row("speechLocalModel").emptyHelp
            }
        }
        Txt {
            width: parent.width
            topPadding: Theme.space2
            text: ModelText.compareNote
            variant: "small"
            tone: "tertiary"
            wrapMode: Text.WordWrap
        }
    }

    Section {
        objectName: "localCleanup"
        title: Schema.row("localRunner").section
        LocalRunnerRows {}
    }

    Section {
        visible: ModelText.localSpeech
        title: Schema.row("localIdleUnload").section
        SchemaRow { rowId: "localIdleUnload" }
        SchemaRow {
            id: acceleration
            rowId: "localAcceleration"
            help: acceleration.spec.help + (Models.runningOn ? " " + acceleration.spec.runningHelp.arg(Models.runningOn) : "")
            Select {
                placeholder: Words.choose
                options: ModelText.accelerationOptions
                currentId: Settings.values.localAcceleration || ""
                onChosen: id => Settings.set("localAcceleration", id)
            }
        }
        // Worth asking only when the backend reaches more than one card, or
        // to show the card an older Automatic choice is pinned to.
        SchemaRow {
            rowId: "localGraphicsCard"
            shown: Models.graphicsCards.length > 1
                   || (Settings.values.localAcceleration === "auto" && !!Settings.values.localGraphicsCard)
            Select {
                placeholder: Words.choose
                options: ModelText.graphicsCards
                currentId: Settings.values.localGraphicsCard || ""
                onChosen: id => Settings.set("localGraphicsCard", id)
            }
        }
        SchemaRow {
            id: folder
            rowId: "localModelFolder"
            help: ModelText.modelFolder
            Button {
                text: folder.spec.actionLabel
                iconName: "folder-open"
                size: "small"
                onClicked: Models.openModelFolder()
            }
        }
    }
}
