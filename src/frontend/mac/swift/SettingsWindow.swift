import AppKit
import Combine
import SwiftUI

// The settings window: a full-height source-list sidebar and a detail column,
// with the window controls floating over the sidebar like System Settings.

struct RootView: View {
    @ObservedObject var model: AppModel
    @State private var query = ""

    var body: some View {
        NavigationSplitView {
            SidebarList(model: model, query: $query)
                // A settings sidebar's width on macOS. Left to itself the split
                // view picks one narrow enough to clip a pane name; an ideal
                // rather than a lock, because the row height and glyph size
                // here follow a setting the user can change.
                .navigationSplitViewColumnWidth(min: 175, ideal: 190)
        } detail: {
            detail
        }
        // On the split view rather than on a column: search covers the whole
        // window, and the sidebar is where a settings app puts the field.
        .searchable(text: $query, placement: .sidebar, prompt: "Search")
        .toolbar(removing: .sidebarToggle)
        .toolbar(removing: .title)
    }

    @ViewBuilder private var detail: some View {
        if let pane = Pane.with(id: model.pane) {
            VStack(alignment: .leading, spacing: 0) {
                if model.whatsNewPending {
                    GroupBox {
                        HStack {
                            Label("Speecher was updated.", systemImage: "sparkles")
                            Spacer()
                            Button("See what's new") { model.showWhatsNew() }
                            Button("Dismiss") { model.dismissWhatsNew() }
                        }
                    }
                    .scenePadding([.top, .horizontal])
                }
                Text(model.title(for: pane))
                    .font(.title2.weight(.semibold))
                    .scenePadding([.top, .horizontal])
                PaneView(pane: pane, model: model)
            }
        } else {
            ContentUnavailableView("No Pane Selected",
                                   systemImage: "sidebar.left",
                                   description: Text("Pick a pane in the sidebar."))
        }
    }
}

/// The source list: seven regular panes in runs, plus What's New while selected,
/// filtered by whatever the search field holds. The schema is the index, so a
/// pane answers to its own name and to any group heading, row label or help text
/// it carries.
struct SidebarList: View {
    @ObservedObject var model: AppModel
    @Binding var query: String

    var body: some View {
        List(selection: $model.pane) {
            if query.isEmpty {
                if model.pane == "whatsNew", let pane = Pane.with(id: model.pane) {
                    row(pane)
                }
                ForEach(Array(Pane.sidebarRuns.enumerated()), id: \.offset) { _, run in
                    Section {
                        ForEach(run.compactMap(Pane.with(id:))) { row($0) }
                    }
                }
            } else {
                // A search shows its hits as one flat list, not as the runs they
                // came from.
                ForEach(Pane.all.filter { model.pane($0, matches: query) }) { row($0) }
            }
        }
        .listStyle(.sidebar)
    }

    /// Icon plus label, and no colour of our own: sidebar icons take the accent
    /// colour the user chose, and a fixed one would override it.
    private func row(_ pane: Pane) -> some View {
        Label(model.title(for: pane), systemImage: model.symbol(for: pane)).tag(pane.id)
    }
}

/// The window itself, so the Objective-C++ front end never has to know what
/// SwiftUI view is inside it.
@MainActor
final class SpeecherSettingsWindow {
    private let model: AppModel
    private let window: NSWindow
    private var titleObserver: AnyCancellable?

    init(model: AppModel) {
        self.model = model
        // No miniaturize: a settings window is quick to reopen with ⌘, so it has
        // no business in the Dock. It remains resizable for the table panes.
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 720, height: 620),
                          styleMask: [.titled, .closable, .resizable, .fullSizeContentView],
                          backing: .buffered,
                          defer: false)
        // AppKit releases a closed window by default while this object keeps a
        // strong reference to it; the over-released window survives as an
        // invisible click-eating ghost. This window closes and reopens.
        window.isReleasedWhenClosed = false
        // The title remains useful to the window server, but the detail column
        // renders it. The sidebar material therefore continues behind the
        // traffic lights instead of stopping beneath a separate title band.
        window.titleVisibility = .hidden
        window.titlebarAppearsTransparent = true
        window.titlebarSeparatorStyle = .none
        window.toolbar = NSToolbar()
        window.toolbarStyle = .unified
        window.standardWindowButton(.zoomButton)?.isEnabled = false
        // A controller rather than a bare hosting view: NavigationSplitView
        // becomes an NSSplitViewController, which needs a parent view
        // controller to install its sidebar item into.
        window.contentViewController = NSHostingController(rootView: RootView(model: model))
        // The controller brings its own idea of how big it wants to be, which is
        // the minimum rather than the size this window was made at.
        window.setContentSize(NSSize(width: 720, height: 620))
        // Panes that hold a table are worth making taller, so the window can
        // grow past the size its content asks for. The floor goes on the frame
        // rather than the content, which the hosting controller owns.
        window.minSize = NSSize(width: 660, height: 560)
        window.center()
        // The first SwiftUI version used an oversized default. Keep future
        // resizing persistent without restoring that pre-release frame.
        window.setFrameAutosaveName("SpeecherSettingsV2")
        // The window title is the pane the user is looking at.
        window.title = Pane.with(id: model.pane).map(model.title(for:)) ?? "Settings"
        titleObserver = model.$pane.sink { [weak window, model] paneId in
            window?.title = Pane.with(id: paneId).map(model.title(for:)) ?? "Settings"
        }
    }

    func show() {
        window.makeKeyAndOrderFront(nil)
        // The device enumeration and the keyring read would both delay the first
        // frame, so they wait a turn of the run loop for it.
        DispatchQueue.main.async { [weak window, model] in
            // Tahoe ignores toolbar(removing: .sidebarToggle) when SwiftUI is
            // hosted in an AppKit-owned window, even though it still installs
            // the item. Remove that one stock item after toolbar installation.
            let toggle = NSToolbarItem.Identifier(
                "com.apple.SwiftUI.navigationSplitView.toggleSidebar")
            if let toolbar = window?.toolbar,
               let index = toolbar.items.firstIndex(where: { $0.itemIdentifier == toggle }) {
                toolbar.removeItem(at: index)
            }
            model.loadDeferredRows()
        }
    }

    func close() {
        window.close()
    }

    // Called from the front end on the main thread, which is where the window
    // has to be touched.
    //
    // This is the window's backing store, so nothing the compositor draws for
    // the window comes out: vibrancy materials are blank, and the whole sidebar
    // column, which SwiftUI puts inside a glass container, is missing. The
    // detail column and the titlebar are real. SwiftUI's ImageRenderer is not an
    // alternative: it refuses NavigationSplitView outright. For a composited
    // shot, screencapture with Screen Recording granted is the way.
    func capture(toPath path: String) -> Bool {
        guard let content = window.contentView,
              let view = content.superview ?? window.contentView,
              let bitmap = view.bitmapImageRepForCachingDisplay(in: view.bounds) else {
            return false
        }
        view.cacheDisplay(in: view.bounds, to: bitmap)
        guard let png = bitmap.representation(using: .png, properties: [:]) else {
            return false
        }
        return (try? png.write(to: URL(fileURLWithPath: path))) != nil
    }
}
