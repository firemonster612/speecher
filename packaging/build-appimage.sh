#!/usr/bin/env bash
# Nightly Build: SPEECHER_UPDINFO='gh-releases-zsync|firemonster612|speecher|nightly|Speecher-*x86_64.AppImage.zsync'
# Stable Release: SPEECHER_UPDINFO='gh-releases-zsync|firemonster612|speecher|latest|Speecher-*x86_64.AppImage.zsync'
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
  SPEECHER_QT_PREFIX    Qt installation prefix to build and bundle against.
                        Falls back to QT_ROOT_DIR. Without either, CMake's
                        default discovery applies (risky on hosts with a
                        system Qt).
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
require_tool file
require_tool ldd
require_tool ninja
require_tool patchelf

BUILD_DIR="$(readlink -m -- "$BUILD_DIR")"
APPDIR_PATH="$(readlink -m -- "$APPDIR_PATH")"
OUTPUT_DIR="$(readlink -m -- "$OUTPUT_DIR")"
APPDIR_MARKER="$APPDIR_PATH/.speecher-appdir-staging"
case "$APPDIR_PATH" in
  /|"$ROOT_DIR"|"$BUILD_DIR"|"$OUTPUT_DIR"|"$HOME")
    echo "Refusing unsafe AppDir staging path: $APPDIR_PATH" >&2
    exit 1
    ;;
esac
if [[ -e "$APPDIR_PATH" && "$APPDIR_PATH" != "$ROOT_DIR/dist/AppDir"
      && ! -f "$APPDIR_MARKER" ]]; then
  echo "Refusing to replace an unmarked AppDir staging path: $APPDIR_PATH" >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR"
rm -rf -- "$APPDIR_PATH"
mkdir -p "$APPDIR_PATH"
printf 'Speecher AppDir staging directory\n' > "$APPDIR_MARKER"

# Pin Qt discovery at configure time; ambient discovery can mix a system Qt
# into the build (SPEECHER_QT_PREFIX wins, QT_ROOT_DIR is what CI's Qt action exports).
QT_PREFIX_HINT="${SPEECHER_QT_PREFIX:-${QT_ROOT_DIR:-}}"
CMAKE_CONFIGURE_ARGS=(
  -G Ninja
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
  -DSPEECHER_DESKTOP_EXEC=speecher
  -DSPEECHER_BUILD_TESTS=OFF
  -DSPEECHER_RELEASE_BUILD=ON
  -DSPEECHER_WITH_KDE=ON
)
if [[ -n "$QT_PREFIX_HINT" ]]; then
  CMAKE_CONFIGURE_ARGS+=("-DCMAKE_PREFIX_PATH=$QT_PREFIX_HINT")
fi
echo "Configuring AppImage build in $BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" "${CMAKE_CONFIGURE_ARGS[@]}"
echo "Compiling speecher"
cmake --build "$BUILD_DIR" --parallel
echo "Installing into AppDir at $APPDIR_PATH"
DESTDIR="$APPDIR_PATH" cmake --install "$BUILD_DIR" --prefix /usr

# Bundle the Qt the build actually linked against. The installed binary has no
# RPATH and the host may carry a different system Qt, so both the ldd closure
# and qmake must be pinned to the Qt recorded in the build's CMake cache.
QT_CMAKE_DIR="$(sed -n 's/^Qt6_DIR:PATH=//p' "$BUILD_DIR/CMakeCache.txt" | head -n1)"
if [[ -z "$QT_CMAKE_DIR" || ! -d "$QT_CMAKE_DIR" ]]; then
  echo "Could not resolve Qt6_DIR from $BUILD_DIR/CMakeCache.txt" >&2
  exit 1
fi
QT_LIB_DIR="$(readlink -f "$QT_CMAKE_DIR/../..")"
QMAKE=""
for candidate in "$QT_CMAKE_DIR/../../../bin/qmake6" "$QT_CMAKE_DIR/../../../bin/qmake" \
                 "$QT_CMAKE_DIR/../../../../bin/qmake6" "$QT_CMAKE_DIR/../../../../bin/qmake" \
                 "$(command -v qmake6 || true)"; do
  [[ -n "$candidate" && -x "$candidate" ]] || continue
  if [[ "$(readlink -f "$("$candidate" -query QT_INSTALL_LIBS)")" == "$QT_LIB_DIR" ]]; then
    QMAKE="$(readlink -f "$candidate")"
    break
  fi
