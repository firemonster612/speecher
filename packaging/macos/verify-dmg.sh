#!/usr/bin/env bash
# Checks that the app in a built DMG opens Main from what the bundle carries:
# the Qt Quick UI loads its Qt QML modules from the bundle alone.
set -euo pipefail

DMG_PATH="${1:?Usage: packaging/macos/verify-dmg.sh DMG}"

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/speecher-verify-dmg.XXXXXX")"
MOUNT_POINT="$WORK_DIR/mount"
APP=""
# Main opens at launch on a profile that has finished setup, so this run
# marks it finished and puts the profile's own value back afterwards.
SETTINGS_DOMAIN=com.io-github-firemonster612.speecher
SETUP_COMPLETED="$(defaults read "$SETTINGS_DOMAIN" app.setupCompleted 2>/dev/null || true)"
cleanup() {
  if [[ -n "$APP" ]]; then
    kill "$APP" 2>/dev/null || true
    wait "$APP" 2>/dev/null || true
  fi
  # defaults reads a bool as 1 or 0 and writes one as true or false.
  if [[ "$SETUP_COMPLETED" == 1 ]]; then
    defaults write "$SETTINGS_DOMAIN" app.setupCompleted -bool true
  elif [[ "$SETUP_COMPLETED" == 0 ]]; then
    defaults write "$SETTINGS_DOMAIN" app.setupCompleted -bool false
  else
    defaults delete "$SETTINGS_DOMAIN" app.setupCompleted 2>/dev/null || true
  fi
  hdiutil detach "$MOUNT_POINT" -quiet 2>/dev/null || true
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT
mkdir "$MOUNT_POINT" "$WORK_DIR/tmp"
hdiutil attach "$DMG_PATH" -readonly -nobrowse -noautoopen -mountpoint "$MOUNT_POINT" -quiet

# SPEECHER_GRAB_MAIN_WINDOW saves Main once it is shown. Its own TMPDIR gives
# it its own socket, apart from a Speecher already running.
defaults write "$SETTINGS_DOMAIN" app.setupCompleted -bool true
SHOT="$WORK_DIR/main.png"
LOG="$HOME/Library/Caches/io.github.firemonster612/speecher/speecher.log"
LOG_LINES="$( (cat "$LOG" 2>/dev/null || true) | wc -l)"
TMPDIR="$WORK_DIR/tmp" SPEECHER_GRAB_MAIN_WINDOW="$SHOT" \
  "$MOUNT_POINT/speecher.app/Contents/MacOS/speecher" > "$WORK_DIR/app.out" 2>&1 &
APP=$!
for _ in $(seq 150); do
  [[ -s "$SHOT" ]] || ! kill -0 "$APP" 2>/dev/null && break
  sleep 0.2
done
QML_ERRORS="$(tail -n +"$((LOG_LINES + 1))" "$LOG" 2>/dev/null | grep ' QML: ' || true)"
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
