#!/usr/bin/env bash

# The dictation panel's phase walk, filmed from its own backing store: waveform
# while listening, a shimmering Transcribing…, a shimmering Refining…, the
# streamed refinement words, then the delivered receipt. Scratch-branch-only;
# uses the stub providers with timings slow enough to see each phase.

source "$(dirname "$0")/common.sh"

baseline_reset
case_begin PANEL-FLOW
export SPEECHER_E2E_PANEL_CAPTURE_DIR="$CASE_DIR/frames"
launchctl setenv SPEECHER_E2E_PANEL_CAPTURE_DIR "$CASE_DIR/frames" >/dev/null 2>&1 || true

check_events() {
  python3 - "$CASE_DIR/panel-events.jsonl" <<'PY'
import json, sys

events = []
for raw in open(sys.argv[1]):
    raw = raw.strip()
    if raw:
        events.append(json.loads(raw))

def first(predicate):
    for index, event in enumerate(events):
        if predicate(event):
            return index
    return -1

live = {"", "Idle", "Preparing", "Starting", "Listening", "Stopping",
        "Refining", "Delivering"}
stopping = first(lambda e: e["event"] == "status" and e["value"] == "Stopping")
refining = first(lambda e: e["event"] == "refining" and e["value"] == "true")
streamed = first(lambda e: e["event"] == "refinement-preview" and e["value"])
delivered = first(lambda e: e["event"] == "status" and e["value"] not in live)
print(f"stopping={stopping} refining={refining} streamed={streamed} delivered={delivered}")
if not 0 <= stopping < refining < streamed < delivered:
    print("FAIL: the panel did not walk Stopping -> Refining -> stream -> delivered")
    sys.exit(1)
PY
}

# Listening from launch: sending "start" over IPC right after the spawn races
# the daemon's socket setup, and the CLI then start-detaches a second daemon
# that dies on the taken socket while the command is lost.
launch_listening() {
  if pgrep -x speecher >"$CASE_DIR/prelaunch-processes.txt" 2>&1; then
    return 1
  fi
  SPEECHER_E2E_STUB="${SPEECHER_E2E_STUB:-}" \
    SPEECHER_E2E_SKIP_MIC_GATE="${SPEECHER_E2E_SKIP_MIC_GATE:-}" \
    SPEECHER_E2E_EVIDENCE_DIR="$CASE_DIR" \
    SPEECHER_E2E_PANEL_CAPTURE_DIR="$CASE_DIR/frames" \
    DYLD_FRAMEWORK_PATH="${QT_ROOT_DIR:-}/lib" \
    "$APP_BIN" --daemon --start-listening >"$CASE_DIR/process.out" 2>&1 &
  APP_PID=$!
  poll_process 15
}

if ! launch_listening; then
  fail_case "The app did not launch."
