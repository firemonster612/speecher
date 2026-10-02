import AppKit
import Combine
import SwiftUI

// The settings window: a full-height source-list sidebar and a detail column,
// with the window controls floating over the sidebar like System Settings.

struct RootView: View {
    @ObservedObject var model: AppModel
    @State private var query = ""
    /// The hit picked in the current search. A search starts with none, so
    /// picking the pane already open still reaches the row that matched.
    @State private var searchPick: String?

    var body: some View {
        NavigationSplitView {
            SidebarList(model: model, query: $query, searchPick: $searchPick)
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
        // Return opens the first hit at the row that matched.
        .onSubmit(of: .search) {
            guard let first = model.search(query).first else { return }
            searchPick = first.pane.id
            model.showPage(first.pane.id, row: first.row)
        }
        .toolbar(removing: .sidebarToggle)
        .toolbar(removing: .title)
        .confirmationDialog(model.homeLabel("clearHistoryQuestion"),
                            isPresented: $model.confirmingClearInsights) {
            Button(model.homeLabel("clearHistoryConfirm"), role: .destructive) { model.clearInsights() }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text(model.homeLabel("clearHistoryBody"))
        }
        .alert(model.homeLabel("clearHistoryFailed"),
               isPresented: $model.clearInsightsFailed) {
            // ui-lint: allow core-string (deleting the history again, not the update banner's retry)
            Button("Try again") { model.clearInsights() }
                .keyboardShortcut(.defaultAction)
            Button("Cancel", role: .cancel) {}
        }
    }

    /// The banner and the title keep to the column a grouped form centres its
    /// cards in, so their edges line up however wide the window is. The pane
    /// itself stays full width, so its scroller sits at the window edge.
    private static let columnWidth: CGFloat = 742

    @ViewBuilder private var detail: some View {
        if let pane = model.pane(withId: model.pane) {
            VStack(spacing: 0) {
                header(pane)
                    .frame(maxWidth: Self.columnWidth, alignment: .leading)
                // Content scrolls under the title, so the two are kept apart
                // rather than the content being cut off at a glyph.
                Divider()
                    .padding(.top, 8)
                PaneView(pane: pane, model: model)
                    // A fresh view per pane, so state one pane keeps, such as
                    // Vocabulary's chosen view, does not carry over to the next.
                    .id(pane.id)
            }
        }
    }

    @ViewBuilder private func header(_ pane: Pane) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            if model.update.visible {
                UpdateBanner(model: model)
                    .scenePadding([.top, .horizontal])
            } else if model.whatsNewPending {
                WhatsNewStrip(banner: model.whatsNewBanner,
                              seeWhatsNew: { model.showWhatsNew() },
                              dismiss: { model.dismissWhatsNew() })
                    .scenePadding([.top, .horizontal])
            }
            HStack {
                if pane.id == "whatsNew" {
                    Button("Back", systemImage: "chevron.backward") { model.leaveWhatsNew() }
                        .labelStyle(.iconOnly)
                }
                Text(pane.title)
                    .font(.title2.weight(.semibold))
            }
            .scenePadding([.top, .horizontal])
        }
    }
}

/// The update banner over the detail column, bound to the live model. The
/// content is a separate value view so an offscreen renderer can seed each
/// state directly.
struct UpdateBanner: View {
    @ObservedObject var model: AppModel

    var body: some View {
        UpdateBannerContent(banner: model.update,
                            act: { model.runUpdateAction() },
                            later: { model.deferUpdate() },
                            dismiss: { model.dismissUpdate() })
    }
}

/// One update banner as core words it: the message, download progress, and
/// whichever of the action, Later and Dismiss buttons the state offers.
struct UpdateBannerContent: View {
    let banner: SpeecherUpdateBanner
    var act: () -> Void = {}
    var later: () -> Void = {}
    var dismiss: () -> Void = {}

    var body: some View {
        GroupBox { row }
    }

    /// The banner's row, kept separate from its GroupBox so the offscreen
    /// preview renderer can place it on a card ImageRenderer can rasterise.
    @ViewBuilder var row: some View {
        HStack {
            Label(banner.text, systemImage: icon)
            Spacer()
            if banner.progress >= 0 {
                ProgressView(value: Double(banner.progress), total: 100)
                    .frame(width: 120)
            }
            if !banner.action.isEmpty {
                Button(banner.action, action: act)
                    .disabled(!banner.actionEnabled)
            }
            if !banner.later.isEmpty {
                Button(banner.later, action: later)
            }
            if !banner.dismiss.isEmpty {
                Button(banner.dismiss, action: dismiss)
            }
        }
    }

    private var icon: String {
        switch banner.tone {
        case .error: return "exclamationmark.triangle.fill"
        case .positive: return "checkmark.circle"
        default: return "arrow.down.circle"
        }
    }
}

/// The post-update strip: the installed version and a way into What's New. Value
/// driven for the same reason as the banner content.
struct WhatsNewStrip: View {
    let banner: SpeecherWhatsNewBanner
    var seeWhatsNew: () -> Void = {}
    var dismiss: () -> Void = {}

    var body: some View {
        GroupBox { row }
    }

    @ViewBuilder var row: some View {
        HStack {
            Label(banner.text, systemImage: "sparkles")
            Spacer()
            Button(banner.action, action: seeWhatsNew)
            Button(banner.dismiss, action: dismiss)
        }
    }
}

/// The source list: the panes in their runs, plus What's New on top while it
/// is pending or selected, filtered by whatever the search field holds. The
/// core index answers the search, so a pane answers to its own name and to any
/// group heading, row label or help text it carries, as on every platform.
struct SidebarList: View {
    @ObservedObject var model: AppModel
    @Binding var query: String
    @Binding var searchPick: String?

