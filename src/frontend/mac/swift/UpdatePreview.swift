import AppKit
import SwiftUI

// Offscreen PNGs of the update UI states, for the PR that documents them. The
// runner has no display and no signed appcast, so each state is rendered from
// its real view with seeded values through ImageRenderer rather than driven
// through a live update. Test-only: nothing in the app calls this.

/// One captured state: the file it lands in and the view rendered into it.
@MainActor
@objc public final class SpeecherUpdatePreview: NSObject {
    /// Renders the update states as PNGs into `directory`, from the banners
    /// the bridge derives for this app's own updater state. The caller seeds
    /// the states through `banners`, in the order of the files.
    @MainActor
    @objc public static func render(toDirectory directory: String,
                                    banners: [SpeecherUpdateBanner],
                                    whatsNew: SpeecherWhatsNewBanner) -> [String] {
        let base = URL(fileURLWithPath: directory, isDirectory: true)
        try? FileManager.default.createDirectory(at: base, withIntermediateDirectories: true)

        let updateBannerState = DictationPanelState()
        updateBannerState.status = "Listening"
        updateBannerState.preview = "the quick brown fox"
        if let first = banners.first {
            updateBannerState.updateMessage = first.text
            updateBannerState.updateAction = first.action
        }

        let whatsNewBannerState = DictationPanelState()
        whatsNewBannerState.status = "Listening"
        whatsNewBannerState.preview = "the quick brown fox"
        whatsNewBannerState.whatsNewMessage = whatsNew.text

        var jobs: [(String, CGSize, AnyView)] = banners.enumerated().map { index, banner in
            (String(format: "%02d-settings-banner.png", index + 1), CGSize(width: 640, height: 90),
             AnyView(card(UpdateBannerContent(banner: banner).row)))
        }
        jobs += [
            ("20-settings-whats-new-strip.png", CGSize(width: 640, height: 90),
             AnyView(card(WhatsNewStrip(banner: whatsNew).row))),
            ("21-panel-update-chip.png", CGSize(width: 520, height: 150),
             AnyView(panel(updateBannerState))),
            ("22-panel-whats-new-chip.png", CGSize(width: 520, height: 150),
             AnyView(panel(whatsNewBannerState))),
        ]

        var written: [String] = []
        for (name, size, view) in jobs {
            guard writePNG(view, size: size, to: base.appendingPathComponent(name)) else {
                return []
            }
            written.append(name)
        }
        return written
    }

    /// A banner or strip row on a rounded card. The settings window draws these
    /// rows in a native GroupBox, which ImageRenderer leaves blank offscreen, so
    /// the capture wraps the same row in a material card it can rasterise.
    @MainActor
    private static func card(_ row: some View) -> some View {
        row
            .padding(.horizontal, 14)
            .padding(.vertical, 10)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 8))
            .padding()
    }

    /// The dictation panel view with no-op actions, at rest on the live phase.
    @MainActor
    private static func panel(_ state: DictationPanelState) -> some View {
        DictationPanelView(state: state,
                           dismiss: {},
                           installUpdate: {},
                           openWhatsNew: {},
                           dismissWhatsNew: {})
            .padding()
    }

    @MainActor
    private static func writePNG(_ view: some View, size: CGSize, to url: URL) -> Bool {
        // A solid backing so a material that would be blank offscreen still
        // reads as a card rather than a transparent hole.
        let renderer = ImageRenderer(content: view
            .frame(width: size.width, height: size.height)
            .background(Color(nsColor: .windowBackgroundColor)))
        renderer.scale = 2
        guard let image = renderer.nsImage,
              let tiff = image.tiffRepresentation,
              let rep = NSBitmapImageRep(data: tiff),
              let png = rep.representation(using: .png, properties: [:]) else {
            return false
        }
        return (try? png.write(to: url)) != nil
    }
}
