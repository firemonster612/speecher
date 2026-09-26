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
        .confirmationDialog("Delete all insights history?",
                            isPresented: $model.confirmingClearInsights) {
            Button("Delete History", role: .destructive) { model.clearInsights() }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Your stats, streaks and records are erased from this computer. This can't be undone.")
        }
    }

    @ViewBuilder private var detail: some View {
        if let pane = model.pane(withId: model.pane) {
            VStack(alignment: .leading, spacing: 0) {
                if model.updateBannerShown {
                    UpdateBanner(model: model)
                        .scenePadding([.top, .horizontal])
                } else if model.whatsNewPending {
                    WhatsNewStrip(installedNumber: model.installedVersionNumber,
                                  seeWhatsNew: { model.showWhatsNew() },
                                  dismiss: { model.dismissWhatsNew() })
                        .scenePadding([.top, .horizontal])
                }
                Text(pane.title)
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

/// The update banner over the detail column, bound to the live model. The
/// state-driven content is a separate value view so an offscreen renderer can
/// seed each state directly.
struct UpdateBanner: View {
    @ObservedObject var model: AppModel

    var body: some View {
        UpdateBannerContent(update: model.update,
                            install: { model.installUpdateAndRestart() },
                            restart: { model.updateNow() },
                            later: { model.updateBannerDeferred = true },
                            retry: { model.updateNow() },
                            dismiss: { model.dismissUpdate() })
    }
}

/// The banner's look for one update state, mirroring the Linux settings banner:
/// offer, download progress, restart, and error. Value-driven, so the actions
/// default to nothing and a renderer can show a state without a model.
struct UpdateBannerContent: View {
    let update: AppModel.UpdateStatus
    var install: () -> Void = {}
    var restart: () -> Void = {}
    var later: () -> Void = {}
    var retry: () -> Void = {}
    var dismiss: () -> Void = {}

    var body: some View {
        GroupBox { row }
    }

    /// The banner's row, kept separate from its GroupBox so the offscreen
    /// preview renderer can place it on a card ImageRenderer can rasterise.
    @ViewBuilder var row: some View {
        HStack {
            Label(text, systemImage: icon)
            Spacer()
            if update.state == .downloading {
                ProgressView(value: Double(update.percent), total: 100)
                    .frame(width: 120)
            } else if update.state == .checking {
                ProgressView().controlSize(.small)
            }
            switch update.state {
            case .updateAvailable:
                Button("Install and restart", action: install)
                Button("Dismiss", action: dismiss)
            case .readyToRestart:
                Button("Restart now", action: restart)
                Button("Later", action: later)
            case .error, .checkFailed:
                Button("Try again", action: retry)
                Button("Dismiss", action: dismiss)
            case .upToDate:
                Button("Dismiss", action: dismiss)
            default:
                // Checking, downloading, restart pending and restarting carry
                // no actions: the sentence is the whole message.
                EmptyView()
            }
        }
    }

    private var icon: String {
        switch update.state {
        case .error, .checkFailed: return "exclamationmark.triangle.fill"
        case .upToDate: return "checkmark.circle"
        case .checking: return "arrow.triangle.2.circlepath"
        default: return "arrow.down.circle"
        }
    }

    private var text: String {
        switch update.state {
        case .checking:
            return "Checking for updates…"
        case .upToDate:
            return "Speecher is up to date"
        case .updateAvailable:
            return update.stableReplacement
                ? "Switch to Stable Release \(update.version) (replaces this Nightly Build)"
                : "Speecher \(update.version) is available"
        case .downloading:
            return "Downloading Speecher \(update.version)"
        case .readyToRestart:
            return update.error.isEmpty ? "Restart to finish updating" : update.error
        case .restartPending:
            return "Restarting after this dictation…"
        case .restarting:
            return "Restarting…"
        case .error, .checkFailed:
            return update.error.isEmpty ? "Update check failed" : update.error
        default:
            return ""
        }
    }
}

/// The post-update strip: the installed version and a way into What's New. Value
/// driven for the same reason as the banner content.
struct WhatsNewStrip: View {
    let installedNumber: String
    var seeWhatsNew: () -> Void = {}
    var dismiss: () -> Void = {}

    var body: some View {
        GroupBox { row }
    }

    @ViewBuilder var row: some View {
        HStack {
            Label("Speecher \(installedNumber) is installed", systemImage: "sparkles")
            Spacer()
            Button("See what's new", action: seeWhatsNew)
            Button("Dismiss", action: dismiss)
        }
    }
}

/// The source list: eight regular panes in runs, plus What's New while selected,
/// filtered by whatever the search field holds. The schema is the index, so a
/// pane answers to its own name and to any group heading, row label or help text
/// it carries.
struct SidebarList: View {
    @ObservedObject var model: AppModel
    @Binding var query: String

    var body: some View {
        List(selection: $model.pane) {
            if query.isEmpty {
                if model.pane == "whatsNew", let pane = model.pane(withId: model.pane) {
                    row(pane)
                }
                ForEach(Array(model.sidebarRuns.enumerated()), id: \.offset) { _, run in
                    Section {
                        ForEach(run.compactMap(model.pane(withId:))) { row($0) }
                    }
                }
            } else {
                // A search shows its hits as one flat list, not as the runs they
                // came from.
                ForEach(model.panes.filter { model.pane($0, matches: query) }) { row($0) }
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

    // Called from the front end on the main thread, which is where the window
    // has to be touched.
    //
    // This is the window's backing store, so nothing the compositor draws for
    // the window comes out: vibrancy materials are blank, and the whole sidebar
    // column, which SwiftUI puts inside a glass container, is missing. The
    // detail column and the titlebar are real. SwiftUI's ImageRenderer is not an
    // alternative: it refuses NavigationSplitView outright. For a composited
    // shot, screencapture with Screen Recording granted is the way.
    //
    // SPEECHER_GRAB_PAGE names the pane to show first, as on the other front
    // ends; unset or unknown leaves the window as it is.
    func capture(toPath path: String) -> Bool {
        let request = ProcessInfo.processInfo.environment["SPEECHER_GRAB_PAGE"]?
            .lowercased().split(separator: ":").first.map(String.init) ?? ""
        // Again here: showing the window fitted it to the screen, and the
        // backing store has no such limit.
        let resized = applyRequestedSize()
        let pane = model.panes.first { $0.id.lowercased() == request }
        if let pane { model.pane = pane.id }
        if resized || pane != nil {
            // Let SwiftUI render the pane before the backing store is read.
            RunLoop.main.run(until: Date(timeIntervalSinceNow: 0.5))
            window.contentView?.layoutSubtreeIfNeeded()
            window.displayIfNeeded()
        }
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
