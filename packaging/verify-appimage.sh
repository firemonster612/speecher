#!/usr/bin/env bash
set -euo pipefail

IMAGE_PATH="${1:?Usage: packaging/verify-appimage.sh APPIMAGE [EXPECTED_VERSION]}"
EXPECTED_VERSION="${2:-}"
IMAGE_PATH="$(readlink -f "$IMAGE_PATH")"
if [[ ! -x "$IMAGE_PATH" ]]; then
  echo "AppImage is missing or not executable: $IMAGE_PATH" >&2
  exit 1
fi

EXTRACT_DIR="$(mktemp -d)"
trap 'rm -rf "$EXTRACT_DIR"' EXIT
(
  cd "$EXTRACT_DIR"
  "$IMAGE_PATH" --appimage-extract >/dev/null
)
APPDIR="$EXTRACT_DIR/squashfs-root"

VERSION_OUTPUT="$(QT_QPA_PLATFORM=offscreen "$APPDIR/AppRun" --version)"
echo "$VERSION_OUTPUT"
VERSION="$(printf '%s\n' "$VERSION_OUTPUT" | sed -n 's/^speecher \(.*\) (build [0-9][0-9]*)$/\1/p')"
if [[ -z "$VERSION" ]]; then
  echo "The packaged application returned an invalid --version response." >&2
  exit 1
fi
if [[ -n "$EXPECTED_VERSION" && "$VERSION" != "$EXPECTED_VERSION" ]]; then
  echo "Packaged version $VERSION does not match expected version $EXPECTED_VERSION." >&2
  exit 1
fi

REQUIRED_FILES=(
  usr/bin/speecher
  usr/bin/wl-copy
  usr/libexec/speecher/speecher-ydotool-setup
  usr/libexec/speecher/speecher-keywatch-setup
  usr/libexec/speecher/speecher-keywatchd
  usr/libexec/speecher/speecher_keywatchd.cil
  usr/libexec/speecher/speecher-wayland-clipboard
  usr/plugins/platforms/libqxcb.so
  usr/plugins/platformthemes/KDEPlasmaPlatformTheme6.so
  usr/plugins/platformthemes/libqgtk3.so
  usr/plugins/platformthemes/libqxdgdesktopportal.so
  usr/plugins/styles/breeze6.so
  usr/plugins/styles/adwaita.so
  usr/plugins/styles/libkvantum.so
  usr/plugins/iconengines/libqsvgicon.so
  usr/plugins/imageformats/libqsvg.so
  usr/plugins/networkinformation/libqnetworkmanager.so
  usr/qml/QtCore/qmldir
  usr/qml/QtQml/qmldir
  usr/qml/QtQuick/qmldir
  usr/qml/QtQuick/Templates/qmldir
  usr/qml/QtQuick/Window/qmldir
  usr/qml/QtQuick/Effects/qmldir
  usr/qml/QtQuick/Shapes/qmldir
  usr/qml/QtQuick/Dialogs/qmldir
  usr/lib/libQt6Core.so.6
  usr/lib/libKF6GlobalAccel.so.6
  usr/lib/libLayerShellQtInterface.so.6
  usr/lib/libqt6keychain.so.1
  usr/lib/libtranscribe.so.0.2
  usr/lib/libggml-cpu-x64.so
  usr/lib/libggml-vulkan.so
  usr/share/icons/breeze/index.theme
)
for required in "${REQUIRED_FILES[@]}"; do
  if [[ ! -e "$APPDIR/$required" ]]; then
    echo "Required AppImage file is missing: $required" >&2
    exit 1
  fi
done
# Qt 6.10 renamed the Wayland platform plugin from libqwayland-generic.so to libqwayland.so.
if [[ ! -e "$APPDIR/usr/plugins/platforms/libqwayland.so" \
   && ! -e "$APPDIR/usr/plugins/platforms/libqwayland-generic.so" ]]; then
  echo "Required AppImage file is missing: usr/plugins/platforms/libqwayland.so" >&2
  exit 1
fi

