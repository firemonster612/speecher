#!/usr/bin/env bash

set -uo pipefail

DOMAIN=com.io-github-firemonster612.speecher
BUNDLE_ID=io.github.firemonster612.speecher
EXPECTED_TEXT='The quick brown fox.'
EVIDENCE_ROOT="${EVIDENCE_ROOT:?}"
APP_BUNDLE="${APP_BUNDLE:?}"
APP_BIN="$APP_BUNDLE/Contents/MacOS/speecher"
PROBE="${PROBE:?}"
VERDICTS="$EVIDENCE_ROOT/verdicts.txt"
RUN_LOG="$EVIDENCE_ROOT/harness.log"
CURRENT_CASE=
CASE_DIR=
CASE_LOG_OFFSET=1
APP_PID=
mkdir -p "$EVIDENCE_ROOT"
: > "$VERDICTS"
: > "$RUN_LOG"

log() {
  printf '%s %s\n' "$(date -u +%FT%TZ)" "$*" | tee -a "$RUN_LOG"
}

record_verdict() {
  local case_id="$1" status="$2" sentence="$3"
  if grep -q "^${case_id}:" "$VERDICTS"; then
    return
  fi
  printf '%s: %s — %s\n' "$case_id" "$status" "$sentence" | tee -a "$VERDICTS"
}

case_begin() {
  CURRENT_CASE="$1"
  CASE_DIR="$EVIDENCE_ROOT/$CURRENT_CASE"
  mkdir -p "$CASE_DIR"
  launchctl setenv SPEECHER_E2E_EVIDENCE_DIR "$CASE_DIR" >/dev/null 2>&1 || true
  export SPEECHER_E2E_EVIDENCE_DIR="$CASE_DIR"
  local log_path
  log_path="$(app_log_path)"
  if [[ -f "$log_path" ]]; then
    CASE_LOG_OFFSET=$(( $(wc -c < "$log_path") + 1 ))
  else
    CASE_LOG_OFFSET=1
  fi
  log "BEGIN $CURRENT_CASE"
}

case_evidence() {
  local log_path
  log_path="$(app_log_path)"
  screencapture -x "$CASE_DIR/screen.png" >"$CASE_DIR/screencapture.out" 2>&1 || true
  local pid
  pid="$(pgrep -x speecher | head -1 || true)"
  if [[ -n "$pid" ]]; then
    "$PROBE" dump "$pid" "$CASE_DIR/windows.json" >"$CASE_DIR/window-probe.out" 2>&1 || true
  else
    printf '[]\n' > "$CASE_DIR/windows.json"
  fi
  if [[ -f "$log_path" ]]; then
    tail -c "+$CASE_LOG_OFFSET" "$log_path" > "$CASE_DIR/log.txt" 2>/dev/null || cp "$log_path" "$CASE_DIR/log.txt"
  else
    : > "$CASE_DIR/log.txt"
  fi
}

pass_case() {
  case_evidence
  record_verdict "$CURRENT_CASE" PASS "$1"
  log "END $CURRENT_CASE PASS"
}

fail_case() {
  case_evidence
  printf '%s\n' "$1" >> "$CASE_DIR/assertions-failed.txt"
  record_verdict "$CURRENT_CASE" FAIL "$1"
  log "END $CURRENT_CASE FAIL: $1"
}

block_case() {
  case_evidence
  record_verdict "$CURRENT_CASE" BLOCKED "$1"
  log "END $CURRENT_CASE BLOCKED: $1"
}

app_log_path() {
  "$APP_BIN" --version 2>/dev/null | sed -n 's/^log //p' | tail -1
}

stop_app() {
  pkill -9 -x speecher >/dev/null 2>&1 || true
  local count=0
  while pgrep -x speecher >/dev/null 2>&1 && (( count < 50 )); do
    sleep 0.1
    count=$((count + 1))
  done
  APP_PID=
}

