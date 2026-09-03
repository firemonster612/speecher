#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"
TCC_SEED="$(dirname "$0")/tcc_seed.py"

if seed_common_tcc; then
  TCC_READY=1
else
  TCC_READY=0
  log "TCC baseline seeding failed"
fi

baseline_reset
case_begin M15
if ! launch_app; then
  fail_case "The app did not launch within 15 seconds."
else
  phase_capture before
  errors=()
  cli start >"$CASE_DIR/start.out" 2>&1 || errors+=("start command failed")
  phase_capture start
  sleep 0.1
  phase_capture plus-100ms
  visible=0
  for attempt in {1..10}; do
    if panel_visible "$CASE_DIR/before.png" "visible-$attempt"; then visible=1; break; fi
    sleep 0.2
  done
  [[ "$visible" == 1 ]] || errors+=("panel did not satisfy the window-and-pixel predicate within 2 seconds")
  poll_status listening 10 >"$CASE_DIR/listening-status.txt" || errors+=("session did not reach listening")
  phase_capture listening
  cli stop >"$CASE_DIR/stop.out" 2>&1 || errors+=("stop command failed")
  if poll_status refining 2 >"$CASE_DIR/refining-status.txt"; then phase_capture refining; else phase_capture refining-missed; fi
  if poll_status delivering 3 >"$CASE_DIR/delivered-status.txt"; then phase_capture delivered; else phase_capture delivered-missed; fi
  poll_status idle 10 >"$CASE_DIR/idle-status.txt" || errors+=("session did not return to idle")
  phase_capture hidden
  panel_gone || errors+=("panel remained onscreen after completion")
  grep -q '"event":"qt-show-emit"' "$CASE_DIR/panel-events.jsonl" 2>/dev/null || errors+=("Qt show emission was not recorded")
  grep -q '"event":"bridge-show".*"blockWasNil":false' "$CASE_DIR/panel-events.jsonl" 2>/dev/null || errors+=("bridge show block was nil or unrecorded")
  grep -q '"event":"show"' "$CASE_DIR/panel-events.jsonl" 2>/dev/null || errors+=("Swift show receipt was not recorded")
  grep -q '"event":"position"' "$CASE_DIR/panel-events.jsonl" 2>/dev/null || errors+=("panel position evidence was not recorded")
  grep -q '"event":"presented"' "$CASE_DIR/panel-events.jsonl" 2>/dev/null || errors+=("post-ordering panel state was not recorded")
  if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "The panel appeared with rendered pixels, crossed every instrumented layer, and hid after delivery."; fi
fi

baseline_reset
case_begin M16
if ! launch_app; then
  fail_case "The app did not launch."
else
  phase_capture before
  initial_windows="$(window_count "$CASE_DIR/windows-initial.json")"
  failed_cycle=
  for cycle in {1..10}; do
    if ! run_cycle "$CASE_DIR/before.png" "cycle-$cycle"; then failed_cycle="$cycle"; break; fi
    panel_count="$(python3 - "$CASE_DIR/windows-cycle-$cycle-listening.json" <<'PY'
import json, sys
print(sum(1 for w in json.load(open(sys.argv[1])) if w.get("layer") == 25 and w.get("isOnscreen")))
PY
)"
    if (( panel_count > 1 )); then failed_cycle="$cycle-more-than-one-panel"; break; fi
  done
  final_windows="$(window_count "$CASE_DIR/windows-final.json")"
  if [[ -n "$failed_cycle" ]]; then
    fail_case "Repeated Dictation Session $failed_cycle violated the panel or state assertion."
  elif [[ "$initial_windows" != "$final_windows" ]]; then
    fail_case "The process window count changed from $initial_windows to $final_windows after 10 cycles."
  else
    pass_case "All 10 Dictation Sessions showed one panel, hid it, returned idle, and kept the window count stable."
  fi
fi

baseline_reset
case_begin M17
defaults write "$DOMAIN" stt.provider no-such-provider
unset SPEECHER_E2E_STUB
launchctl unsetenv SPEECHER_E2E_STUB >/dev/null 2>&1 || true
if ! launch_app; then
  fail_case "The app did not launch for the error-path test."
