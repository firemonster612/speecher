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

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/speecher-dmg.XXXXXX")"
STAGING_DIR="$WORK_DIR/staging"
RW_DMG="$WORK_DIR/speecher-rw.dmg"
MOUNT_POINT=""

cleanup() {
  if [[ -n "$MOUNT_POINT" ]]; then
    hdiutil detach "$MOUNT_POINT" -quiet || true
  fi
  rm -rf "$WORK_DIR"
}
trap cleanup EXIT

mkdir -p "$STAGING_DIR/.background"
ditto "$SOURCE_APP" "$STAGING_DIR/speecher.app"
# Ad-hoc signing: without it the deployed binary and Qt dylibs are unsigned
# and macOS refuses to launch the copy a user drags out of the image.
"$MACDEPLOYQT" "$STAGING_DIR/speecher.app" -always-overwrite -codesign=-
# Homebrew's macdeployqt does not reliably ad-hoc sign the whole tree despite
# -codesign=-, so sign again explicitly: --deep covers the nested frameworks
# and dylibs macdeployqt left unsigned, then the bundle itself.
codesign --force --deep --sign - "$STAGING_DIR/speecher.app"
if ! codesign --verify --deep --strict "$STAGING_DIR/speecher.app"; then
  echo "Ad-hoc signing left $STAGING_DIR/speecher.app failing codesign --verify --deep --strict" >&2
  exit 1
fi
ln -s /Applications "$STAGING_DIR/Applications"
cp "$BACKGROUND" "$STAGING_DIR/.background/dmg-background.png"

hdiutil create -volname Speecher -srcfolder "$STAGING_DIR" -fs HFS+ \
  -format UDRW -ov "$RW_DMG" >/dev/null
# Mount under /Volumes: Finder only addresses a disk by name when the system
# picked the mount point, so a custom -mountpoint would break the layout pass.
ATTACH_OUT="$(hdiutil attach "$RW_DMG" -readwrite -noverify -noautoopen)"
MOUNT_POINT="$(sed -n 's|.*\(/Volumes/.*\)$|\1|p' <<<"$ATTACH_OUT" | head -1)"
VOLUME_NAME="$(basename "$MOUNT_POINT")"

if osascript - "$VOLUME_NAME" <<'APPLESCRIPT'
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
  echo "Note: Finder layout pass was skipped; the DMG contents are still complete." >&2
fi

if ! hdiutil detach "$MOUNT_POINT" -quiet; then
  hdiutil detach "$MOUNT_POINT" -force -quiet
fi
MOUNT_POINT=""
hdiutil convert "$RW_DMG" -format UDZO -imagekey zlib-level=9 \
  -ov -o "$OUTPUT_DMG" >/dev/null

echo "Created $OUTPUT_DMG"
