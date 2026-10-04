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
    screencapture -x "$CASE_DIR/failure-screen.png" >"$CASE_DIR/failure-capture.out" 2>&1 || true
    fail_case "Live check stopped; see harness.log and the step's command output."
  fi
  finalize_run
  launchctl unsetenv SPEECHER_E2E_PANEL_CAPTURE_DIR >/dev/null 2>&1 || true
  exit "$result"
}
trap cleanup EXIT

# The shared helpers use unbounded AppleEvents; CI must finish if TextEdit is blocked.
textedit_text() {
  "$KEY_INPUT" text "$(pgrep -x TextEdit | head -1)" 2>>"$CASE_DIR/editor-readback.out"
}

textedit_reset() {
  log "Opening TextEdit"
  defaults write com.apple.TextEdit NSShowAppCentricOpenPanelInsteadOfUntitledFile -bool false
  open -a TextEdit
  sleep 2
  screencapture -x "$CASE_DIR/textedit-open.png"
  bounded_osascript -e 'tell application "TextEdit"' \
    -e 'if (count documents) is 0 then make new document' \
    -e 'set text of document 1 to ""' -e 'activate' -e 'end tell' \
    >"$CASE_DIR/textedit-reset.out" 2>&1
}

press_key() {
  log "Hardware-style key code $1"
  "$KEY_INPUT" "$@"
  sleep 0.3
  screencapture -x "$CASE_DIR/key-$1.png"
  log "Key code $1 posted"
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
  log "Reading TextEdit document text"
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
  "$KEY_INPUT" fields "$(pgrep -x TextEdit | head -1)"
}

case_begin SETUP
KEY_INPUT="$EVIDENCE_ROOT/key-input"
cat >"$EVIDENCE_ROOT/key-input.swift" <<'SWIFT'
import ApplicationServices
import CoreGraphics
import Foundation

func attribute(_ element: AXUIElement, _ name: String) -> CFTypeRef? {
    var value: CFTypeRef?
    guard AXUIElementCopyAttributeValue(element, name as CFString, &value) == .success else { return nil }
    return value
}

func descendants(_ element: AXUIElement) -> [AXUIElement] {
    let children = attribute(element, kAXChildrenAttribute) as? [AXUIElement] ?? []
    return [element] + children.flatMap(descendants)
}

let mode = CommandLine.arguments[1]
if mode == "text" || mode == "fields" {
    guard let pid = Int32(CommandLine.arguments[2]) else { exit(2) }
    let app = AXUIElementCreateApplication(pid)
    AXUIElementSetMessagingTimeout(app, 5)
    guard let windows = attribute(app, kAXWindowsAttribute) as? [AXUIElement],
          !windows.isEmpty else { fputs("No accessible TextEdit window\n", stderr); exit(2) }
    let elements = windows.flatMap(descendants)
    if mode == "fields" {
        print(elements.filter {
            let role = attribute($0, kAXRoleAttribute) as? String
            return role == kAXTextFieldRole || role == "AXSearchField"
        }.count)
    } else {
        guard let area = elements.first(where: { attribute($0, kAXRoleAttribute) as? String == kAXTextAreaRole }),
              let text = attribute(area, kAXValueAttribute) as? String else {
            for element in elements {
                let role = attribute(element, kAXRoleAttribute) as? String ?? ""
                let title = attribute(element, kAXTitleAttribute) as? String ?? ""
                let value = attribute(element, kAXValueAttribute) as? String ?? ""
                fputs("role=\(role) title=\(title) value=\(value)\n", stderr)
            }
            fputs("No accessible TextEdit document text\n", stderr); exit(2)
        }
        print(text)
    }
    exit(0)
}

guard CommandLine.arguments.count >= 2,
      let code = UInt16(CommandLine.arguments[1]),
      let source = CGEventSource(stateID: .hidSystemState),
      let down = CGEvent(keyboardEventSource: source, virtualKey: code, keyDown: true),
      let up = CGEvent(keyboardEventSource: source, virtualKey: code, keyDown: false)
