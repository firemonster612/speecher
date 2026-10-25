#!/usr/bin/env bash
# Renders the QML front end with mock data; see preview.py for options.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
. "$here/kwin.sh"
exec "$PY" "$here/preview.py" "$@"
