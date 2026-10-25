# Sourced by run.sh and check.sh: starts a private headless KWin if one is not
# running, and points Qt at it so nothing appears on the desktop. Sets PY to
# a Python with PySide6 (SPEECHER_PREVIEW_PY, default
# ~/.cache/speecher-quick/.venv/bin/python).
PY="${SPEECHER_PREVIEW_PY:-$HOME/.cache/speecher-quick/.venv/bin/python}"
# Big enough for the 1640x1600 Gallery; a window cannot outgrow the screen.
screen="1700x1700"
socket="speecher-preview-$screen"

if [ ! -S "$XDG_RUNTIME_DIR/$socket" ]; then
    (dbus-run-session -- kwin_wayland --virtual --no-lockscreen --width ${screen%x*} --height ${screen#*x} --scale 2 \
        --socket "$socket" >"${TMPDIR:-/tmp}/speecher-preview-kwin.log" 2>&1 &)
    for _ in $(seq 50); do [ -S "$XDG_RUNTIME_DIR/$socket" ] && break; sleep 0.1; done
fi

export QT_QPA_PLATFORM=wayland WAYLAND_DISPLAY="$socket" QT_WAYLAND_DISABLE_WINDOWDECORATION=1
export QT_SCALE_FACTOR="${SPEECHER_PREVIEW_SCALE:-2}"
