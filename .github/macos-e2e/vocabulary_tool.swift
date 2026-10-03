// Real input and OCR for vocabulary_run.sh. Scratch-branch-only.
//   click X Y            a left click posted at screen point X,Y
//   type TEXT            TEXT typed into whatever has focus
//   ocr PNG [X,Y,W,H]    the text Vision reads in the screenshot, or in the
//                        given rectangle of it in screen points
import CoreGraphics
import Foundation
import ImageIO
import Vision

let arguments = CommandLine.arguments
let source = CGEventSource(stateID: .hidSystemState)

func ocr(_ path: String, _ rect: String?) -> String {
    guard let file = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil),
          var image = CGImageSourceCreateImageAtIndex(file, 0, nil) else { exit(1) }
    if let rect {
        let parts = rect.split(separator: ",").compactMap { Double($0) }
        let scale = Double(image.width) / CGDisplayBounds(CGMainDisplayID()).width
        if parts.count == 4,
           let cropped = image.cropping(to: CGRect(x: parts[0] * scale, y: parts[1] * scale,
                                                   width: parts[2] * scale, height: parts[3] * scale)) {
            image = cropped
        }
    }
    let request = VNRecognizeTextRequest()
    request.recognitionLevel = .accurate
    request.usesLanguageCorrection = false
    try? VNImageRequestHandler(cgImage: image).perform([request])
    return (request.results ?? []).compactMap { $0.topCandidates(1).first?.string }
        .joined(separator: " ")
}

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
case "ocr" where arguments.count == 3 || arguments.count == 4:
    print(ocr(arguments[2], arguments.count == 4 ? arguments[3] : nil))
default:
    FileHandle.standardError.write("usage: vocabulary_tool click X Y | type TEXT | ocr PNG [X,Y,W,H]\n"
        .data(using: .utf8)!)
    exit(2)
}
