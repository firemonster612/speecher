#!/usr/bin/env bash
# Renders every page in both themes, each setup step and each popup state
# into DIR, for comparing before and after a change with compare.py:
#   preview/snapshot.sh /tmp/before ... make the change ... preview/snapshot.sh /tmp/after
#   ~/.cache/speecher-quick/.venv/bin/python preview/compare.py /tmp/before /tmp/after /tmp/diff
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:?usage: snapshot.sh DIR}"
mkdir -p "$out"
pages=$(grep -oE '\{ id: "[a-zA-Z]+", group:' "$here/../qml/Speecher/Wording/schema/pages.js" | cut -d'"' -f2)
for theme in light dark; do
    for page in $pages; do
        "$here/run.sh" --page "$page" --theme "$theme" --settle 1.6 --out "$out/$page-$theme.png" >/dev/null
    done
    for step in 0 1 2 3 4 5 6 7; do
        "$here/run.sh" --root Speecher.App/SetupAssistant --size 960x680 --theme "$theme" --set step=$step --settle 1.6 \
            --out "$out/setup-$step-$theme.png" >/dev/null
    done
done
for demo in listening paused refining delivered review error; do
    "$here/run.sh" --root SpeecherPreview/PopupScene --size 1200x760 --arg popup-demo=$demo --settle 1.6 \
        --out "$out/popup-$demo.png" >/dev/null
done
echo "$(find "$out" -name '*.png' | wc -l) screenshots in $out"