else
  errors=()
  poll_status listening 10 >"$CASE_DIR/listening.out" \
    || errors+=("the session never reached listening")
  # Long enough for the stub's partials to fill the live preview on film. By
  # now the daemon's IPC socket has long been up, so the CLI verbs are safe.
  sleep 2.5
  cli stop >"$CASE_DIR/stop.out" 2>&1 || errors+=("the stop command failed")
  poll_status idle 25 >"$CASE_DIR/idle.out" || errors+=("the session never returned to idle")
  sleep 1
  check_events >"$CASE_DIR/event-checks.txt" 2>&1 \
    || errors+=("the panel events do not show the full phase walk; see event-checks.txt")
  frame_count=$(find "$CASE_DIR/frames" -name 'frame-*.png' 2>/dev/null | wc -l | tr -d ' ')
  printf 'frames: %s\n' "$frame_count" >>"$CASE_DIR/event-checks.txt"
  (( frame_count >= 30 )) || errors+=("only $frame_count panel frames were captured")
  if command -v ffmpeg >/dev/null 2>&1; then
    ffmpeg -y -framerate 10 -i "$CASE_DIR/frames/frame-%06d.png" \
      -pix_fmt yuv420p "$CASE_DIR/panel-flow.mp4" >"$CASE_DIR/ffmpeg.out" 2>&1 \
      || errors+=("ffmpeg could not assemble the panel video")
  else
    errors+=("ffmpeg is not on the runner")
  fi
  if (( ${#errors[@]} )); then
    fail_case "$(IFS='; '; echo "${errors[*]}")"
  else
    pass_case "The panel walked listening, Transcribing…, Refining…, streamed text and delivery on film."
  fi
fi

stop_app
launchctl unsetenv SPEECHER_E2E_PANEL_CAPTURE_DIR >/dev/null 2>&1 || true
unset SPEECHER_E2E_PANEL_CAPTURE_DIR

# BANNER-STACK: both notices pinned on, to film the order they stack above the
# pill. A CI install has no update pending, so the stack never appears on its
# own in the flow above.
case_begin BANNER-STACK
export SPEECHER_E2E_PANEL_BANNERS=1
launchctl setenv SPEECHER_E2E_PANEL_BANNERS 1 >/dev/null 2>&1 || true
export SPEECHER_E2E_PANEL_CAPTURE_DIR="$CASE_DIR/frames"
launchctl setenv SPEECHER_E2E_PANEL_CAPTURE_DIR "$CASE_DIR/frames" >/dev/null 2>&1 || true
if ! launch_listening; then
  fail_case "The app did not launch."
else
  poll_status listening 10 >"$CASE_DIR/listening.out" \
    || log "BANNER-STACK never reached listening; filming whatever showed"
  sleep 1.5
  banner_frames=$(find "$CASE_DIR/frames" -name 'frame-*.png' 2>/dev/null | wc -l | tr -d ' ')
  printf 'frames: %s\n' "$banner_frames" >"$CASE_DIR/frame-count.txt"
  if (( banner_frames >= 5 )); then
    pass_case "Filmed the what's-new and update notices stacked above the pill."
  else
    fail_case "only $banner_frames banner-stack frames were captured"
  fi
fi
stop_app
launchctl unsetenv SPEECHER_E2E_PANEL_BANNERS >/dev/null 2>&1 || true
launchctl unsetenv SPEECHER_E2E_PANEL_CAPTURE_DIR >/dev/null 2>&1 || true
unset SPEECHER_E2E_PANEL_BANNERS SPEECHER_E2E_PANEL_CAPTURE_DIR

# DICTATION-PANE: the Transcription row's dynamic subtitle (the schema's
# helpValue) renders in the settings window's Dictation pane.
defaults write "$DOMAIN" stt.provider claude
defaults write "$BUNDLE_ID" settingsPane dictation
case_begin DICTATION-PANE
DYLD_FRAMEWORK_PATH="${QT_ROOT_DIR:-}/lib" \
  "$APP_BIN" --grab "$CASE_DIR/dictation-pane.png" >"$CASE_DIR/grab.out" 2>&1
pane_text="$(swift - "$CASE_DIR/dictation-pane.png" <<'SWIFT'
import Foundation
import ImageIO
import Vision

let url = URL(fileURLWithPath: CommandLine.arguments[1])
guard let data = try? Data(contentsOf: url),
      let source = CGImageSourceCreateWithData(data as CFData, nil),
      let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else { exit(1) }
let request = VNRecognizeTextRequest()
request.recognitionLevel = .accurate
try? VNImageRequestHandler(cgImage: image).perform([request])
print((request.results ?? []).compactMap { $0.topCandidates(1).first?.string }
    .joined(separator: " "))
SWIFT
)"
printf '%s\n' "$pane_text" >"$CASE_DIR/ocr.txt"
if [[ "$pane_text" == *"Deepgram Nova 3"* ]]; then
  pass_case "The Dictation pane shows the selected provider's dynamic subtitle."
else
  fail_case "The Dictation pane capture does not show the Claude Voice subtitle."
fi

finalize_run
log "Panel flow E2E finished"
cat "$VERDICTS"
if grep -q 'FAIL\|BLOCKED' "$VERDICTS"; then
  exit 1
fi
