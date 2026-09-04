#!/usr/bin/env bash
set -euo pipefail

if [[ "${1:-}" != "--session" ]]; then
  BUILD_DIR="$(readlink -f "${1:-build}")"
  exec dbus-run-session -- "$0" --session "$BUILD_DIR"
fi

BUILD_DIR="${2:?Missing build directory}"
RUNTIME_DIR="$(mktemp -d)"
chmod 700 "$RUNTIME_DIR"
export XDG_RUNTIME_DIR="$RUNTIME_DIR"
export WAYLAND_DISPLAY=speecher-ci
export QT_QPA_PLATFORM=wayland
export QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1

kwin_wayland --virtual --socket "$WAYLAND_DISPLAY" --no-lockscreen \
  --no-global-shortcuts --no-kactivities >"$RUNTIME_DIR/kwin.log" 2>&1 &
KWIN_PID=$!
FIXTURE_PID=
cleanup() {
  status=$?
  [[ -n "$FIXTURE_PID" ]] && kill "$FIXTURE_PID" 2>/dev/null || true
  kill "$KWIN_PID" 2>/dev/null || true
  wait "$FIXTURE_PID" 2>/dev/null || true
  wait "$KWIN_PID" 2>/dev/null || true
  if (( status != 0 )); then
    sed -n '1,240p' "$RUNTIME_DIR/kwin.log" >&2
    [[ -f "$RUNTIME_DIR/fixture.log" ]] && sed -n '1,240p' "$RUNTIME_DIR/fixture.log" >&2
  fi
  rm -rf -- "$RUNTIME_DIR"
  exit "$status"
}
trap cleanup EXIT

for _ in {1..50}; do
  [[ -S "$RUNTIME_DIR/$WAYLAND_DISPLAY" ]] && break
  sleep 0.1
done
if [[ ! -S "$RUNTIME_DIR/$WAYLAND_DISPLAY" ]]; then
  echo "KWin did not create its Wayland socket." >&2
  exit 1
fi

gdbus call --session --dest org.a11y.Bus --object-path /org/a11y/bus \
  --method org.a11y.Bus.GetAddress >/dev/null
"$BUILD_DIR/speecher_atspi_fixture" >"$RUNTIME_DIR/fixture.log" 2>&1 &
FIXTURE_PID=$!
sleep 1

SPEECHER_TEST_ONLY_PLATFORM_LIVE=1 \
SPEECHER_TEST_LIVE_ATSPI=1 \
SPEECHER_TEST_LIVE_CLIPBOARD_FALLBACK=1 \
  "$BUILD_DIR/speecher_tests"
