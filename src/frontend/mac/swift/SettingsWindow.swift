import AppKit
import Combine
import SwiftUI

// The settings window: sidebar and detail, System Settings' idiom. A visible
// title bar with a stock toolbar is what gives it the settings-window titlebar
// height, and it is also what keeps every interactive view out of the band
// where macOS 26's glass container swallows clicks.

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
                .navigationSplitViewColumnWidth(min: 200, ideal: 215)
        } detail: {
            detail
        }
        // On the split view rather than on a column: search covers the whole
        // window, and the sidebar is where a settings app puts the field.
        .searchable(text: $query, placement: .sidebar, prompt: "Search")
    }

    @ViewBuilder private var detail: some View {
        if let pane = Pane.with(id: model.pane) {
            PaneView(pane: pane, model: model)
                .navigationTitle(pane.title)
        } else {
            ContentUnavailableView("No Pane Selected",
                                   systemImage: "sidebar.left",
                                   description: Text("Pick a pane in the sidebar."))
        }
    }
}

/// The source list: the eight panes in runs, filtered by whatever the search
/// field holds. The schema is the index, so a pane answers to its own name and
/// to any group heading, row label or help text it carries.
struct SidebarList: View {
    @ObservedObject var model: AppModel
    @Binding var query: String

    var body: some View {
        List(selection: $model.pane) {
            if query.isEmpty {
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
        Label(pane.title, systemImage: pane.symbol).tag(pane.id)
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
        // no business in the Dock, and it sizes itself to the pane on show.
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 780, height: 620),
                          styleMask: [.titled, .closable, .resizable],
                          backing: .buffered,
                          defer: false)
        // Empty on purpose: the toolbar is what puts the pane title and the
        // sidebar's search field where macOS puts them. A settings window's
        // toolbar is for moving between panes and nothing else, so it carries
        // no buttons of its own.
        window.toolbar = NSToolbar()
        window.toolbarStyle = .unified
        window.standardWindowButton(.zoomButton)?.isEnabled = false
        // A controller rather than a bare hosting view: NavigationSplitView
        // becomes an NSSplitViewController, which needs a parent view
        // controller to install its sidebar item into.
        window.contentViewController = NSHostingController(rootView: RootView(model: model))
        // The controller brings its own idea of how big it wants to be, which is
        // the minimum rather than the size this window was made at.
        window.setContentSize(NSSize(width: 780, height: 620))
        // Panes that hold a table are worth making taller, so the window can
        // grow past the size its content asks for. The floor goes on the frame
        // rather than the content, which the hosting controller owns.
        window.minSize = NSSize(width: 680, height: 520)
        window.center()
        window.setFrameAutosaveName("SpeecherSettings")
        // The window title is the pane the user is looking at.
        window.title = Pane.with(id: model.pane)?.title ?? "Settings"
        titleObserver = model.$pane.sink { [weak window] pane in
            window?.title = Pane.with(id: pane)?.title ?? "Settings"
        }
    }

    func show() {
        window.makeKeyAndOrderFront(nil)
        // The device enumeration and the keyring read would both delay the first
        // frame, so they wait a turn of the run loop for it.
        DispatchQueue.main.async { [model] in
            model.loadDeferredRows()
        }
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