    var body: some View {
        // Every pick goes through showPage, so choosing What's New here is the
        // same as any other way of opening it. A search hit opens at the row
        // that matched.
        List(selection: Binding<String?>(get: { query.isEmpty ? model.pane : searchPick },
                                         set: { pick in
                                             guard let pick else { return }
                                             if query.isEmpty {
                                                 if pick != model.pane { model.showPage(pick) }
                                                 return
                                             }
                                             searchPick = pick
                                             let row = model.search(query).first(where: { $0.pane.id == pick })?.row
                                             model.showPage(pick, row: row)
                                         })) {
            if query.isEmpty {
                // Each titled group under the native section header; the top
                // group has none, and What's New leads it while pending or open.
                ForEach(Array(model.sidebarGroups.enumerated()), id: \.offset) { index, group in
                    let panes = group.panes.compactMap(model.pane(withId:))
                    if group.title.isEmpty {
                        Section {
                            if index == 0, model.pane == "whatsNew" || model.whatsNewPending,
                               let whatsNew = model.pane(withId: "whatsNew") {
                                row(whatsNew)
                            }
                            ForEach(panes) { row($0) }
                        }
                    } else {
                        Section(group.title) {
                            ForEach(panes) { row($0) }
                        }
                    }
                }
            } else {
                // A search shows its hits as one flat list, not under the groups
                // they came from, and opens each at the row that matched.
                ForEach(model.search(query)) { hit in
                    row(hit.pane)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .contentShape(Rectangle())
                        // A click on the hit already picked changes no
                        // selection, yet should still bring its row back.
                        .simultaneousGesture(TapGesture().onEnded {
                            model.showPage(hit.pane.id, row: hit.row)
                        })
                }
            }
        }
        .listStyle(.sidebar)
        .onChange(of: query) { searchPick = nil }
        .overlay {
            if !query.isEmpty, model.search(query).isEmpty {
                ContentUnavailableView(SpeecherBridge.noSettingsMatchText, systemImage: "magnifyingglass")
            }
        }
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
    private var closeObserver: NSObjectProtocol?

    init(model: AppModel) {
        self.model = model
        // No miniaturize: a settings window is quick to reopen with ⌘, so it has
        // no business in the Dock. It remains resizable, and zooms, for the
        // table panes.
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
        // After the autosaved frame, which would otherwise win.
        applyRequestedSize()
        // The window title is the pane the user is looking at. The pane list is
        // captured by value: a closure the model's own publisher retains must
        // not capture the model.
        let panes = model.panes
        window.title = panes.first { $0.id == model.pane }?.title ?? "Settings"
        titleObserver = model.$pane.sink { [weak window] pane in
            window?.title = panes.first { $0.id == pane }?.title ?? "Settings"
        }
        // A recording must not outlive the window it was started in.
        closeObserver = NotificationCenter.default.addObserver(
            forName: NSWindow.willCloseNotification, object: window, queue: .main
        ) { [weak model] _ in
            MainActor.assumeIsolated { model?.stopShortcutRecording() }
        }
    }

    /// Screenshot automation: SPEECHER_GRAB_SIZE=WxH sizes the window's
    /// content, as on Linux. Says whether it asked for a size.
    @discardableResult
    private func applyRequestedSize() -> Bool {
        let parts = (ProcessInfo.processInfo.environment["SPEECHER_GRAB_SIZE"] ?? "")
            .split(separator: "x").compactMap { Double($0) }
        guard parts.count == 2 else { return false }
        window.setContentSize(NSSize(width: parts[0], height: parts[1]))
        return true
    }

    /// Whether the window is on screen, which a Sparkle relaunch restores.
    var isVisible: Bool { window.isVisible }

    func close() {
        window.close()
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
            model.refreshLocalSetup()
        }
    }

    // Called from the front end on the main thread, which is where the window
    // has to be touched. SwiftUI's ImageRenderer is not an alternative: it
    // refuses NavigationSplitView outright.
    //
    // SPEECHER_GRAB_PAGE names the page to show first, as a page id on every
    // front end; unset leaves the window as it is, unknown shows Home.
    // SPEECHER_GRAB_ROW names a row of that page to scroll to, as search does.
    func capture(toPath path: String) -> Bool {
        let environment = ProcessInfo.processInfo.environment
        let request = environment["SPEECHER_GRAB_PAGE"] ?? ""
        // Again here: showing the window fitted it to the screen, and the
        // backing store has no such limit.
        let resized = applyRequestedSize()
        if !request.isEmpty { model.showPage(request, row: environment["SPEECHER_GRAB_ROW"]) }
        if resized || !request.isEmpty {
            // Let SwiftUI render the pane before the backing store is read.
            RunLoop.main.run(until: Date(timeIntervalSinceNow: 0.5))
            window.contentView?.layoutSubtreeIfNeeded()
            window.displayIfNeeded()
        }
        return window.captureBackingStore(toPath: path)
    }
}

extension NSWindow {
    /// Writes the window as a PNG, titlebar included. This is the backing
    /// store, so nothing the compositor draws for the window comes out:
    /// vibrancy materials are blank, and the settings sidebar, which SwiftUI
    /// puts inside a glass container, is missing. For a composited shot,
    /// screencapture with Screen Recording granted is the way.
    func captureBackingStore(toPath path: String) -> Bool {
        guard let content = contentView,
              let view = content.superview ?? contentView,
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