else { exit(1) }
let flags: CGEventFlags = CommandLine.arguments.dropFirst(2).contains("command") ? .maskCommand : []
down.flags = flags
up.flags = flags
down.post(tap: .cghidEventTap)
usleep(80000)
up.post(tap: .cghidEventTap)
SWIFT
swiftc "$EVIDENCE_ROOT/key-input.swift" -o "$KEY_INPUT"
baseline_reset
defaults write "$DOMAIN" shortcuts.cancelDictation Esc
defaults write "$DOMAIN" shortcuts.pauseDictation P
defaults write "$DOMAIN" output.completionStatusDurationMs -int 3000
defaults write "$BUNDLE_ID" SUEnableAutomaticChecks -bool false
defaults read "$DOMAIN" >"$CASE_DIR/settings.txt"
seed_common_tcc
user_tcc="$HOME/Library/Application Support/com.apple.TCC/TCC.db"
system_tcc='/Library/Application Support/com.apple.TCC/TCC.db'
# On hosted runners, TCC attributes osascript's requests to this ancestor.
runner_client="$(sqlite3 "$user_tcc" "SELECT DISTINCT client FROM access WHERE client LIKE '%hosted-compute-agent%' LIMIT 1;")"
if [[ -z "$runner_client" ]]; then
  runner_client="$(ps -axo comm= | awk '/\/hosted-compute-agent$/ { print; exit }')"
fi
[[ -n "$runner_client" ]]
printf '%s\n' "$runner_client" >"$EVIDENCE_ROOT/runner-tcc-client.txt"
for target in com.apple.TextEdit com.apple.systemevents; do
  python3 "$TCC_SEED" "$user_tcc" kTCCServiceAppleEvents "$runner_client" 2 "$target" 1 \
    >>"$EVIDENCE_ROOT/tcc-seeding.log"
done
sudo python3 "$TCC_SEED" "$system_tcc" kTCCServiceAccessibility "$runner_client" 2 UNUSED 1 \
  >>"$EVIDENCE_ROOT/tcc-seeding.log"
sudo python3 "$TCC_SEED" "$system_tcc" kTCCServicePostEvent "$runner_client" 2 UNUSED 1 \
  >>"$EVIDENCE_ROOT/tcc-seeding.log"
sudo python3 "$TCC_SEED" "$system_tcc" kTCCServicePostEvent "$KEY_INPUT" 2 UNUSED 1 \
  >>"$EVIDENCE_ROOT/tcc-seeding.log"
sudo python3 "$TCC_SEED" "$system_tcc" kTCCServiceAccessibility "$KEY_INPUT" 2 UNUSED 1 \
  >>"$EVIDENCE_ROOT/tcc-seeding.log"
# tcc_seed.py copies a template row, including Terminal's target identity.
sqlite3 "$user_tcc" \
  "UPDATE access SET indirect_object_code_identity=NULL WHERE service='kTCCServiceAppleEvents' AND (client='/usr/bin/osascript' OR client LIKE '%hosted-compute-agent%');"
restart_tcc
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
fields_baseline="$(find_field_count)"
printf '%s\n' "$fields_baseline" >"$CASE_DIR/find-fields-baseline.txt"
press_key 3 command
sleep 0.5
fields_before="$(find_field_count)"
printf '%s\n' "$fields_before" >"$CASE_DIR/find-fields-before.txt"
(( fields_before > fields_baseline ))
screencapture -x "$CASE_DIR/find-before-cancel.png"
# Moving focus to Find can accept TextEdit's automatic capitalization suggestion.
cancel_text="$(textedit_text)"
printf '%s\n' "$cancel_text" >"$CASE_DIR/editor-before-cancel.txt"
press_key 53 >"$CASE_DIR/key.out" 2>&1
wait_panel_status Canceled
capture_step
poll_status idle 10 >"$CASE_DIR/status.txt"
expect_text "$cancel_text"
fields_after="$(find_field_count)"
printf '%s\n' "$fields_after" >"$CASE_DIR/find-fields-after.txt"
[[ "$fields_after" == "$fields_before" ]]
pass_case "Escape canceled the session, pasted nothing, and left TextEdit's Find bar open."

case_begin 05-IDLE-KEYS
press_key 53 >"$CASE_DIR/escape.out" 2>&1
fields_after="$(find_field_count)"
printf '%s\n' "$fields_after" >"$CASE_DIR/find-fields-after-escape.txt"
[[ "$fields_after" == "$fields_baseline" ]]
screencapture -x "$CASE_DIR/escape-closed-find.png"
press_key 35 >"$CASE_DIR/p.out" 2>&1
expect_text "${cancel_text}p"
capture_step
pass_case "After the session, Escape closed Find and P typed a second p."

log "All five session shortcut steps passed"
cat "$VERDICTS"
