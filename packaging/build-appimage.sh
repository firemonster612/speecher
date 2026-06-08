#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${SPEECHER_BUILD_DIR:-"$ROOT_DIR/build-appimage"}"
APPDIR_PATH="${SPEECHER_APPDIR:-"$ROOT_DIR/dist/AppDir"}"
OUTPUT_DIR="${SPEECHER_OUTPUT_DIR:-"$ROOT_DIR/dist"}"
BUILD_TYPE="${SPEECHER_BUILD_TYPE:-RelWithDebInfo}"
BUNDLE_WL_CLIPBOARD="${BUNDLE_WL_CLIPBOARD:-1}"

usage() {
  cat <<EOF
Usage: packaging/build-appimage.sh [options]

Options:
  --build-dir PATH       CMake build directory. Default: ./build-appimage
  --appdir PATH          AppDir staging directory. Default: ./dist/AppDir
  --output-dir PATH      Output directory. Default: ./dist
  --no-bundle-wl-clipboard
                         Do not bundle wl-copy even if available.
  --help                 Show this help.

Environment:
  SPEECHER_BUILD_DIR    CMake build directory. Default: ./build-appimage
  SPEECHER_APPDIR       AppDir staging directory. Default: ./dist/AppDir
  SPEECHER_OUTPUT_DIR   Output directory. Default: ./dist
  SPEECHER_BUILD_TYPE   CMake build type. Default: RelWithDebInfo
  ARCH                   AppImage arch. Default: x86_64
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --appdir)
      APPDIR_PATH="$2"
      shift 2
      ;;
    --output-dir)
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --no-bundle-wl-clipboard)
      BUNDLE_WL_CLIPBOARD=0
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

require_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required tool: $1" >&2
    exit 1
  fi
}

require_tool cmake
require_tool appimagetool
require_tool qmake6
require_tool ldd

mkdir -p "$OUTPUT_DIR"
rm -rf "$APPDIR_PATH"

echo "Configuring AppImage build in $BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DSPEECHER_DESKTOP_EXEC=speecher \
  -DSPEECHER_BUILD_TESTS=OFF
echo "Compiling speecher"
cmake --build "$BUILD_DIR" --parallel
echo "Installing into AppDir at $APPDIR_PATH"
DESTDIR="$APPDIR_PATH" cmake --install "$BUILD_DIR" --prefix /usr

if [[ "$BUNDLE_WL_CLIPBOARD" == "1" ]] && command -v wl-copy >/dev/null 2>&1; then
  echo "Bundling wl-copy"
  install -Dm755 "$(command -v wl-copy)" "$APPDIR_PATH/usr/bin/wl-copy"
fi

mkdir -p "$APPDIR_PATH/usr/lib" "$APPDIR_PATH/usr/plugins"

skip_library() {
  local name
  name="$(basename "$1")"
  case "$name" in
    ld-linux*|linux-vdso*|libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libresolv.so*|libutil.so*|libnss_*.so*|libcrypt.so*|libblkid.so*|libmount.so*|libsasl2.so*|libevent*.so*|libunistring.so*|libGL*.so*|libEGL*.so*|libOpenGL*.so*|libwayland-client.so*|libxcb.so*|libX11.so*|libfontconfig.so*|libfreetype.so*|libharfbuzz.so*)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

copy_library() {
  local src="$1"
  [[ -e "$src" ]] || return 0
  skip_library "$src" && return 0
  local real
  real="$(readlink -f "$src")"
  local dest="$APPDIR_PATH/usr/lib/$(basename "$src")"
  [[ -e "$dest" ]] || cp -aL "$real" "$dest"
}

copy_deps_for_elf() {
  local elf="$1"
  ldd "$elf" 2>/dev/null | awk '
    /=> \// { print $(NF - 1) }
    /^\// { print $1 }
  ' | while read -r lib; do
    copy_library "$lib"
  done
}

copy_deps_closure() {
  local before after
  copy_deps_for_elf "$APPDIR_PATH/usr/bin/speecher"
  [[ -x "$APPDIR_PATH/usr/bin/wl-copy" ]] && copy_deps_for_elf "$APPDIR_PATH/usr/bin/wl-copy"
  while true; do
    before="$(find "$APPDIR_PATH/usr/lib" -type f | wc -l)"
    while IFS= read -r elf; do
      copy_deps_for_elf "$elf"
    done < <(find "$APPDIR_PATH/usr/lib" -type f)
    after="$(find "$APPDIR_PATH/usr/lib" -type f | wc -l)"
    [[ "$before" == "$after" ]] && break
  done
}

copy_plugin_dir() {
  local name="$1"
  local qt_plugins
  qt_plugins="$(qmake6 -query QT_INSTALL_PLUGINS)"
  [[ -d "$qt_plugins/$name" ]] || return 0
  echo "Copying Qt plugin directory: $name"
  mkdir -p "$APPDIR_PATH/usr/plugins/$name"
  cp -aL "$qt_plugins/$name"/. "$APPDIR_PATH/usr/plugins/$name/"
}

copy_plugin_dir platforms
copy_plugin_dir platformthemes
copy_plugin_dir styles
copy_plugin_dir tls
copy_plugin_dir multimedia
copy_plugin_dir platforminputcontexts
copy_plugin_dir imageformats
copy_plugin_dir xcbglintegrations
copy_plugin_dir wayland-decoration-client
copy_plugin_dir wayland-graphics-integration-client
copy_plugin_dir wayland-shell-integration

echo "Copying runtime library dependencies"
copy_deps_closure
echo "Copying Qt plugin library dependencies"
while IFS= read -r plugin; do
  copy_deps_for_elf "$plugin"
done < <(find "$APPDIR_PATH/usr/plugins" -type f -name '*.so')
echo "Finishing runtime dependency closure"
copy_deps_closure

echo "Writing AppImage runtime files"
cat > "$APPDIR_PATH/usr/bin/qt.conf" <<'EOF'
[Paths]
Plugins = ../plugins
EOF

cat > "$APPDIR_PATH/AppRun" <<'EOF'
#!/usr/bin/env bash
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PATH="$HERE/usr/bin:${PATH:-}"
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/usr/plugins/platforms"
exec "$HERE/usr/bin/speecher" "$@"
EOF
chmod +x "$APPDIR_PATH/AppRun"

ln -sf usr/share/applications/local.speecher.desktop "$APPDIR_PATH/local.speecher.desktop"
ln -sf usr/share/icons/hicolor/scalable/apps/local.speecher.svg "$APPDIR_PATH/local.speecher.svg"

ARCH="${ARCH:-x86_64}"
APPIMAGE_PATH="$OUTPUT_DIR/Speecher-${ARCH}.AppImage"
echo "Building AppImage with appimagetool: $APPIMAGE_PATH"
appimagetool -n "$APPDIR_PATH" "$APPIMAGE_PATH"

echo "Created $APPIMAGE_PATH"
