"""Load every page, setup step and popup state, and fail on any QML warning.

Runs inside the headless KWin that run.sh starts (use check.sh). Each
configuration runs in its own process, since the theme and mock states come
from the command line. Prints each warning with where it happened and exits
non-zero if there were any.
"""

import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
STEP_MS = 700

PAGES = re.findall(r'\{ id: "(\w+)", group:', (ROOT / "mock/Speecher/Backend/schema/pages.js").read_text())
SEARCHES = ["shortcut", "api key", "paste", "zzz no match"]
POPUP_DEMOS = ["listening", "paused", "refining", "delivered", "review", "error"]
SETUP_STEPS = 8

# (root, steps, arguments) for every run.
RUNS = [(root, steps, [f"--theme={theme}"])
        for theme in ("light", "dark")
        for root, steps in (("Speecher.App/Main", "pages"), ("Speecher.App/SetupAssistant", "setup"))]
RUNS += [("SpeecherPreview/PopupScene", "settle", [f"--popup-demo={demo}"]) for demo in POPUP_DEMOS]
RUNS += [("SpeecherPreview/Gallery", "settle", [f"--theme={theme}"]) for theme in ("light", "dark")]


def one(root, steps, args):
    """Runs one configuration and prints its warnings as WARN lines."""
    from PySide6.QtCore import QObject, QTimer
    from PySide6.QtGui import QFontDatabase, QGuiApplication
    from PySide6.QtQml import QQmlApplicationEngine
    from PySide6.QtQuick import QQuickWindow  # noqa: F401  (lets rootObjects() return windows)

    app = QGuiApplication([sys.argv[0], *args])
    for font in sorted((ROOT / "assets").rglob("*.ttf")):
        QFontDatabase.addApplicationFont(str(font))
    engine = QQmlApplicationEngine()
    for path in ("qml", "mock", "preview/qml"):
        engine.addImportPath(str(ROOT / path))
    where = ["load"]
    engine.warnings.connect(lambda ws: [print(f"WARN {where[0]}: {w.toString()}", flush=True) for w in ws])
    engine.loadFromModule(*root.split("/"))
    if not engine.rootObjects():
        print("WARN load: failed to load", flush=True)
        return
    window = engine.rootObjects()[0]

    def navigate(page):
        engine.singletonInstance("Speecher.App", "Navigation").setProperty("current", page)

    def search(query):
        palette = window.findChild(QObject, "searchPalette")
        palette.open()
        palette.setProperty("query", query)

    if steps == "pages":
        queue = [(f"page {p}", lambda p=p: navigate(p)) for p in PAGES]
        queue += [(f"search {q!r}", lambda q=q: search(q)) for q in SEARCHES]
        queue += [("close search", lambda: window.findChild(QObject, "searchPalette").close())]
    elif steps == "setup":
        queue = [(f"step {n}", lambda n=n: window.setProperty("step", n)) for n in range(SETUP_STEPS)]
    else:
        queue = [("settle", lambda: None)]

    def advance():
        if not queue:
            app.quit()
            return
        where[0], action = queue.pop(0)
        action()
        QTimer.singleShot(STEP_MS, advance)

    QTimer.singleShot(STEP_MS, advance)
    app.exec()


def main():
    found = []
    for root, steps, args in RUNS:
        label = f"{root} {' '.join(args)}"
        result = subprocess.run([sys.executable, __file__, "--one", root, steps, *args],
                                capture_output=True, text=True, timeout=120)
        lines = [l[5:] for l in result.stdout.splitlines() if l.startswith("WARN ")]
        if result.returncode != 0:
            lines.append(f"exited with {result.returncode}: {result.stderr.strip().splitlines()[-1:]}")
        found += [f"{label}, {l}" for l in lines]
        print(f"{'ok  ' if not lines else 'FAIL'} {label} ({len(lines)})", flush=True)
    for line in found:
        print(line)
    print(f"{len(found)} warning(s): {len(PAGES)} pages and search in both themes, "
          f"{SETUP_STEPS} setup steps in both themes, {len(POPUP_DEMOS)} popup states, the gallery in both themes")
    sys.exit(1 if found else 0)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--one":
        one(sys.argv[2], sys.argv[3], sys.argv[4:])
    else:
        main()
