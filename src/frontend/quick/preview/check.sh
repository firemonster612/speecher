#!/usr/bin/env bash
# The front end's gate: qmllint must report nothing, and every page, setup
# step and popup state must load without a QML warning.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
. "$here/kwin.sh"
lint="$(dirname "$PY")/pyside6-qmllint"

echo "qmllint"
# Absolute paths: given relative ones, qmllint reports singletons as missing
# from qmldir. Findings print relative to the tree.
mapfile -t files < <(find "$root/qml" "$root/mock" "$root/preview/qml" -name '*.qml' | sort)
if ! report="$("$lint" -I "$root/qml" -I "$root/mock" -I "$root/preview/qml" "${files[@]}" 2>&1 | sed "s|$root/||g")" || [ -n "$report" ]; then
    echo "$report"
    echo "qmllint: $(grep -c '^Warning\|^Error' <<<"$report" || true) finding(s)"
    lint_failed=1
fi

echo "loading"
"$PY" "$here/check.py" || load_failed=1
[ -z "${lint_failed:-}" ] && [ -z "${load_failed:-}" ]
