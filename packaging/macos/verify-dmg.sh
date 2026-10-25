#!/usr/bin/env bash
# Checks that the app in a built DMG opens Main from what the bundle carries:
# the Qt Quick UI loads its Qt QML modules from the bundle alone.
set -euo pipefail

DMG_PATH="${1:?Usage: packaging/macos/verify-dmg.sh DMG}"

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/speecher-verify-dmg.XXXXXX")"
MOUNT_POINT="$WORK_DIR/mount"
APP=""
# The run is an isolated instance, with settings, data and a socket of its
# own, marked as having finished setup so that Main opens at launch. The
# person's own Speecher, running or not, is left alone.
export SPEECHER_INSTANCE="verify-dmg-$$"
SETTINGS_DOMAIN="com.io-github-firemonster612.speecher-$SPEECHER_INSTANCE"
APP_DIRS=(
  "$HOME/Library/Application Support/io.github.firemonster612/speecher-$SPEECHER_INSTANCE"
  "$HOME/Library/Caches/io.github.firemonster612/speecher-$SPEECHER_INSTANCE"
)
cleanup() {
  if [[ -n "$APP" ]]; then
    kill "$APP" 2>/dev/null || true
    wait "$APP" 2>/dev/null || true
  fi
  defaults delete "$SETTINGS_DOMAIN" 2>/dev/null || true
  # cfprefsd leaves the domain's emptied plist behind.
  rm -rf "${APP_DIRS[@]}" "$HOME/Library/Preferences/$SETTINGS_DOMAIN.plist"
  hdiutil detach "$MOUNT_POINT" -quiet 2>/dev/null || true
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT
mkdir "$MOUNT_POINT"
hdiutil attach "$DMG_PATH" -readonly -nobrowse -noautoopen -mountpoint "$MOUNT_POINT" -quiet

# The licenses of the fonts the UI carries.
for license in LICENSE-AtkinsonHyperlegibleNext.txt LICENSE-Lucide.txt; do
  if [[ ! -f "$MOUNT_POINT/speecher.app/Contents/Resources/$license" ]]; then
    echo "The app bundle is missing $license." >&2
    exit 1
  fi
done

defaults write "$SETTINGS_DOMAIN" app.setupCompleted -bool true
# SPEECHER_GRAB_MAIN_WINDOW saves Main once it is shown.
SHOT="$WORK_DIR/main.png"
LOG="${APP_DIRS[1]}/speecher.log"
SPEECHER_GRAB_MAIN_WINDOW="$SHOT" \
  "$MOUNT_POINT/speecher.app/Contents/MacOS/speecher" > "$WORK_DIR/app.out" 2>&1 &
APP=$!
for _ in $(seq 150); do
  [[ -s "$SHOT" ]] || ! kill -0 "$APP" 2>/dev/null && break
  sleep 0.2
done
QML_ERRORS="$(grep ' QML: ' "$LOG" 2>/dev/null || true)"
if [[ ! -s "$SHOT" || -n "$QML_ERRORS" ]]; then
  echo "The packaged Qt Quick UI did not open Main:" >&2
  if [[ -n "$QML_ERRORS" ]]; then
    echo "$QML_ERRORS" >&2
  else
    tail -n 20 "$WORK_DIR/app.out" >&2
  fi
  exit 1
fi
echo "The packaged Qt Quick UI opened Main."