else
  phase_capture before
  cli start >"$CASE_DIR/start.out" 2>&1 || true
  errors=()
  poll_status error 5 >"$CASE_DIR/error-status.txt" || errors+=("status did not become error")
  panel_visible "$CASE_DIR/before.png" error || errors+=("error panel was not visibly rendered")
  osascript -e 'tell application "System Events" to tell process "speecher" to return exists button "Dismiss" of window 1' \
    >"$CASE_DIR/dismiss-ax.txt" 2>"$CASE_DIR/dismiss-ax.err" || true
  grep -q true "$CASE_DIR/dismiss-ax.txt" || errors+=("AX did not expose a Dismiss button")
  cli stop >"$CASE_DIR/stop.out" 2>&1 || true
  poll_status idle 5 >"$CASE_DIR/idle-status.txt" || errors+=("stop did not return the error state to idle")
  panel_gone || errors+=("stop did not hide the error panel")
  if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "The unknown provider produced a visible error pill with Dismiss and stop returned the app to idle."; fi
fi

baseline_reset
case_begin M22
if [[ "$TCC_READY" != 1 ]]; then
  block_case "The runner could not seed the required Accessibility and Automation grants."
elif ! textedit_reset >"$CASE_DIR/textedit-setup.out" 2>"$CASE_DIR/textedit-setup.err"; then
  block_case "TextEdit could not be automated without a permission prompt."
elif ! launch_app; then
  fail_case "The app did not launch."
else
  printf sentinel | pbcopy
  defaults write "$DOMAIN" output.restoreClipboardAfterTyping -bool true
  osascript -e 'tell application "TextEdit" to activate' >/dev/null 2>&1
  errors=()
  accessibility_log_value 1 10 || errors+=("launch log did not report trusted=1")
  cli start >/dev/null 2>&1 || errors+=("start failed")
  poll_status listening 10 >/dev/null || errors+=("session did not reach listening")
  sleep 1.1
  cli stop >/dev/null 2>&1 || errors+=("stop failed")
  poll_status idle 10 >/dev/null || errors+=("session did not finish")
  textedit_text >"$CASE_DIR/textedit.txt" 2>"$CASE_DIR/textedit.err" || errors+=("TextEdit read-back failed")
  pbpaste >"$CASE_DIR/clipboard.txt"
  [[ "$(cat "$CASE_DIR/textedit.txt")" == "$EXPECTED_TEXT" ]] || errors+=("TextEdit did not contain the refined transcript")
  [[ "$(cat "$CASE_DIR/clipboard.txt")" == sentinel ]] || errors+=("clipboard sentinel was not restored")
  if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "With trusted=1, TextEdit received the exact refined transcript and the clipboard sentinel was restored."; fi
fi

baseline_reset
case_begin M23
if [[ "$TCC_READY" != 1 ]]; then
  block_case "The runner could not seed microphone TCC."
