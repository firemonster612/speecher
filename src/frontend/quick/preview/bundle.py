"""Packs the front end, its mock data and fonts into a zip to try on another
machine: bundle.py OUT.zip. Unpacked, `bash try.sh` opens it (see TRY.txt,
which becomes the bundle's README)."""

import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOP = "speecher-ui"
FOLDERS = ["qml", "mock", "assets", "preview/qml"]
FILES = {"preview/try.py": "preview/try.py", "preview/try.sh": "preview/try.sh",
         "preview/try.ps1": "preview/try.ps1", "preview/TRY.txt": "README.txt"}
LAUNCHERS = {
    "try.sh": '#!/usr/bin/env bash\n# bash try.sh [app|setup|popup|gallery] [--theme=dark] [--reduce-motion=true]\n'
              'exec bash "$(cd "$(dirname "$0")" && pwd)/preview/try.sh" "$@"\n',
    "try.ps1": '# .\\try.ps1 [app|setup|popup|gallery] [--theme=dark] [--reduce-motion=true]\n'
               '& "$PSScriptRoot\\preview\\try.ps1" @args\n',
}


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else sys.exit(__doc__)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
        for folder in FOLDERS:
            for path in sorted((ROOT / folder).rglob("*")):
                if path.is_file() and "__pycache__" not in path.parts:
                    zf.write(path, f"{TOP}/{path.relative_to(ROOT)}")
        for source, name in FILES.items():
            zf.write(ROOT / source, f"{TOP}/{name}")
        for name, text in LAUNCHERS.items():
            zf.writestr(f"{TOP}/{name}", text)
    print(out)


if __name__ == "__main__":
    main()