done
if [[ -z "$QMAKE" ]]; then
  echo "Could not find a qmake belonging to the build Qt at $QT_LIB_DIR" >&2
  exit 1
fi
echo "Bundling Qt from $QT_LIB_DIR (qmake: $QMAKE)"

if [[ "$BUNDLE_WL_CLIPBOARD" == "1" ]] && command -v wl-copy >/dev/null 2>&1; then
  echo "Bundling wl-copy"
  install -Dm755 "$(command -v wl-copy)" "$APPDIR_PATH/usr/bin/wl-copy"
fi

mkdir -p "$APPDIR_PATH/usr/lib" "$APPDIR_PATH/usr/plugins"

skip_library() {
  local name
  name="$(basename "$1")"
  case "$name" in
    ld-linux*|linux-vdso*|libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libresolv.so*|libutil.so*|libnss_*.so*|libcrypt.so*|libGL*.so*|libEGL*.so*|libOpenGL*.so*|libwayland-client.so*|libxcb.so*|libX11.so*|libfontconfig.so*|libfreetype.so*|libharfbuzz.so*)
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
  local dependencies
  dependencies="$(LD_LIBRARY_PATH="$QT_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ldd "$elf" 2>&1 || true)"
  if grep -Fq 'not found' <<<"$dependencies"; then
    echo "Unresolved dependency while bundling $elf:" >&2
    printf '%s\n' "$dependencies" >&2
    exit 1
  fi
  printf '%s\n' "$dependencies" | awk '
    /=> \// { print $(NF - 1) }
    /^\// { print $1 }
  ' | while read -r lib; do
    copy_library "$lib"
  done
}

verify_elf_dependencies() {
  local elf dependencies count=0
  while IFS= read -r -d '' elf; do
    [[ "$(file -b "$elf")" == ELF* ]] || continue
    ((count += 1))
    dependencies="$(ldd "$elf" 2>&1 || true)"
    if grep -Fq 'not found' <<<"$dependencies"; then
      echo "Unresolved dependency in ${elf#"$APPDIR_PATH"/}:" >&2
      printf '%s\n' "$dependencies" >&2
      exit 1
    fi
  done < <(find "$APPDIR_PATH/usr" -type f -print0)
  echo "Verified dependencies for $count bundled ELF files"
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
  qt_plugins="$("$QMAKE" -query QT_INSTALL_PLUGINS)"
  [[ -d "$qt_plugins/$name" ]] || return 0
  echo "Copying Qt plugin directory: $name"
  mkdir -p "$APPDIR_PATH/usr/plugins/$name"
  cp -aL "$qt_plugins/$name"/. "$APPDIR_PATH/usr/plugins/$name/"
}

copy_plugin_dir platforms
copy_plugin_dir platformthemes
copy_plugin_dir styles
copy_plugin_dir iconengines
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

BREEZE_ICON_DIR="/usr/share/icons/breeze"
if [[ ! -f "$BREEZE_ICON_DIR/index.theme" ]]; then
  echo "The breeze-icon-theme package is required to build the AppImage" >&2
  exit 1
fi
echo "Copying Breeze fallback icon theme"
mkdir -p "$APPDIR_PATH/usr/share/icons"
cp -a "$BREEZE_ICON_DIR" "$APPDIR_PATH/usr/share/icons/"

KVANTUM_THEME_DIR="/usr/share/Kvantum"
if [[ ! -d "$KVANTUM_THEME_DIR" ]]; then
  echo "The qt6-style-kvantum package is missing its themes" >&2
  exit 1
fi
echo "Copying bundled Kvantum themes"
cp -a "$KVANTUM_THEME_DIR" "$APPDIR_PATH/usr/share/"

if [[ ! -f "$APPDIR_PATH/usr/lib/libQt6Core.so.6" ]]; then
  echo "libQt6Core.so.6 was not bundled; the dependency closure is broken" >&2
  exit 1
fi
if ! cmp -s "$APPDIR_PATH/usr/lib/libQt6Core.so.6" "$(readlink -f "$QT_LIB_DIR/libQt6Core.so.6")"; then
  echo "Bundled libQt6Core.so.6 does not match $QT_LIB_DIR — mixed Qt closure" >&2
  exit 1
fi