else
  errors=()
  seed_tcc kTCCServiceMicrophone 0 >"$CASE_DIR/mic-deny-seed.txt" 2>&1 && restart_tcc || errors+=("microphone deny row could not be seeded")
  if (( ! ${#errors[@]} )); then
    launch_app 1 || errors+=("denied-world app launch failed")
    phase_capture before
    cli start >"$CASE_DIR/denied-start.out" 2>&1 || true
    poll_status_one_of 5 idle error >"$CASE_DIR/denied-status.txt" || errors+=("denied microphone path hung")
    panel_visible "$CASE_DIR/before.png" mic-denied-error || errors+=("denied microphone did not show a visible error pill")
  fi
  stop_app
  seed_tcc kTCCServiceMicrophone 2 >"$CASE_DIR/mic-allow-seed.txt" 2>&1 && restart_tcc || errors+=("microphone allow row could not be seeded")
  if (( ! ${#errors[@]} )); then
    launch_app 1 || errors+=("allowed-world app launch failed")
    cli start >"$CASE_DIR/allowed-start.out" 2>&1 || errors+=("allowed start failed")
    poll_status listening 10 >"$CASE_DIR/allowed-status.txt" || errors+=("real CoreAudio path did not reach listening")
    grep -q 'audio capture started' "$(app_log_path)" || errors+=("audio pipeline start was not logged")
    cli stop >/dev/null 2>&1 || true
    poll_status idle 10 >/dev/null || errors+=("real CoreAudio path did not stop cleanly")
  fi
  if [[ " ${errors[*]} " == *"could not be seeded"* ]]; then block_case "$(IFS='; '; echo "${errors[*]}")"; elif (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "Denied microphone access failed fast without a prompt; allowed BlackHole input reached Listening and stopped cleanly."; fi
fi

baseline_reset
case_begin M25
if ! launch_app; then
  fail_case "The first instance did not launch."
else
  open -n "$APP_BUNDLE" >"$CASE_DIR/second-open.out" 2>"$CASE_DIR/second-open.err" || true
  for _ in {1..25}; do [[ "$(pgrep -x speecher | wc -l | tr -d ' ')" == 1 ]] && window_matches normal "$CASE_DIR/windows-settings.json" && break; sleep 0.2; done
  if [[ "$(pgrep -x speecher | wc -l | tr -d ' ')" != 1 ]]; then fail_case "The second GUI launch left more than one Speecher process."; elif ! window_matches normal "$CASE_DIR/windows-settings-final.json"; then fail_case "The second launch did not bring up the settings window."; else pass_case "The second GUI launch forwarded showMain and left exactly one process with settings onscreen."; fi
fi

baseline_reset
case_begin M26
if ! launch_app; then
  fail_case "The app did not launch."
else
  errors=()
  [[ "$(cli status | tail -1)" == idle ]] || errors+=("initial status was not idle")
  cli start >"$CASE_DIR/start.out" 2>&1 || errors+=("start failed")
  poll_status listening 10 >"$CASE_DIR/listening.txt" || errors+=("status did not reach listening")
  cli stop >"$CASE_DIR/stop.out" 2>&1 || errors+=("stop failed")
  poll_status idle 10 >"$CASE_DIR/idle.txt" || errors+=("status did not return idle")
  cli settings >"$CASE_DIR/settings.out" 2>&1 || errors+=("settings command failed")
  for _ in {1..25}; do window_matches normal "$CASE_DIR/windows-settings.json" && break; sleep 0.2; done
  window_matches normal "$CASE_DIR/windows-settings-final.json" || errors+=("settings window did not appear")
  if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "CLI status, start, stop, and settings completed the expected state and window round-trip."; fi
fi

baseline_reset
case_begin M27
if ! launch_app; then
  fail_case "The initial instance did not launch."
else
  kill -9 "$APP_PID" >/dev/null 2>&1 || true
  wait "$APP_PID" 2>/dev/null || true
  APP_PID=
  if ! launch_app; then fail_case "Relaunch after SIGKILL did not recover within 15 seconds."; elif [[ "$(cli status | tail -1)" != idle ]]; then fail_case "The recovered instance did not answer status."; elif tail -100 "$(app_log_path)" | grep -q 'Another Speecher instance'; then fail_case "The relaunch logged a duplicate-instance error."; else pass_case "The app removed the stale local socket and relaunched to an answering idle instance."; fi
fi

baseline_reset
case_begin M29
if [[ "$TCC_READY" != 1 ]]; then
  block_case "TextEdit and Accessibility automation could not be prepared."
elif ! launch_app; then
  fail_case "The app did not launch."
else
  errors=()
  printf '%s\n' automatic direct_insert mac-paste qt-clipboard >"$CASE_DIR/macos-methods.txt"
  if grep -nE 'Ydotool|WlCopy' src/frontend/mac/MacCustomRows.cpp >"$CASE_DIR/linux-method-source.txt"; then
    grep -q 'No ydotool entry' "$CASE_DIR/linux-method-source.txt" || errors+=("macOS source appears to offer a Linux-only method")
  fi
  for method in automatic direct_insert mac-paste qt-clipboard; do
    textedit_reset >/dev/null 2>&1 || { errors+=("TextEdit reset failed for $method"); continue; }
    printf old | pbcopy
    defaults write "$DOMAIN" output.method "$method"
    stop_app
    launch_app || { errors+=("launch failed for $method"); continue; }
    osascript -e 'tell application "TextEdit" to activate' >/dev/null 2>&1
    cli start >/dev/null 2>&1
    poll_status listening 10 >/dev/null || { errors+=("$method did not listen"); continue; }
    sleep 1.1
    cli stop >/dev/null 2>&1
    poll_status idle 10 >/dev/null || { errors+=("$method did not finish"); continue; }
    textedit_text >"$CASE_DIR/$method-textedit.txt" 2>/dev/null || true
    pbpaste >"$CASE_DIR/$method-clipboard.txt"
    if [[ "$method" == qt-clipboard ]]; then
      [[ "$(cat "$CASE_DIR/$method-clipboard.txt")" == "$EXPECTED_TEXT" ]] || errors+=("$method clipboard text differed")
    else
      [[ "$(cat "$CASE_DIR/$method-textedit.txt")" == "$EXPECTED_TEXT" ]] || errors+=("$method target text differed")
    fi
  done
  if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "All four macOS output methods delivered byte-identical text, and the macOS choices excluded ydotool and wl-copy."; fi
fi

baseline_reset
case_begin M20
if ! launch_app; then
  fail_case "The app did not launch."
  record_verdict M19 BLOCKED "M20 could not create the long-uptime precondition."
else
  phase_capture before
  initial_windows="$(window_count "$CASE_DIR/windows-initial.json")"
  printf 'cycle,rss_kib\n' >"$CASE_DIR/rss.csv"
  errors=()
  m19_ok=0
  for cycle in {1..30}; do
    if ! run_cycle "$CASE_DIR/before.png" "cycle-$cycle"; then errors+=("cycle $cycle failed"); break; fi
    if [[ "$cycle" =~ ^(1|5|10|15|20|25|30)$ ]]; then
      rss="$(ps -o rss= -p "$APP_PID" | tr -d ' ')"
      printf '%s,%s\n' "$cycle" "$rss" >>"$CASE_DIR/rss.csv"
    fi
    [[ "$cycle" == 30 ]] && m19_ok=1
  done
  final_windows="$(window_count "$CASE_DIR/windows-final.json")"
  [[ "$initial_windows" == "$final_windows" ]] || errors+=("window count changed from $initial_windows to $final_windows")
  leaks --nocontext "$APP_PID" >"$CASE_DIR/leaks.txt" 2>&1 || true
  leaked_bytes="$(sed -n 's/.*for \([0-9,][0-9,]*\) total leaked bytes.*/\1/p' "$CASE_DIR/leaks.txt" | tail -1 | tr -d ',')"
  [[ -n "$leaked_bytes" ]] || errors+=("leaks total could not be parsed")
  [[ -z "$leaked_bytes" || "$leaked_bytes" -lt 262144 ]] || errors+=("leaks reported $leaked_bytes bytes")
  rss5="$(awk -F, '$1==5 {print $2}' "$CASE_DIR/rss.csv")"
  rss30="$(awk -F, '$1==30 {print $2}' "$CASE_DIR/rss.csv")"
  if [[ -z "$rss5" || -z "$rss30" ]] || ! awk -v a="$rss30" -v b="$rss5" 'BEGIN {exit !(a < 1.3*b)}'; then errors+=("cycle-30 RSS $rss30 KiB was not below 1.3x cycle-5 RSS $rss5 KiB"); fi
  if [[ "$m19_ok" == 1 ]]; then record_verdict M19 PASS "The panel still satisfied the full visibility predicate on cycle 30."; else record_verdict M19 FAIL "The cycle-30 panel precondition did not complete."; fi
  if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "Thirty cycles kept RSS below 1.3x the cycle-5 value, leaks below 256 KiB, and the window count stable."; fi
fi

quit_case() {
  local id="$1" mode="$2"
  baseline_reset
  case_begin "$id"
  if ! launch_app; then fail_case "The app did not launch."; return; fi
  if [[ "$mode" == completed ]]; then
    cli start >/dev/null 2>&1; poll_status listening 10 >/dev/null; sleep 1.1; cli stop >/dev/null 2>&1; poll_status idle 10 >/dev/null
  elif [[ "$mode" == listening ]]; then
    cli start >/dev/null 2>&1; poll_status listening 10 >/dev/null
  else
    cli setup >/dev/null 2>&1
    for _ in {1..25}; do window_matches normal "$CASE_DIR/windows-setup.json" && break; sleep 0.2; done
    osascript -e 'tell application "System Events" to key code 53' >/dev/null 2>&1 || true
  fi
  osascript -e 'tell application id "io.github.firemonster612.speecher" to quit' >"$CASE_DIR/quit.out" 2>"$CASE_DIR/quit.err" || true
  exited=0
  for _ in {1..50}; do if ! kill -0 "$APP_PID" >/dev/null 2>&1; then exited=1; break; fi; sleep 0.2; done
  wait "$APP_PID" 2>/dev/null
  rc=$?
  APP_PID=
  crash_count="$(find "$HOME/Library/Logs/DiagnosticReports" -type f -iname '*speecher*' -mmin -1 2>/dev/null | wc -l | tr -d ' ')"
  if [[ "$exited" != 1 ]]; then fail_case "The process did not exit within 10 seconds."; elif [[ "$rc" != 0 ]]; then fail_case "The directly launched process exited with code $rc."; elif (( crash_count > 0 )); then fail_case "A Speecher diagnostic report appeared after quit."; else pass_case "The directly launched process exited 0 within 10 seconds without a crash report after $mode."; fi
}

quit_case M30 completed
quit_case M31 listening
quit_case M32 setup

case_begin M33
block_case "Cut first by the plan's overrun ladder; the target has no permitted env-gated local-appcast override, so a deterministic local feed precondition could not be created."

finalize_run
