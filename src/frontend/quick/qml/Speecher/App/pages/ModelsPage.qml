pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.App
import "speech"

Page {
    id: page
    title: Schema.page("models").title
    subtitle: Models.text.ratedFor.arg(Models.computer)

    readonly property var library: Schema.row("localModelBrowser")
    // The library, or only what is on this computer (or on its way).
    property string show: "all"
    // The model in use comes first, then the suggestion, then the catalog.
    readonly property var listed: {
        const rank = m => m.id === Models.inUse ? 0 : m.id === Models.suggestedModel ? 1 : 2
        const onComputer = m => Models.downloaded.indexOf(m.id) >= 0 || Models.downloads[m.id] !== undefined
        const catalog = Models.catalog
        return catalog.filter(m => show === "all" || onComputer(m))
                      .sort((a, b) => rank(a) - rank(b) || catalog.indexOf(a) - catalog.indexOf(b))
    }

    Section {
        id: librarySection
        title: page.library.label
        caption: page.library.help
        plain: true
        SearchLanding { rowIds: ["localModelBrowser"]; target: librarySection }
        actions: [
            SegmentedControl {
                id: filter
                objectName: "modelFilter"
                options: Models.libraryFilters
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
                entry: Models.catalogModel(key)
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
            text: Models.text.compareNote
            variant: "small"
            tone: "tertiary"
            wrapMode: Text.WordWrap
        }
    }

    Section {
        title: Schema.row("localIdleUnload").section
        SchemaRow { rowId: "localIdleUnload" }
        SchemaRow {
            id: acceleration
            rowId: "localAcceleration"
            help: acceleration.spec.help + " " + acceleration.spec.runningHelp.arg(Models.runningOn)
        }
        SchemaRow {
            rowId: "localGraphicsCard"
            shown: Models.graphicsCards.length > 1
            Select {
                placeholder: Words.choose
                options: Models.graphicsCards
                currentId: Settings.values.localGraphicsCard
                onChosen: id => Settings.set("localGraphicsCard", id)
            }
        }
        SchemaRow {
            id: folder
            rowId: "localModelFolder"
            help: Models.modelFolder
            Button {
                text: folder.spec.actionLabel
                iconName: "folder-open"
                onClicked: Models.openModelFolder()
            }
        }
    }
}