baseline_reset() {
  stop_app
  defaults delete "$DOMAIN" >/dev/null 2>&1 || true
  defaults write "$DOMAIN" app.setupCompleted -bool true
  defaults write "$DOMAIN" app.launchAtLogin -bool false
  defaults write "$DOMAIN" stt.provider e2e-stub
  defaults write "$DOMAIN" refinement.provider e2e-stub
  defaults write "$DOMAIN" refinement.style balanced
  defaults write "$DOMAIN" refinement.includeScreenshotContext -bool false
  defaults write "$DOMAIN" output.method automatic
  defaults write "$DOMAIN" output.restoreClipboardAfterTyping -bool false
  defaults write "$DOMAIN" output.completionStatusDurationMs -int 500
  defaults write "$DOMAIN" ui.pauseMediaDuringTranscription -bool false
  defaults write "$DOMAIN" ui.soundsEnabled -bool false
  defaults write "$DOMAIN" updates.autoCheck -bool false
  defaults write "$DOMAIN" updates.autoInstall -bool false
  defaults write "$DOMAIN" vocabulary.correctionLearningEnabled -bool false
  launchctl setenv SPEECHER_E2E_STUB 1 >/dev/null 2>&1 || true
  launchctl unsetenv SPEECHER_E2E_REAL_AUDIO >/dev/null 2>&1 || true
  export SPEECHER_E2E_STUB=1
  unset SPEECHER_E2E_REAL_AUDIO
}

launch_app() {
  local real_audio="${1:-0}"
  if [[ "$real_audio" == 1 ]]; then
    export SPEECHER_E2E_REAL_AUDIO=1
    launchctl setenv SPEECHER_E2E_REAL_AUDIO 1 >/dev/null 2>&1 || true
  else
    unset SPEECHER_E2E_REAL_AUDIO
    launchctl unsetenv SPEECHER_E2E_REAL_AUDIO >/dev/null 2>&1 || true
  fi
  SPEECHER_E2E_STUB="${SPEECHER_E2E_STUB:-}" \
    SPEECHER_E2E_REAL_AUDIO="${SPEECHER_E2E_REAL_AUDIO:-}" \
    SPEECHER_E2E_EVIDENCE_DIR="$CASE_DIR" \
    DYLD_FRAMEWORK_PATH="${QT_ROOT_DIR:-}/lib" \
    "$APP_BIN" --daemon >"$CASE_DIR/process.out" 2>&1 &
  APP_PID=$!
  poll_process 15
}

poll_process() {
  local timeout="$1" count=0 limit=$((timeout * 10))
  while (( count < limit )); do
    if kill -0 "$APP_PID" >/dev/null 2>&1 && [[ "$(pgrep -x speecher | wc -l | tr -d ' ')" == 1 ]]; then
      return 0
    fi
    sleep 0.1
    count=$((count + 1))
  done
  return 1
}

cli() {
  "$APP_BIN" "$@"
}

poll_status() {
  local wanted="$1" timeout="${2:-15}" count=0 limit=$((timeout * 5)) status
  while (( count < limit )); do
    status="$(cli status 2>/dev/null | tail -1)"
    if [[ "$status" == "$wanted" ]]; then
      printf '%s\n' "$status"
      return 0
    fi
    sleep 0.2
    count=$((count + 1))
  done
  printf '%s\n' "${status:-unknown}"
  return 1
}

poll_status_one_of() {
  local timeout="$1"
  shift
  local count=0 limit=$((timeout * 5)) status wanted
  while (( count < limit )); do
    status="$(cli status 2>/dev/null | tail -1)"
    for wanted in "$@"; do
      [[ "$status" == "$wanted" ]] && { printf '%s\n' "$status"; return 0; }
    done
    sleep 0.2
    count=$((count + 1))
  done
  printf '%s\n' "${status:-unknown}"
  return 1
}

