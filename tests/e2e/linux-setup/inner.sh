#!/usr/bin/env bash
# Runs inside the virtual KDE Wayland session (started by run.sh). Launches the
# AppImage as a daemon with a fresh profile and a fake home, then hands control
# to the driver.
set -euo pipefail

FLOW_DIR="$E2E_FLOW_DIR"
FAKE_HOME="$FLOW_DIR/home"
APP="$FAKE_HOME/Downloads/Speecher.AppImage"
APP_TMPDIR="$E2E_APP_TMPDIR"
mkdir -p "$APP_TMPDIR"

# Seed the stub providers so the Transcription gate can pass without vendor
# credentials; setup itself has not run.
mkdir -p "$FLOW_DIR/config/${SPEECHER_ORG:-io.github.firemonster612}"
cat > "$FLOW_DIR/config/${SPEECHER_ORG:-io.github.firemonster612}/speecher.conf" <<INI
[stt]
provider=e2e-stub

[refinement]
provider=e2e-stub

[audio]
vadEnabled=false
INI

# A short WAV with an audible tone: the microphone page holds Next until the
# level meter has heard something.
python3 - "$FLOW_DIR/mic.wav" <<'PY'
import math, struct, sys, wave
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
    tone = [int(12000 * math.sin(2 * math.pi * 440 * i / 16000)) for i in range(16000)]
    w.writeframes(struct.pack("<" + "h" * len(tone), *tone))
PY

COMMON_ENV=(
  HOME="$FAKE_HOME"
  APPIMAGE_EXTRACT_AND_RUN=1
  TMPDIR="$APP_TMPDIR"
  QT_QPA_PLATFORM=wayland
  QT_ACCESSIBILITY=1
  QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1
  LIBGL_ALWAYS_SOFTWARE=1
  SPEECHER_E2E_STUB=1
  SPEECHER_AUDIO_WAV="$FLOW_DIR/mic.wav"
  SPEECHER_GRAB_DIR="$FLOW_DIR/grabs"
  SPEECHER_GRAB_PAGE="setup:global shortcut"
  XDG_CONFIG_HOME="$FLOW_DIR/config"
  XDG_DATA_HOME="$FLOW_DIR/data"
  XDG_CACHE_HOME="$FLOW_DIR/cache"
  XDG_RUNTIME_DIR="$E2E_RUNTIME_DIR"
)

sleep 1
bwrap \
  --ro-bind / / \
  --dev-bind /dev /dev \
  --proc /proc \
  --bind /tmp /tmp \
  --bind "$FLOW_DIR" "$FLOW_DIR" \
  --bind "$APP_TMPDIR" "$APP_TMPDIR" \
  env "${COMMON_ENV[@]}" "$APP" --daemon \
  > "$FLOW_DIR/app-stdio.log" 2>&1 &

sleep 3
E2E_APP="$APP" \
E2E_FLOW_DIR="$FLOW_DIR" \
E2E_FAKE_HOME="$FAKE_HOME" \
APPIMAGE_EXTRACT_AND_RUN=1 \
TMPDIR="$APP_TMPDIR" \
QT_QPA_PLATFORM=wayland \
XDG_RUNTIME_DIR="$E2E_RUNTIME_DIR" \
  python3 "$E2E_HERE/drive.py" > "$FLOW_DIR/driver.log" 2>&1