# The Qt Quick UI loads from the bundled QML modules alone: on a profile that
# has finished setup, a launch opens Main, which SPEECHER_GRAB_MAIN_WINDOW
# saves once it is shown. A host import path is set, which AppRun has to
# drop. Its own socket, profile and session bus keep it apart from a
# Speecher already running.
UI_HOME="$EXTRACT_DIR/ui-home"
mkdir -p "$UI_HOME/config/io.github.firemonster612" "$UI_HOME/tmp"
printf '[app]\nsetupCompleted=true\n' > "$UI_HOME/config/io.github.firemonster612/speecher.conf"
UI_SHOT="$UI_HOME/main.png"
UI_LOG="$UI_HOME/cache/io.github.firemonster612/speecher/speecher.log"
(
  export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software SPEECHER_GRAB_MAIN_WINDOW="$UI_SHOT"
  export XDG_CONFIG_HOME="$UI_HOME/config" XDG_DATA_HOME="$UI_HOME/data" XDG_CACHE_HOME="$UI_HOME/cache"
  export XDG_STATE_HOME="$UI_HOME/state" TMPDIR="$UI_HOME/tmp"
  export QML_IMPORT_PATH="/usr/lib/qt6/qml:/usr/lib/x86_64-linux-gnu/qt6/qml"
  session=()
  command -v dbus-run-session >/dev/null && session=(dbus-run-session --)
  timeout 30 "${session[@]}" bash -c '
    "$1" > "$2/ui.out" 2>&1 &
    for _ in $(seq 100); do [[ -s "$3" ]] && break; sleep 0.2; done
    kill $! 2>/dev/null
  ' _ "$APPDIR/AppRun" "$UI_HOME" "$UI_SHOT"
)
if [[ ! -s "$UI_SHOT" ]] || grep -q ' QML: ' "$UI_LOG"; then
  echo "The packaged Qt Quick UI did not open Main:" >&2
  grep ' QML: ' "$UI_LOG" >&2 || tail -n 20 "$UI_LOG" "$UI_HOME/ui.out" >&2
  exit 1
fi
echo "The packaged Qt Quick UI opened Main."

HELPER="$APPDIR/usr/libexec/speecher/speecher-ydotool-setup"
if ldd "$HELPER" | grep -Fq 'libQt6'; then
  echo "The relocated ydotool setup helper depends on Qt." >&2
  ldd "$HELPER" >&2
  exit 1
fi
mkdir "$EXTRACT_DIR/relocated-helper"
cp "$HELPER" "$EXTRACT_DIR/relocated-helper/"
env -u LD_LIBRARY_PATH "$EXTRACT_DIR/relocated-helper/speecher-ydotool-setup" --help >/dev/null

mkdir "$EXTRACT_DIR/untrusted-launch-directory"
cat > "$EXTRACT_DIR/untrusted-launch-directory/getconf" <<'EOF'
#!/bin/sh
touch app-run-used-current-directory
printf '%s\n' 'glibc 999.0'
EOF
chmod +x "$EXTRACT_DIR/untrusted-launch-directory/getconf"
(
  cd "$EXTRACT_DIR/untrusted-launch-directory"
  env -u PATH QT_QPA_PLATFORM=offscreen "$APPDIR/AppRun" --version >/dev/null
  if [[ -e app-run-used-current-directory ]]; then
    echo "AppRun searched the launch directory when PATH was unset." >&2
    exit 1
  fi
)

ELF_COUNT=0
while IFS= read -r -d '' candidate; do
  if [[ "$(file -b "$candidate")" != ELF* ]]; then
    continue
  fi
  ((ELF_COUNT += 1))
  dependencies="$(ldd "$candidate" 2>&1 || true)"
  if grep -Fq 'not found' <<<"$dependencies"; then
    echo "Unresolved dependency in ${candidate#"$APPDIR"/}:" >&2
    printf '%s\n' "$dependencies" >&2
    exit 1
  fi
done < <(find "$APPDIR/usr" -type f -print0)
if (( ELF_COUNT == 0 )); then
  echo "No ELF files were found in the extracted AppImage." >&2
  exit 1
fi
echo "Verified $ELF_COUNT bundled ELF files."
