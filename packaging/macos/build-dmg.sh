#!/usr/bin/env bash
# Regenerate the background with:
# magick -size 660x400 xc:'#2b2b2b' -font Helvetica -fill white -gravity north -pointsize 28 -annotate +0+45 'Drag Speecher to Applications' -gravity northwest -stroke white -strokewidth 8 -draw 'line 275,210 370,210' -fill white -stroke none -draw 'polygon 370,190 400,210 370,230' PNG24:packaging/macos/dmg-background.png

set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "build-dmg.sh runs on macOS only." >&2
  exit 1
fi

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 BUILD_DIR" >&2
  exit 1
fi

BUILD_DIR="$(cd "$1" && pwd)"
SOURCE_APP="$BUILD_DIR/speecher.app"
OUTPUT_DMG="$BUILD_DIR/speecher.dmg"
LOG="$BUILD_DIR/speecher-dmg.log"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BACKGROUND="$SCRIPT_DIR/dmg-background.png"

if [[ ! -d "$SOURCE_APP" ]]; then
  echo "App bundle not found: $SOURCE_APP" >&2
  exit 1
fi

MACDEPLOYQT="$(command -v macdeployqt || true)"
if [[ -z "$MACDEPLOYQT" ]] && command -v brew >/dev/null 2>&1; then
  QT_PREFIX="$(brew --prefix qt 2>/dev/null || true)"
  MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"
fi
if [[ ! -x "$MACDEPLOYQT" ]]; then
  echo "macdeployqt not found; install Homebrew Qt with: brew install qt" >&2
  exit 1
fi

# The tools this script drives are loud about things that need no attention —
# macdeployqt in particular prints ERROR lines about optional Qt plugins it
# rightly skips, and about a signing pass this script redoes anyway. All of it
# goes to the log; the terminal gets one line per stage, and the log's tail
# only when a stage genuinely fails.
: > "$LOG"
STEP=0
STEPS=7
step() {
  STEP=$((STEP + 1))
  printf '[%d/%d] %s\n' "$STEP" "$STEPS" "$1"
}
run_logged() {
  local description="$1"
  shift
  if ! "$@" >> "$LOG" 2>&1; then
    echo "$description failed. Last lines of $LOG:" >&2
    tail -15 "$LOG" >&2
    exit 1
  fi
}

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/speecher-dmg.XXXXXX")"
STAGING_DIR="$WORK_DIR/staging"
RW_DMG="$WORK_DIR/speecher-rw.dmg"
MOUNT_POINT=""

