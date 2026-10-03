// The CGEvent fallback for vocabulary_run.sh: "click X Y" posts a left click
// at screen point X,Y; "type TEXT" types TEXT. Scratch-branch-only.
import CoreGraphics
import Foundation

let arguments = CommandLine.arguments
let source = CGEventSource(stateID: .hidSystemState)
switch arguments.count > 1 ? arguments[1] : "" {
case "click" where arguments.count == 4:
    let point = CGPoint(x: Double(arguments[2]) ?? 0, y: Double(arguments[3]) ?? 0)
    for type in [CGEventType.mouseMoved, .leftMouseDown, .leftMouseUp] {
        CGEvent(mouseEventSource: source, mouseType: type, mouseCursorPosition: point,
                mouseButton: .left)?.post(tap: .cghidEventTap)
        usleep(80_000)
    }
case "type" where arguments.count == 3:
    for unit in arguments[2].utf16 {
        for down in [true, false] {
            let event = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: down)
            var character = unit
            event?.keyboardSetUnicodeString(stringLength: 1, unicodeString: &character)
            event?.post(tap: .cghidEventTap)
        }
        usleep(40_000)
    }
default:
    FileHandle.standardError.write("usage: click_at click X Y | type TEXT\n".data(using: .utf8)!)
    exit(2)
}
