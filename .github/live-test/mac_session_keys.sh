#!/usr/bin/env bash

source "$(dirname "$0")/../macos-e2e/common.sh"
set -e
TCC_SEED="$(dirname "$0")/../macos-e2e/tcc_seed.py"
SESSION_DIR="$EVIDENCE_ROOT/session"
mkdir -p "$SESSION_DIR/frames"
: > "$SESSION_DIR/panel-events.jsonl"

cleanup() {
  local result=$?
  trap - EXIT
  local log_path
  log_path="$(app_log_path)"
  if [[ -f "$log_path" ]]; then
    cp "$log_path" "$SESSION_DIR/app.log"
  fi
  if (( result != 0 )); then
    fail_case "Live check stopped; see harness.log and the step's command output."
  fi
  finalize_run
  launchctl unsetenv SPEECHER_E2E_PANEL_CAPTURE_DIR >/dev/null 2>&1 || true
  exit "$result"
}
trap cleanup EXIT

press_key() {
  log "Hardware-style key code $1"
  bounded_osascript -e "tell application \"System Events\" to key code $1"
  sleep 0.3
}

capture_step() {
  textedit_text >"$CASE_DIR/editor.txt"
  screencapture -x "$CASE_DIR/screen.png" >"$CASE_DIR/screencapture.out" 2>&1
  "$PROBE" all "$CASE_DIR/all-windows.json"
  cp "$SESSION_DIR/panel-events.jsonl" "$CASE_DIR/panel-events.jsonl"
  local frame
  frame="$(find "$SESSION_DIR/frames" -name 'frame-*.png' | sort | tail -1)"
  if [[ -n "$frame" ]]; then
    cp "$frame" "$CASE_DIR/panel.png"
  fi
}

expect_text() {
  local actual
  actual="$(textedit_text)"
  printf '%s\n' "$actual" >"$CASE_DIR/editor.txt"
  if [[ "$actual" != "$1" ]]; then
    log "Expected editor text '$1', received '$actual'"
    return 1
  fi
}

wait_panel_status() {
  local wanted="$1" count=0
  while (( count < 50 )); do
    if python3 - "$SESSION_DIR/panel-events.jsonl" "$wanted" <<'PY'
import json, sys
try:
    events = [json.loads(line) for line in open(sys.argv[1]) if line.strip()]
except (FileNotFoundError, json.JSONDecodeError):
    raise SystemExit(1)
statuses = [event["value"] for event in events if event["event"] == "status"]
raise SystemExit(0 if statuses and statuses[-1] == sys.argv[2] else 1)
PY
    then
      return 0
    fi
    sleep 0.1
    count=$((count + 1))
  done
  log "Popup did not reach $wanted"
  return 1
}

find_field_count() {
  bounded_osascript <<'APPLESCRIPT'
tell application "System Events" to tell process "TextEdit"
    set fieldCount to 0
    repeat with element in entire contents of front window
        set elementRole to role of element
        if elementRole is "AXTextField" or elementRole is "AXSearchField" then
            set fieldCount to fieldCount + 1
        end if
    end repeat
    return fieldCount
end tell
APPLESCRIPT
}

case_begin SETUP
baseline_reset
defaults write "$DOMAIN" shortcuts.cancelDictation Esc
defaults write "$DOMAIN" shortcuts.pauseDictation P
defaults write "$DOMAIN" output.completionStatusDurationMs -int 3000
defaults write "$BUNDLE_ID" SUEnableAutomaticChecks -bool false
defaults read "$DOMAIN" >"$CASE_DIR/settings.txt"
seed_common_tcc
probe_desktop_capture
[[ "$DESKTOP_CAPTURE" == 1 ]]

export SPEECHER_E2E_PANEL_CAPTURE_DIR="$SESSION_DIR/frames"
# Keep the app's callback and stderr evidence in one directory for the entire session.
CASE_DIR="$SESSION_DIR"
launch_app
sleep 1
poll_status idle 15 >"$SESSION_DIR/initial-status.txt"
textedit_reset
sleep 0.5

case_begin 01-IDLE-P
press_key 35 >"$CASE_DIR/key.out" 2>&1
expect_text p
capture_step
pass_case "Before a session, P typed p into TextEdit."

case_begin 02-PAUSE-P
cli toggle >"$CASE_DIR/start.out" 2>&1
poll_status listening 10 >"$CASE_DIR/listening.txt"
wait_panel_status Listening
sleep 1
screencapture -x "$CASE_DIR/listening.png"
press_key 35 >"$CASE_DIR/key.out" 2>&1
wait_panel_status Paused
poll_status paused 10 >"$CASE_DIR/status.txt"
expect_text p
capture_step
grep -q 'pause requested' "$(app_log_path)"
pass_case "P paused the session; TextEdit still contained only the original p."

case_begin 03-RESUME-P
press_key 35 >"$CASE_DIR/key.out" 2>&1
wait_panel_status Listening
poll_status listening 10 >"$CASE_DIR/status.txt"
expect_text p
capture_step
grep -q 'resume requested' "$(app_log_path)"
pass_case "P resumed listening; TextEdit still contained only the original p."

case_begin 04-CANCEL-ESC
bounded_osascript -e 'tell application "System Events" to keystroke "f" using command down'
sleep 0.5
fields_before="$(find_field_count)"
printf '%s\n' "$fields_before" >"$CASE_DIR/find-fields-before.txt"
(( fields_before > 0 ))
screencapture -x "$CASE_DIR/find-before-cancel.png"
press_key 53 >"$CASE_DIR/key.out" 2>&1
wait_panel_status Canceled
capture_step
poll_status idle 10 >"$CASE_DIR/status.txt"
expect_text p
fields_after="$(find_field_count)"
printf '%s\n' "$fields_after" >"$CASE_DIR/find-fields-after.txt"
[[ "$fields_after" == "$fields_before" ]]
pass_case "Escape canceled the session, pasted nothing, and left TextEdit's Find bar open."

case_begin 05-IDLE-KEYS
press_key 53 >"$CASE_DIR/escape.out" 2>&1
fields_after="$(find_field_count)"
printf '%s\n' "$fields_after" >"$CASE_DIR/find-fields-after-escape.txt"
[[ "$fields_after" == 0 ]]
screencapture -x "$CASE_DIR/escape-closed-find.png"
press_key 35 >"$CASE_DIR/p.out" 2>&1
expect_text pp
capture_step
pass_case "After the session, Escape closed Find and P typed a second p."

log "All five session shortcut steps passed"
cat "$VERDICTS"