cleanup() {
  if [[ -n "$MOUNT_POINT" ]]; then
    hdiutil detach "$MOUNT_POINT" -quiet >> "$LOG" 2>&1 || true
  fi
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT

step "Copying the app bundle"
mkdir -p "$STAGING_DIR/.background"
run_logged "Copying the app bundle" ditto "$SOURCE_APP" "$STAGING_DIR/speecher.app"

# Signing identity. The ad-hoc default ("-") makes a launchable bundle, but
# macOS keys the Accessibility grant to the signature, and an ad-hoc
# signature changes on every build — each update then silently invalidates
# the grant while System Settings keeps showing it as on. A stable identity
# (a self-signed code-signing certificate is enough) makes grants survive
# updates: SPEECHER_SIGN_IDENTITY="My Cert Name" make dmg
SIGN_IDENTITY="${SPEECHER_SIGN_IDENTITY:--}"

step "Bundling Qt into the app (the slow part)"
# Without signing the deployed binary and Qt dylibs are unsigned and macOS
# refuses to launch the copy a user drags out of the image.
run_logged "Bundling Qt" "$MACDEPLOYQT" "$STAGING_DIR/speecher.app" -always-overwrite -codesign="$SIGN_IDENTITY"

step "Signing the bundle"
SPARKLE="$STAGING_DIR/speecher.app/Contents/Frameworks/Sparkle.framework"
# Sign Sparkle's XPC services first because they are its innermost code.
run_logged "Signing Sparkle Downloader" codesign --force --sign "$SIGN_IDENTITY" \
  "$SPARKLE/Versions/B/XPCServices/Downloader.xpc"
run_logged "Signing Sparkle InstallerLauncher" codesign --force --sign "$SIGN_IDENTITY" \
  "$SPARKLE/Versions/B/XPCServices/InstallerLauncher.xpc"
# Sign the command-line updater nested inside the framework.
run_logged "Signing Sparkle Autoupdate" codesign --force --sign "$SIGN_IDENTITY" \
  "$SPARKLE/Versions/B/Autoupdate"
# Sign Sparkle's updater app after its own nested code.
run_logged "Signing Sparkle Updater" codesign --force --sign "$SIGN_IDENTITY" \
  "$SPARKLE/Versions/B/Updater.app"
# Seal the framework only after every nested component is signed.
run_logged "Signing Sparkle framework" codesign --force --sign "$SIGN_IDENTITY" "$SPARKLE"
# macdeployqt signs Qt's code; seal the app last without --deep.
run_logged "Signing app" codesign --force --sign "$SIGN_IDENTITY" "$STAGING_DIR/speecher.app"

step "Verifying the signature"
if ! codesign --verify --deep --strict "$STAGING_DIR/speecher.app" >> "$LOG" 2>&1; then
  echo "Signing left the bundle failing codesign --verify --deep --strict; see $LOG" >&2
  tail -15 "$LOG" >&2
  exit 1
fi

step "Creating the disk image"
ln -s /Applications "$STAGING_DIR/Applications"
cp "$BACKGROUND" "$STAGING_DIR/.background/dmg-background.png"
run_logged "Creating the disk image" hdiutil create -volname Speecher \
  -srcfolder "$STAGING_DIR" -fs HFS+ -format UDRW -ov "$RW_DMG"
# Mount under /Volumes: Finder only addresses a disk by name when the system
# picked the mount point, so a custom -mountpoint would break the layout pass.
ATTACH_OUT="$(hdiutil attach "$RW_DMG" -readwrite -noverify -noautoopen 2>> "$LOG")"
MOUNT_POINT="$(sed -n 's|.*\(/Volumes/.*\)$|\1|p' <<<"$ATTACH_OUT" | head -1)"
VOLUME_NAME="$(basename "$MOUNT_POINT")"

step "Arranging the Finder window"
if osascript - "$VOLUME_NAME" >> "$LOG" 2>&1 <<'APPLESCRIPT'
on run argv
tell application "Finder"
  tell disk (item 1 of argv)
    open
    set current view of container window to icon view
    set toolbar visible of container window to false
    set statusbar visible of container window to false
    set sidebar width of container window to 0
    set bounds of container window to {100, 100, 760, 500}

    set viewOptions to the icon view options of container window
    set arrangement of viewOptions to not arranged
    set icon size of viewOptions to 96
    set text size of viewOptions to 14
    set background picture of viewOptions to file ".background:dmg-background.png"

    set position of item "speecher.app" to {180, 210}
    set position of item "Applications" to {480, 210}
    update without registering applications
    delay 2
    close
  end tell
end tell
end run
APPLESCRIPT
then
  sync
else
  echo "      Finder was not scriptable here, so the window keeps its default layout; the DMG is still complete."
  # A layout pass that died mid-script can leave a partial .DS_Store that
  # positions icons outside the window; no layout beats half a layout.
  rm -f "$MOUNT_POINT/.DS_Store"
fi

step "Compressing the image"
if ! hdiutil detach "$MOUNT_POINT" -quiet >> "$LOG" 2>&1; then
  hdiutil detach "$MOUNT_POINT" -force -quiet >> "$LOG" 2>&1
fi
MOUNT_POINT=""
run_logged "Compressing the image" hdiutil convert "$RW_DMG" -format UDZO \
  -imagekey zlib-level=9 -ov -o "$OUTPUT_DMG"

echo "Created $OUTPUT_DMG (full tool output: $LOG)"
