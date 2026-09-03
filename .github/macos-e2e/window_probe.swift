import AppKit
import CoreGraphics
import Foundation
import ImageIO

func windows(for pid: pid_t) -> [[String: Any]] {
    guard let all = CGWindowListCopyWindowInfo(.optionAll, kCGNullWindowID)
        as? [[String: Any]] else { return [] }
    return all.filter {
        ($0[kCGWindowOwnerPID as String] as? NSNumber)?.int32Value == pid
    }.map { info in
        [
            "bounds": info[kCGWindowBounds as String] ?? NSNull(),
            "layer": info[kCGWindowLayer as String] ?? NSNull(),
            "alpha": info[kCGWindowAlpha as String] ?? NSNull(),
            "isOnscreen": info[kCGWindowIsOnscreen as String] ?? false,
            "name": info[kCGWindowName as String] ?? "",
            "ownerPID": info[kCGWindowOwnerPID as String] ?? pid,
        ]
    }
}

func save(_ value: Any, to path: String) {
    guard let data = try? JSONSerialization.data(withJSONObject: value,
                                                  options: [.prettyPrinted, .sortedKeys]) else {
        exit(4)
    }
    try? data.write(to: URL(fileURLWithPath: path))
}

func panelBounds(_ windows: [[String: Any]]) -> CGRect? {
    for window in windows {
        guard (window["layer"] as? NSNumber)?.intValue == 25,
              (window["alpha"] as? NSNumber)?.doubleValue ?? 0 > 0,
              (window["isOnscreen"] as? Bool) == true,
              let bounds = window["bounds"] as? [String: Any],
              let x = (bounds["X"] as? NSNumber)?.doubleValue,
              let y = (bounds["Y"] as? NSNumber)?.doubleValue,
              let width = (bounds["Width"] as? NSNumber)?.doubleValue,
              let height = (bounds["Height"] as? NSNumber)?.doubleValue,
              width >= 420, abs(height - 72) < 1 else { continue }
        return CGRect(x: x, y: y, width: width, height: height)
    }
    return nil
}

func pixels(in path: String, bounds: CGRect) -> [UInt8]? {
    guard let source = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil),
          let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else { return nil }
    let scaleX = CGFloat(image.width) / (NSScreen.screens.map(\.frame.maxX).max() ?? 1)
    let scaleY = CGFloat(image.height) / (NSScreen.screens.map(\.frame.maxY).max() ?? 1)
    let crop = CGRect(x: bounds.minX * scaleX,
                      y: bounds.minY * scaleY,
                      width: bounds.width * scaleX,
                      height: bounds.height * scaleY).integral
    guard let panel = image.cropping(to: crop),
          let data = panel.dataProvider?.data else { return nil }
    return Array(UnsafeBufferPointer(start: CFDataGetBytePtr(data),
                                     count: CFDataGetLength(data)))
}

guard CommandLine.arguments.count >= 4,
      let pid = Int32(CommandLine.arguments[2]) else { exit(64) }

let mode = CommandLine.arguments[1]
let current = windows(for: pid)

if mode == "dump" {
    save(current, to: CommandLine.arguments[3])
    exit(0)
}

guard mode == "panel", CommandLine.arguments.count == 6 else { exit(64) }
save(current, to: CommandLine.arguments[5])
guard let bounds = panelBounds(current) else { exit(2) }
guard let before = pixels(in: CommandLine.arguments[3], bounds: bounds),
      let after = pixels(in: CommandLine.arguments[4], bounds: bounds),
      before.count == after.count else { exit(3) }
let changed = zip(before, after).reduce(0) { $0 + ($1.0 == $1.1 ? 0 : 1) }
exit(changed > max(100, before.count / 1000) ? 0 : 3)
