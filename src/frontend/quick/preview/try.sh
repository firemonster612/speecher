#!/usr/bin/env bash
# Opens the front end in a normal window on mock data. Needs only uv
# (https://docs.astral.sh/uv/); the first run downloads PySide6 into uv's cache.
#   ./try.sh [app|setup|popup|gallery] [--theme=dark] [--reduce-motion=true]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
exec uv run --quiet --no-project --python 3.13 \
    --with pyside6-essentials==6.12.0 --with pyside6-addons==6.12.0 \
    python "$here/try.py" "$@"
