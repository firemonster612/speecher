#!/usr/bin/env bash
# Linux tray icon end-to-end test.
#
# Builds an AppImage, starts it as a daemon inside a virtual KDE Wayland
# session with a minimal StatusNotifierWatcher on the session bus, and checks
# over D-Bus that:
#   * the daemon registers a StatusNotifierItem as soon as it starts,
#   * the item's menu offers Start dictation, Settings… and Quit Speecher,
#   * activating Start dictation over dbusmenu starts a session (tooltip and
#     menu flip to the listening state) and the flipped item stops it again,
#   * Quit ends the daemon and the item leaves the bus.
#
# Usage: tests/e2e/linux-tray/run.sh [--reuse-build]
# Requires: appimagetool, patchelf, kwin_wayland, dbus-run-session, bwrap,
#           python3 with GObject introspection (Gio), a Qt 6.8 the AppImage bundles.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="$ROOT_DIR/tests/e2e/linux-tray"
WORK="${E2E_WORK:-$ROOT_DIR/.scratch/tray-flow/linux-e2e}"
TOOLS="$ROOT_DIR/.scratch/update-flow/tools"
export PATH="$TOOLS:$PATH"

mkdir -p "$WORK"
log() { printf '\n=== %s ===\n' "$*"; }

# Same constraints as the other rigs: disk-backed and short (sun_path).
E2E_TMP_BASE="$HOME/.spe-e2e-tray"
rm -rf "$E2E_TMP_BASE"
mkdir -p "$E2E_TMP_BASE/build"
export TMPDIR="$E2E_TMP_BASE/build"

if [[ "${1:-}" != "--reuse-build" || ! -x "$WORK/Speecher.AppImage" ]]; then
  log "building AppImage"
  SPEECHER_BUILD_DIR="$WORK/build" \
  SPEECHER_APPDIR="$WORK/appdir" \
  SPEECHER_OUTPUT_DIR="$WORK/dist" \
  SPEECHER_BUILD_TYPE=Release \
  SPEECHER_APPIMAGE_CMAKE_EXTRA="${SPEECHER_APPIMAGE_CMAKE_EXTRA:--DSPEECHER_E2E_HOOKS=ON -DSPEECHER_WITH_KDE=OFF -DSPEECHER_RELEASE_BUILD=OFF}" \
    bash "$ROOT_DIR/packaging/build-appimage.sh" > "$WORK/build.log" 2>&1
  mv "$WORK"/dist/Speecher*x86_64.AppImage "$WORK/Speecher.AppImage"
  chmod +x "$WORK/Speecher.AppImage"
fi

log "running tray flow"
FLOW_DIR="$WORK/flow-tray"
rm -rf "$FLOW_DIR"
mkdir -p "$FLOW_DIR"/{config,data,cache}
RUNTIME_DIR="/tmp/spe-e2e-tray"
rm -rf "$RUNTIME_DIR"
mkdir -p "$RUNTIME_DIR"
chmod 700 "$RUNTIME_DIR"
export XDG_RUNTIME_DIR="$RUNTIME_DIR"
APP_TMPDIR="$E2E_TMP_BASE/app"
mkdir -p "$APP_TMPDIR"

cp "$WORK/Speecher.AppImage" "$FLOW_DIR/Speecher.AppImage"
chmod +x "$FLOW_DIR/Speecher.AppImage"

export E2E_FLOW_DIR="$FLOW_DIR" E2E_RUNTIME_DIR="$RUNTIME_DIR" \
       E2E_APP_TMPDIR="$APP_TMPDIR" E2E_HERE="$HERE"

overall=0
if dbus-run-session -- kwin_wayland --virtual --width 1280 --height 900 \
     --no-lockscreen --no-global-shortcuts \
     --exit-with-session="$HERE/inner.sh" > "$FLOW_DIR/session.log" 2>&1; then
  if grep -q "E2E-RESULT: PASS" "$FLOW_DIR/driver.log" 2>/dev/null; then
    log "tray flow PASSED"
  else
    log "tray flow FAILED"; overall=1
  fi
else
  log "tray flow session exited non-zero"; overall=1
fi

log "done (exit $overall). Evidence in $FLOW_DIR"
exit "$overall"
