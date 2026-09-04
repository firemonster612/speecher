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
  usr/libexec/speecher/speecher-wayland-clipboard
  usr/plugins/platforms/libqxcb.so
  usr/plugins/platforms/libqwayland-generic.so
  usr/plugins/platformthemes/KDEPlasmaPlatformTheme6.so
  usr/plugins/platformthemes/libqgtk3.so
  usr/plugins/platformthemes/libqxdgdesktopportal.so
  usr/plugins/styles/breeze6.so
  usr/plugins/iconengines/libqsvgicon.so
  usr/lib/libQt6Core.so.6
  usr/lib/libKF6ColorScheme.so.6
  usr/lib/libKF6GlobalAccel.so.6
  usr/lib/libKF6WidgetsAddons.so.6
  usr/lib/libLayerShellQtInterface.so.6
  usr/lib/libqt6keychain.so.1
  usr/share/icons/breeze/index.theme
)
for required in "${REQUIRED_FILES[@]}"; do
  if [[ ! -e "$APPDIR/$required" ]]; then
    echo "Required AppImage file is missing: $required" >&2
    exit 1
  fi
done

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
