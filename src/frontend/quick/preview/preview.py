"""Render the QML front end with mock data and save screenshots or video.

Runs inside a headless KWin (see preview/run.sh), so nothing appears on the
desktop. Examples:

  preview/run.sh --page shortcuts --out shots/shortcuts.png
  preview/run.sh --page home --theme light --out shots/home-light.png
  preview/run.sh --page general --search "hotkey" --out shots/search.png
  preview/run.sh --page vocabulary --record 2 --time-scale 4 --poke vocabularyPage.tab=corrections@0.6 --out tab.mp4
  preview/run.sh --page home --poke Speecher.Backend/Dictation.state=listening@0.5 --out live.png
  preview/run.sh --root Speecher.App/SetupAssistant --record 2 --poke step=3@0.5 --out step.mp4
  preview/run.sh --page pasting --scroll 900 --out lower.png
  preview/run.sh --page models --record 3 --press 900,470@0.5 --out press.mp4
  preview/run.sh --root SpeecherPreview/PopupScene --size 1200x760 --arg popup-demo=review --out shots/review.png
  preview/run.sh --root SpeecherPreview/PopupScene --size 1200x760 --record 19 --time-scale 3 --out shots/popup.mp4
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from PySide6.QtCore import Q_ARG, QMetaObject, QObject, QPoint, Qt, QTimer
from PySide6.QtGui import QFontDatabase, QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuick import QQuickWindow
from PySide6.QtTest import QTest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


def parse():
    p = argparse.ArgumentParser()
    p.add_argument("--root", default="Speecher.App/Main", help="Module/Type of the window to load")
    p.add_argument("--page", default="home")
    p.add_argument("--row", help="Jump to this settings row, as search does")
    p.add_argument("--search", help="Open the search palette with this query")
    p.add_argument("--theme", default="light", choices=["dark", "light"])
    p.add_argument("--size", default="1240x820")
    p.add_argument("--settle", type=float, default=1.6, help="Seconds to wait before the screenshot")
    p.add_argument("--set", action="append", default=[], help="Set a property on the root: name=value")
    p.add_argument("--setting", action="append", default=[],
                   help="Start with a settings row's value, as the mock Settings would save it: id=value")
    p.add_argument("--arg", action="append", default=[],
                   help="Pass name=value to QML as --name=value in Qt.application.arguments, for mock states")
    p.add_argument("--poke", action="append", default=[],
                   help="At a time, set a property: target.property=value@seconds (seconds after load; with "
                        "--record, in the recording's own time). target is an objectName (list delegates "
                        "included), a singleton as Module/Type, or nothing for the window itself (step=3@1). "
                        "Use it to trigger a tab switch or a state, then screenshot or record the transition.")
    p.add_argument("--press", action="append", default=[],
                   help="At a time, move the pointer to x,y (window points) and press there for 0.3 s: "
                        "x,y@seconds, timed like --poke. Use it to record hover and press feedback.")
    p.add_argument("--scroll", type=float, help="Scroll the page this far down (at most to its end) before the screenshot")
    p.add_argument("--record", type=float, help="Record this many seconds of video instead")
    p.add_argument("--time-scale", type=float, default=1.0, help="Slow animations down while recording")
    p.add_argument("--fps", type=int, default=60)
    p.add_argument("--out", required=True)
    return p.parse_known_args()[0]


def parse_value(value):
    for kind in (int, float):
        try:
            return kind(value)
        except ValueError:
            pass
    return {"true": True, "false": False}.get(value, value)



def main():
    args = parse()
    qt_args = [sys.argv[0], f"--theme={args.theme}"]
    scale = args.time_scale if args.record else 1.0
    qt_args.append(f"--time-scale={scale}")
    qt_args += [f"--{a}" for a in args.arg]
    app = QGuiApplication(qt_args)
    for font in sorted((ROOT / "assets").rglob("*.ttf")):
        QFontDatabase.addApplicationFont(str(font))

    engine = QQmlApplicationEngine()
    engine.addImportPath(str(ROOT / "qml"))
    engine.addImportPath(str(ROOT / "mock"))
    engine.addImportPath(str(HERE / "qml"))
    engine.warnings.connect(lambda ws: [print("QML:", w.toString(), file=sys.stderr) for w in ws])

    nav = engine.singletonInstance("Speecher.App", "Navigation")
    if nav is not None:
        nav.setProperty("current", args.page)

    settings = engine.singletonInstance("Speecher.Backend", "Settings")
    for assignment in args.setting:
        row, _, value = assignment.partition("=")
        QMetaObject.invokeMethod(settings, "set", Q_ARG("QVariant", row), Q_ARG("QVariant", parse_value(value)))
    module, type_name = args.root.split("/")
    engine.loadFromModule(module, type_name)
    if not engine.rootObjects():
        sys.exit("failed to load " + args.root)
    window: QQuickWindow = engine.rootObjects()[0]
    # As the app does, so the windows follow the Appearance settings; the
    # Theme setting starts at System, which follows --theme.
    engine.loadFromModule("Speecher.App", "ThemeSetting")
    w, h = (int(v) for v in args.size.split("x"))
    window.setWidth(w)
    window.setHeight(h)
    for assignment in args.set:
        name, value = assignment.split("=", 1)
        window.setProperty(name, value)

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    def after_settle():
        if args.row and nav is not None:
            nav.goToRow(args.row)
        if args.search is not None:
            palette = window.findChild(QObject, "searchPalette")
            palette.open()
            palette.setProperty("query", args.search)

    QTimer.singleShot(int(args.settle * 300), after_settle)

    def find(name):
        """The object a poke names: the window, a singleton, or an item by objectName."""
        if not name:
            return window
        if "/" in name:
            return engine.singletonInstance(*name.split("/"))
        found = window.findChild(QObject, name)
        # List delegates are not QObject children of the list; walk the items.
        items = [window.contentItem()]
        while found is None and items:
            item = items.pop()
            found = item if item.objectName() == name else None
            items += item.childItems()
        return found

    pokes = [(p.rpartition("@")[0], float(p.rpartition("@")[2])) for p in args.poke]
    for target, when in pokes:
        path, _, value = target.partition("=")
        name, _, prop = path.rpartition(".")
        delay = when * (scale if args.record else 1) + (0.4 if args.record else 0)

        def apply(name=name, prop=prop, value=value):
            obj = find(name)
            if obj is None:
                print(f"poke: nothing named {name}", file=sys.stderr)
                return
            obj.setProperty(prop, parse_value(value))

        QTimer.singleShot(int(delay * 1000), apply)

    # The pointer arrives a moment before the press, so hover shows first.
    for spec in args.press:
        point, _, when = spec.rpartition("@")
        x, y = (int(v) for v in point.split(","))
        delay = float(when) * (scale if args.record else 1) + (0.4 if args.record else 0)
        at = QPoint(x, y)
        QTimer.singleShot(int((delay - 0.25 * scale) * 1000), lambda at=at: QTest.mouseMove(window, at))
        QTimer.singleShot(int(delay * 1000), lambda at=at: QTest.mousePress(window, Qt.LeftButton, Qt.NoModifier, at))
        QTimer.singleShot(int((delay + 0.3 * scale) * 1000),
                          lambda at=at: QTest.mouseRelease(window, Qt.LeftButton, Qt.NoModifier, at))

    def scroll():
        page = find("pageScroll")
        end = page.property("contentHeight") - page.property("height")
        page.setProperty("contentY", max(0, min(args.scroll, end)))

    if args.scroll is not None:
        QTimer.singleShot(int(args.settle * 600), scroll)

    if args.record:
        record(app, window, args, out, scale)
    else:
        delay = args.settle + (1.4 if args.row else 0) + (0.8 if args.search is not None else 0)
        QTimer.singleShot(int(delay * 1000), lambda: (window.grabWindow().save(str(out)), print(out), app.quit()))
        app.exec()


def record(app, window, args, out, scale):
    frames = Path(tempfile.mkdtemp(prefix="speecher-frames-"))
    stamps = []
    start = [None]

    def grab():
        now = time.monotonic()
        if start[0] is None:
            start[0] = now
        t = (now - start[0]) / scale
        if t > args.record:
            timer.stop()
            encode(frames, stamps, out, args.fps)
            shutil.rmtree(frames)
            print(out)
            app.quit()
            return
        path = frames / f"{len(stamps):05d}.png"
        window.grabWindow().save(str(path))
        stamps.append((path, t))

    timer = QTimer()
    timer.timeout.connect(grab)
    QTimer.singleShot(400, lambda: timer.start(max(1, int(1000 * scale / args.fps / 2))))
    app.exec()


def encode(frames, stamps, out, fps):
    # Frames were grabbed at uneven times; the concat list gives each one its
    # real duration, then ffmpeg resamples to a steady frame rate.
    listing = frames / "frames.txt"
    with listing.open("w") as f:
        for (path, t), nxt in zip(stamps, stamps[1:] + [None]):
            duration = (nxt[1] - t) if nxt else 1 / fps
            f.write(f"file '{path}'\nduration {duration:.5f}\n")
        f.write(f"file '{stamps[-1][0]}'\n")
    codec = ["-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "16", "-preset", "slow"] if out.suffix == ".mp4" \
        else ["-c:v", "libvpx-vp9", "-b:v", "0", "-crf", "24"]
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", str(listing),
                    "-vf", f"fps={fps},pad=ceil(iw/2)*2:ceil(ih/2)*2", *codec, str(out)], check=True)


if __name__ == "__main__":
    main()
