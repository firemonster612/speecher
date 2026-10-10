#!/usr/bin/env bash
# Builds webrtc-audio-processing 1.3 into PREFIX for echo cancellation on
# macOS, where Homebrew has no formula for it. 1.3 is the version Linux
# packages ship and WebRtcEchoCanceller is written against. Abseil is built
# into the library from meson's wrap, as Homebrew's is newer than 1.3 was
# written for, so the library needs nothing outside macOS.
#
# Needs meson, ninja and pkgconf (brew install meson ninja pkgconf). Then
# configure Speecher with PKG_CONFIG_PATH=PREFIX/lib/pkgconfig.
#
# DEPLOYMENT_TARGET is Speecher's minimum macOS, CMAKE_OSX_DEPLOYMENT_TARGET
# in CMakeLists.txt, so the linker takes the library for it.

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 PREFIX DEPLOYMENT_TARGET" >&2
  exit 1
fi

VERSION=1.3
SHA256=2365e93e778d7b61b5d6e02d21c47d97222e9c7deff9e1d0838ad6ec2e86f1b9
mkdir -p "$1"
PREFIX="$(cd "$1" && pwd)"
LIBRARY="$PREFIX/lib/libwebrtc-audio-processing-1.3.dylib"
export MACOSX_DEPLOYMENT_TARGET="$2"

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/webrtc-audio-processing.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT

curl -fsSL --retry 3 -o "$WORK_DIR/source.tar.xz" \
  "https://freedesktop.org/software/pulseaudio/webrtc-audio-processing/webrtc-audio-processing-$VERSION.tar.xz"
echo "$SHA256  $WORK_DIR/source.tar.xz" | shasum -a 256 -c -
tar -xf "$WORK_DIR/source.tar.xz" -C "$WORK_DIR"

meson setup "$WORK_DIR/build" "$WORK_DIR/webrtc-audio-processing-$VERSION" \
  --prefix "$PREFIX" --buildtype release --default-library shared \
  --force-fallback-for abseil-cpp -Dabseil-cpp:default_library=static
meson install -C "$WORK_DIR/build"

# An absolute install name, like Homebrew's libraries: the build tree's app
# and tests load it from PREFIX, and macdeployqt copies it into the bundle.
install_name_tool -id "$LIBRARY" "$LIBRARY"
# The library's headers include Abseil's, which the wrap installs to
# PREFIX/include, but the .pc file only names the library's own directory.
sed -i '' 's|^Cflags: |Cflags: -I${includedir} |' "$PREFIX/lib/pkgconfig/webrtc-audio-processing-1.pc"