phase_capture() {
  local phase="$1" pid
  screencapture -x "$CASE_DIR/$phase.png" >/dev/null 2>&1 || true
  pid="$(pgrep -x speecher | head -1 || true)"
  if [[ -n "$pid" ]]; then
    "$PROBE" dump "$pid" "$CASE_DIR/windows-$phase.json" >/dev/null 2>&1 || true
  fi
}

panel_visible() {
  local baseline="$1" phase="$2" pid
  pid="$(pgrep -x speecher | head -1 || true)"
  [[ -n "$pid" ]] || return 2
  screencapture -x "$CASE_DIR/$phase.png" >/dev/null 2>&1 || return 3
  "$PROBE" panel "$pid" "$baseline" "$CASE_DIR/$phase.png" \
    "$CASE_DIR/windows-$phase.json" >"$CASE_DIR/panel-$phase.out" 2>&1
}

panel_gone() {
  local pid json="$CASE_DIR/windows-gone.json"
  pid="$(pgrep -x speecher | head -1 || true)"
  [[ -z "$pid" ]] && return 0
  "$PROBE" dump "$pid" "$json" >/dev/null 2>&1 || return 1
  python3 - "$json" <<'PY'
import json, sys
for window in json.load(open(sys.argv[1])):
    bounds = window.get("bounds", {})
    if (window.get("layer") == 25 and window.get("isOnscreen") and window.get("alpha", 0) > 0
            and bounds.get("Width", 0) >= 420 and abs(bounds.get("Height", 0) - 72) < 1):
        raise SystemExit(1)
PY
}

window_count() {
  local pid json="$1"
  pid="$(pgrep -x speecher | head -1 || true)"
  [[ -n "$pid" ]] || { echo 0; return; }
  "$PROBE" dump "$pid" "$json" >/dev/null 2>&1 || { echo 0; return; }
  python3 - "$json" <<'PY'
import json, sys
print(len(json.load(open(sys.argv[1]))))
PY
}

window_matches() {
  local kind="$1" json="$2" pid
  pid="$(pgrep -x speecher | head -1 || true)"
  [[ -n "$pid" ]] || return 1
  "$PROBE" dump "$pid" "$json" >/dev/null 2>&1 || return 1
  python3 - "$kind" "$json" <<'PY'
import json, sys
kind, path = sys.argv[1:]
for window in json.load(open(path)):
    bounds = window.get("bounds", {})
    if not window.get("isOnscreen"):
        continue
    if kind == "normal" and window.get("layer") == 0 and bounds.get("Width", 0) > 200 and bounds.get("Height", 0) > 150:
        raise SystemExit(0)
    if kind == "status-item" and window.get("layer", 0) >= 20 and bounds.get("Width", 0) < 200:
        raise SystemExit(0)
raise SystemExit(1)
PY
}

run_cycle() {
  local baseline="$1" label="$2"
  cli start >"$CASE_DIR/$label-start.out" 2>&1 || return 1
  poll_status listening 10 >"$CASE_DIR/$label-listening.out" || return 1
  panel_visible "$baseline" "$label-listening" || return 1
  cli stop >"$CASE_DIR/$label-stop.out" 2>&1 || return 1
  poll_status idle 10 >"$CASE_DIR/$label-idle.out" || return 1
  panel_gone
}

restart_tcc() {
  sudo launchctl kickstart -k system/com.apple.tccd >/dev/null 2>&1 \
    || sudo killall tccd >/dev/null 2>&1 \
    || return 1
  killall tccd >/dev/null 2>&1 || true
}

