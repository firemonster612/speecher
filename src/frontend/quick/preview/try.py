"""Opens the QML front end in an ordinary window, on mock data, to click around.

  try.py            the main window
  try.py setup      the setup assistant
  try.py popup      the dictation popup over a pretend chat app
  try.py gallery    every shared control

Extra --name=value arguments reach QML as they do in preview.py, for example
--theme=dark, --reduce-motion=true or --popup-demo=review.
"""

import sys
from pathlib import Path

from PySide6.QtGui import QFontDatabase, QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine

ROOT = Path(__file__).resolve().parent.parent
ROOTS = {
    "app": ("Speecher.App", "Main"),
    "setup": ("Speecher.App", "SetupAssistant"),
    "popup": ("SpeecherPreview", "PopupScene"),
    "gallery": ("SpeecherPreview", "Gallery"),
}


def main():
    names = [a for a in sys.argv[1:] if not a.startswith("--")]
    which = names[0] if names else "app"
    if which not in ROOTS:
        sys.exit(f"unknown view {which!r}; pick one of {', '.join(ROOTS)}")

    app = QGuiApplication([sys.argv[0], *[a for a in sys.argv[1:] if a.startswith("--")]])
    app.setApplicationName("Speecher (UI preview)")
    for font in sorted((ROOT / "assets").rglob("*.ttf")):
        QFontDatabase.addApplicationFont(str(font))

    engine = QQmlApplicationEngine()
    for path in ("qml", "mock", "preview/qml"):
        engine.addImportPath(str(ROOT / path))
    engine.loadFromModule(*ROOTS[which])
    if not engine.rootObjects():
        sys.exit("the QML failed to load; see the messages above")
    window = engine.rootObjects()[0]
    # As the app does, so the Theme setting switches the theme.
    engine.loadFromModule("Speecher.App", "ThemeSetting")
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