set_runpath_for_tree() {
  local tree="$1"
  local runpath="$2"
  [[ -d "$tree" ]] || return 0
  while IFS= read -r -d '' elf; do
    if [[ "$(file -b "$elf")" == ELF* ]]; then
      patchelf --set-rpath "$runpath" "$elf"
    fi
  done < <(find "$tree" -type f -print0)
}

echo "Writing relative RUNPATHs"
set_runpath_for_tree "$APPDIR_PATH/usr/bin" '$ORIGIN/../lib'
set_runpath_for_tree "$APPDIR_PATH/usr/libexec/speecher" '$ORIGIN/../../lib'
set_runpath_for_tree "$APPDIR_PATH/usr/plugins" '$ORIGIN/../../lib'
set_runpath_for_tree "$APPDIR_PATH/usr/lib" '$ORIGIN'

echo "Checking bundled ELF dependencies"
verify_elf_dependencies

echo "Writing AppImage runtime files"
cat > "$APPDIR_PATH/usr/bin/qt.conf" <<'EOF'
[Paths]
Plugins = ../plugins
EOF

cat > "$APPDIR_PATH/AppRun" <<'EOF'
#!/usr/bin/env bash
INHERITED_PATH="${PATH-}"
PATH=
IFS=: read -r -a inherited_path_parts <<< "$INHERITED_PATH"
for path_part in "${inherited_path_parts[@]}"; do
  [[ "$path_part" == /* ]] && PATH="${PATH:+$PATH:}$path_part"
done
PATH="${PATH:-/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin}"
export PATH
SOURCE_DIR="${BASH_SOURCE[0]%/*}"
[[ "$SOURCE_DIR" == "${BASH_SOURCE[0]}" ]] && SOURCE_DIR=.
HERE="$(cd "$SOURCE_DIR" && pwd -P)" || exit 1
export PATH="$HERE/usr/bin:$PATH"
GLIBC_VERSION="$(getconf GNU_LIBC_VERSION 2>/dev/null || true)"
GLIBC_VERSION="${GLIBC_VERSION##* }"
if [[ "$GLIBC_VERSION" =~ ^([0-9]+)\.([0-9]+) ]] \
    && (( BASH_REMATCH[1] < 2 || (BASH_REMATCH[1] == 2 && BASH_REMATCH[2] < 41) )); then
  echo "Speecher's AppImage requires glibc 2.41 or newer (Debian 13, Fedora 42, Ubuntu 25.04, or later)." >&2
  exit 1
fi
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/usr/plugins/platforms"
export XDG_DATA_DIRS="${XDG_DATA_DIRS:-/usr/local/share:/usr/share}:$HERE/usr/share"
# Use Plasma's platform integration on KDE unless the user selected another one.
if [[ -z "${QT_QPA_PLATFORMTHEME:-}" && "${XDG_CURRENT_DESKTOP:-}" == *KDE* ]]; then
  export QT_QPA_PLATFORMTHEME=kde
fi
exec "$HERE/usr/bin/speecher" "$@"
EOF
chmod +x "$APPDIR_PATH/AppRun"

ln -sf usr/share/applications/io.github.firemonster612.speecher.desktop "$APPDIR_PATH/io.github.firemonster612.speecher.desktop"
ln -sf usr/share/icons/hicolor/scalable/apps/io.github.firemonster612.speecher.svg "$APPDIR_PATH/io.github.firemonster612.speecher.svg"

ARCH="${ARCH:-x86_64}"
APPIMAGE_PATH="$(cd "$OUTPUT_DIR" && pwd -P)/Speecher-${ARCH}.AppImage"
echo "Building AppImage with appimagetool: $APPIMAGE_PATH"
APPIMAGETOOL_ARGS=(-n)
if [[ -n "${SPEECHER_UPDINFO:-}" ]]; then
  APPIMAGETOOL_ARGS+=(-u "$SPEECHER_UPDINFO")
fi
(
  cd "$OUTPUT_DIR"
  appimagetool "${APPIMAGETOOL_ARGS[@]}" "$APPDIR_PATH" "$APPIMAGE_PATH"
)

echo "Created $APPIMAGE_PATH"
if [[ -n "${SPEECHER_UPDINFO:-}" ]]; then
  ZSYNC_PATH="${APPIMAGE_PATH}.zsync"
  if [[ ! -f "$ZSYNC_PATH" ]]; then
    echo "appimagetool did not create expected zsync file: $ZSYNC_PATH" >&2
    exit 1
  fi
  echo "Created $ZSYNC_PATH"
fi