seed_tcc() {
  local service="$1" auth="$2" db client type="${3:-0}" indirect="${4:-}"
  if [[ "$service" == kTCCServiceMicrophone || "$service" == kTCCServiceAppleEvents ]]; then
    db="$HOME/Library/Application Support/com.apple.TCC/TCC.db"
  else
    db="/Library/Application Support/com.apple.TCC/TCC.db"
  fi
  client="$BUNDLE_ID"
  if [[ "$service" == kTCCServiceAppleEvents ]]; then
    client=/usr/bin/osascript
    type=1
  fi
  if [[ "$db" == /Library/* ]]; then
    if [[ -n "$indirect" ]]; then
      sudo python3 "$TCC_SEED" "$db" "$service" "$client" "$auth" "$indirect" "$type"
    else
      sudo python3 "$TCC_SEED" "$db" "$service" "$client" "$auth"
    fi
  else
    if [[ -n "$indirect" ]]; then
      python3 "$TCC_SEED" "$db" "$service" "$client" "$auth" "$indirect" "$type"
    else
      python3 "$TCC_SEED" "$db" "$service" "$client" "$auth"
    fi
  fi
}

seed_common_tcc() {
  local output="$EVIDENCE_ROOT/tcc-seeding.log"
  : > "$output"
  sqlite3 "$HOME/Library/Application Support/com.apple.TCC/TCC.db" '.schema access' \
    > "$EVIDENCE_ROOT/tcc-user-schema.sql" 2>&1 || return 1
  sudo sqlite3 "/Library/Application Support/com.apple.TCC/TCC.db" '.schema access' \
    > "$EVIDENCE_ROOT/tcc-system-schema.sql" 2>&1 || return 1
  seed_tcc kTCCServiceMicrophone 2 >>"$output" 2>&1 || return 1
  seed_tcc kTCCServiceAccessibility 2 >>"$output" 2>&1 || return 1
  seed_tcc kTCCServicePostEvent 2 >>"$output" 2>&1 || return 1
  seed_tcc kTCCServiceScreenCapture 2 >>"$output" 2>&1 || return 1
  sudo python3 "$TCC_SEED" "/Library/Application Support/com.apple.TCC/TCC.db" \
    kTCCServiceAccessibility /usr/bin/osascript 2 UNUSED 1 >>"$output" 2>&1 || return 1
  sudo python3 "$TCC_SEED" "/Library/Application Support/com.apple.TCC/TCC.db" \
    kTCCServiceScreenCapture /usr/sbin/screencapture 2 UNUSED 1 >>"$output" 2>&1 || return 1
  seed_tcc kTCCServiceAppleEvents 2 1 com.apple.TextEdit >>"$output" 2>&1 || return 1
  seed_tcc kTCCServiceAppleEvents 2 1 com.apple.systemevents >>"$output" 2>&1 || return 1
  seed_tcc kTCCServiceAppleEvents 2 1 "$BUNDLE_ID" >>"$output" 2>&1 || return 1
  restart_tcc >>"$output" 2>&1
}

accessibility_log_value() {
  local expected="$1" timeout="${2:-10}" count=0 log_path
  log_path="$(app_log_path)"
  while (( count < timeout * 5 )); do
    if tail -100 "$log_path" 2>/dev/null | grep -q "trusted=$expected"; then
      return 0
    fi
    sleep 0.2
    count=$((count + 1))
  done
  return 1
}

textedit_reset() {
  printf '' > /tmp/speecher-e2e-target.txt
  open -a TextEdit /tmp/speecher-e2e-target.txt
  local count=0
  while (( count < 50 )); do
    osascript -e 'tell application "TextEdit" to if (count documents) > 0 then return "ready"' \
      2>/dev/null | grep -q ready && break
    sleep 0.2
    count=$((count + 1))
  done
  osascript -e 'tell application "TextEdit" to set text of document 1 to ""' \
    -e 'tell application "TextEdit" to activate' >/dev/null
}

textedit_text() {
  osascript -e 'tell application "TextEdit" to return text of document 1'
}

finalize_run() {
  stop_app
  launchctl unsetenv SPEECHER_E2E_STUB >/dev/null 2>&1 || true
  launchctl unsetenv SPEECHER_E2E_REAL_AUDIO >/dev/null 2>&1 || true
  launchctl unsetenv SPEECHER_E2E_EVIDENCE_DIR >/dev/null 2>&1 || true
}
