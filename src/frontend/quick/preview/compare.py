"""Compare two folders of screenshots and report which ones changed.

  python compare.py BEFORE_DIR AFTER_DIR [DIFF_DIR]

Prints, for every PNG in BEFORE_DIR, the share of pixels that differ in the
same-named file in AFTER_DIR, and writes a red-on-grey difference image to
DIFF_DIR for each one that changed. Exits non-zero if any file changed or is
missing. Pixels count as different when a channel moves by more than 8, so
anti-aliasing noise does not register.
"""

import sys
from pathlib import Path

import numpy as np
from PySide6.QtGui import QGuiApplication, QImage

TOLERANCE = 8


def pixels(image: QImage) -> np.ndarray:
    image = image.convertToFormat(QImage.Format_RGB32)
    rows = np.frombuffer(image.constBits(), np.uint8).reshape(image.height(), image.bytesPerLine())
    return rows[:, : image.width() * 4].reshape(image.height(), image.width(), 4)[:, :, :3].astype(np.int16)


def compare(before: QImage, after: QImage):
    """Returns the share of changed pixels and a difference image."""
    if before.size() != after.size():
        return 1.0, None
    a, b = pixels(before), pixels(after)
    mask = (np.abs(a - b) > TOLERANCE).any(axis=2)
    grey = a.mean(axis=2).astype(np.uint8)
    diff = np.dstack([grey, grey, grey])
    diff[mask] = (220, 40, 40)
    diff = np.ascontiguousarray(diff)
    image = QImage(diff.data, diff.shape[1], diff.shape[0], diff.shape[1] * 3, QImage.Format_RGB888).copy()
    return float(mask.mean()), image


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    app = QGuiApplication(sys.argv[:1])  # noqa: F841  (QImage needs a Gui app for some formats)
    before_dir, after_dir = Path(sys.argv[1]), Path(sys.argv[2])
    diff_dir = Path(sys.argv[3]) if len(sys.argv) > 3 else None
    if diff_dir:
        diff_dir.mkdir(parents=True, exist_ok=True)
    failed = False
    for before_path in sorted(before_dir.glob("*.png")):
        after_path = after_dir / before_path.name
        if not after_path.exists():
            print(f"missing  {before_path.name}")
            failed = True
            continue
        share, diff = compare(QImage(str(before_path)), QImage(str(after_path)))
        if share > 0:
            failed = True
            if diff_dir and diff is not None:
                diff.save(str(diff_dir / before_path.name))
        print(f"{'same    ' if share == 0 else 'changed '} {before_path.name} {share:.2%}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
