import AppKit
import SwiftUI

// Speecher's macOS window. Everything it knows about the app arrives through
// SpeecherBridge, which is this target's bridging header; there is no C++ here.

/// The settings surface as SwiftUI reads it. The bridge hands over immutable
/// snapshots, so a write goes back through the bridge and the snapshot is taken
/// again — which is also what re-derives the rows a changed value gates.
final class SettingsModel: ObservableObject {
    @Published private(set) var pages: [SettingsPageModel]
    @Published private(set) var status: String

    private let bridge: SpeecherBridge

    init(bridge: SpeecherBridge) {
        self.bridge = bridge
        pages = bridge.settingsSchema.pages
        status = bridge.stateName
        bridge.statusChanged = { [weak self] status in
            self?.status = status
        }
    }

    func page(_ pageId: String) -> SettingsPageModel? {
        pages.first { $0.pageId == pageId }
    }

    func trigger(_ rowId: String) {
        bridge.settingsSchema.actionTriggered?(rowId)
    }

    func binding<Value>(_ row: SettingsRowModel,
                        read: @escaping (Any?) -> Value,
                        write: @escaping (Value) -> Any) -> Binding<Value> {
        Binding(get: { read(row.value) },
                set: { [weak self] newValue in
                    guard let self else { return }
                    bridge.settingsSchema.setValue(write(newValue), forRowId: row.rowId)
                    bridge.settingsSchema.commit()
                    pages = bridge.settingsSchema.pages
                })
    }
}

struct RootView: View {
    @ObservedObject var model: SettingsModel
    @State private var selection = "general"

    var body: some View {
        NavigationSplitView {
            List(model.pages, id: \.pageId, selection: $selection) { page in
                Label(page.title, systemImage: page.symbolName)
            }
            .listStyle(.sidebar)
            .navigationSplitViewColumnWidth(min: 190, ideal: 200)
            .safeAreaInset(edge: .bottom) { stateBadge }
        } detail: {
            detail
        }
    }

    @ViewBuilder private var detail: some View {
        if let page = model.page(selection) {
            // One page is native so far; the rest arrive in later slices.
            if page.pageId == "general" {
                PageForm(page: page, model: model).navigationTitle(page.title)
            } else {
                ContentUnavailableView(page.title,
                                       systemImage: page.symbolName,
                                       description: Text("This page is not native yet."))
                    .navigationTitle(page.title)
            }
        }
    }

    private var badge: some View {
        Text(model.status)
            .font(.caption)
            .padding(.horizontal, 12)
            .padding(.vertical, 7)
    }

    @ViewBuilder private var stateBadge: some View {
        if #available(macOS 26, *) {
            badge.glassEffect()
        } else {
            badge.background(.quaternary, in: .capsule)
        }
    }
}

struct PageForm: View {
    let page: SettingsPageModel
    @ObservedObject var model: SettingsModel

    var body: some View {
        Form {
            // A section title is not unique enough to key on: a page may have a
            // section only for the shape, with no title at all.
            ForEach(Array(page.sections.enumerated()), id: \.offset) { _, section in
                Section {
                    ForEach(section.rows, id: \.rowId) { row in
                        RowView(row: row, model: model)
                    }
                } header: {
                    if !section.title.isEmpty { Text(section.title) }
                } footer: {
                    if !section.help.isEmpty { Text(section.help) }
                }
            }
        }
        .formStyle(.grouped)
    }
}

struct RowView: View {
    let row: SettingsRowModel
    @ObservedObject var model: SettingsModel

    var body: some View {
        control.disabled(!row.enabled)
    }

    @ViewBuilder private var control: some View {
        switch row.kind {
        case .toggle:
            Toggle(isOn: model.binding(row, read: Self.flag, write: { $0 as NSNumber })) { label }
        case .choice:
            Picker(selection: model.binding(row, read: Self.text, write: { $0 })) {
                ForEach(row.options, id: \.rowOptionId) { option in
                    Text(option.label).tag(option.rowOptionId)
                }
            } label: {
                label
            }
        case .number:
            LabeledContent {
                Stepper(value: model.binding(row, read: Self.number, write: { $0 as NSNumber }),
                        in: row.minimum...row.maximum,
                        step: row.step) {
                    Text("\(Self.number(row.value))\(row.suffix)").monospacedDigit()
                }
            } label: {
                label
            }
        case .text:
            TextField(text: model.binding(row, read: Self.text, write: { $0 })) { label }
        case .info:
            LabeledContent { Text(Self.text(row.value)).foregroundStyle(.secondary) } label: { label }
        case .action:
            LabeledContent { Button(row.actionLabel) { model.trigger(row.rowId) } } label: { label }
        default:
            LabeledContent { Text("Not native yet").foregroundStyle(.secondary) } label: { label }
        }
    }

    private var label: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(row.label)
            if !row.help.isEmpty {
                Text(row.help).font(.caption).foregroundStyle(.secondary)
            }
        }
    }

    private static func flag(_ value: Any?) -> Bool { (value as? NSNumber)?.boolValue ?? false }
    private static func number(_ value: Any?) -> Int { (value as? NSNumber)?.intValue ?? 0 }
    private static func text(_ value: Any?) -> String { value as? String ?? "" }
}

/// The window itself, so the Objective-C++ front end never has to know what
/// SwiftUI view is inside it.
@objc public final class SpeecherMainWindow: NSObject {
    private let model: SettingsModel
    private let window: NSWindow

    @objc public init(bridge: SpeecherBridge) {
        model = SettingsModel(bridge: bridge)
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 900, height: 620),
                          styleMask: [.titled, .closable, .miniaturizable, .resizable,
                                      .fullSizeContentView],
                          backing: .buffered,
                          defer: false)
        super.init()
        window.title = "Speecher"
        window.titlebarAppearsTransparent = true
        window.contentView = NSHostingView(rootView: RootView(model: model))
        window.center()
    }

    @objc public func show() {
        window.makeKeyAndOrderFront(nil)
    }

    // Called from the front end on the main thread, which is where the window
    // has to be touched.
    //
    // This is the window's backing store, so vibrancy materials come out blank
    // — an offscreen render cannot ask the compositor for what is behind the
    // window. SwiftUI's ImageRenderer is not an alternative: it refuses
    // NavigationSplitView outright. For a composited shot, screencapture with
    // Screen Recording granted is the way.
    @MainActor
    @objc public func capture(toPath path: String) -> Bool {
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
