#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"
TCC_SEED="$(dirname "$0")/tcc_seed.py"

if ! seed_common_tcc; then
  log "TCC baseline seeding failed; continuing because these cases do not depend on seeded real-audio access"
fi
probe_desktop_capture

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
  [[ "$visible" == 1 ]] || errors+=("panel did not satisfy the window visibility predicate within 2 seconds")
  poll_status listening 10 >"$CASE_DIR/listening-status.txt" || errors+=("session did not reach listening")
  phase_capture listening
  cli stop >"$CASE_DIR/stop.out" 2>&1 || errors+=("stop command failed")
  if poll_status refining 2 >"$CASE_DIR/refining-status.txt"; then phase_capture refining; else phase_capture refining-missed; fi
  if poll_status delivering 3 >"$CASE_DIR/delivered-status.txt"; then phase_capture delivered; else phase_capture delivered-missed; fi
  poll_status idle 10 >"$CASE_DIR/idle-status.txt" || errors+=("session did not return to idle")
  phase_capture hidden
  panel_gone || errors+=("panel remained onscreen after completion")
  panel_event_exists qt-show-emit || errors+=("Qt show emission was not recorded")
  panel_event_exists bridge-show false || errors+=("bridge show block was nil or unrecorded")
  panel_event_exists show || errors+=("Swift show receipt was not recorded")
  panel_event_exists position || errors+=("panel position evidence was not recorded")
  panel_event_exists presented || errors+=("post-ordering panel state was not recorded")
  if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "The panel appeared, crossed every instrumented layer, and hid after delivery."; fi
fi

baseline_reset
case_begin M16
if ! launch_app; then
  fail_case "The app did not launch."
else
  phase_capture before
  initial_windows="$(window_count "$CASE_DIR/windows-initial.json")"
  failed_cycle=
  for cycle in {1..5}; do
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
    fail_case "The process window count changed from $initial_windows to $final_windows after 5 cycles."
  else
    pass_case "All 5 Dictation Sessions showed one panel, hid it, returned idle, and kept the window count stable."
  fi
fi

baseline_reset
case_begin M17
defaults write "$DOMAIN" stt.provider no-such-provider
unset SPEECHER_E2E_STUB SPEECHER_E2E_SKIP_MIC_GATE
launchctl unsetenv SPEECHER_E2E_STUB >/dev/null 2>&1 || true
launchctl unsetenv SPEECHER_E2E_SKIP_MIC_GATE >/dev/null 2>&1 || true
if ! launch_app; then
  fail_case "The app did not launch for the error-path test."
else
  phase_capture before
  cli start >"$CASE_DIR/start.out" 2>&1 || true
  errors=()
  cli status >"$CASE_DIR/error-status.txt" 2>&1 || true
  visible=0
  for attempt in {1..10}; do
    if panel_visible "$CASE_DIR/before.png" "error-$attempt"; then visible=1; break; fi
    sleep 0.2
  done
  [[ "$visible" == 1 ]] || errors+=("error panel did not satisfy the window visibility predicate")
  panel_event_exists error || errors+=("Swift error receipt was not recorded")
  panel_event_exists position || errors+=("error panel position was not recorded")
  panel_event_exists presented || errors+=("post-ordering error panel state was not recorded")
  bounded_osascript -e 'tell application "System Events" to tell process "speecher" to return exists button "Dismiss" of window 1' \
    >"$CASE_DIR/dismiss-ax.txt" 2>"$CASE_DIR/dismiss-ax.err" || true
  cli stop >"$CASE_DIR/stop.out" 2>&1 || true
  poll_status idle 5 >"$CASE_DIR/idle-status.txt" || true
  panel_gone >"$CASE_DIR/panel-gone.out" 2>&1 || true
  if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "The clean unknown-provider launch produced an error pill that satisfied the M15-style visibility predicates."; fi
fi

baseline_reset
case_begin M20
if ! launch_app; then
  fail_case "The app did not launch."
  record_verdict M19 BLOCKED "M20 could not create the cycle-15 precondition."
else
  phase_capture before
  initial_windows="$(window_count "$CASE_DIR/windows-initial.json")"
  printf 'cycle,rss_kib\n' >"$CASE_DIR/rss.csv"
  errors=()
  m19_ok=0
  for cycle in {1..15}; do
    if ! run_cycle "$CASE_DIR/before.png" "cycle-$cycle"; then errors+=("cycle $cycle failed"); break; fi
    if [[ "$cycle" =~ ^(1|5|10|15)$ ]]; then
      rss="$(ps -o rss= -p "$APP_PID" | tr -d ' ')"
      printf '%s,%s\n' "$cycle" "$rss" >>"$CASE_DIR/rss.csv"
    fi
    [[ "$cycle" == 15 ]] && m19_ok=1
  done
  final_windows="$(window_count "$CASE_DIR/windows-final.json")"
  [[ "$initial_windows" == "$final_windows" ]] || errors+=("window count changed from $initial_windows to $final_windows")
  leaks --nocontext "$APP_PID" >"$CASE_DIR/leaks.txt" 2>&1 || true
  leaked_bytes="$(sed -n 's/.*for \([0-9,][0-9,]*\) total leaked bytes.*/\1/p' "$CASE_DIR/leaks.txt" | tail -1 | tr -d ',')"
  [[ -n "$leaked_bytes" ]] || errors+=("leaks total could not be parsed")
  [[ -z "$leaked_bytes" || "$leaked_bytes" -lt 262144 ]] || errors+=("leaks reported $leaked_bytes bytes")
  rss5="$(awk -F, '$1==5 {print $2}' "$CASE_DIR/rss.csv")"
  rss15="$(awk -F, '$1==15 {print $2}' "$CASE_DIR/rss.csv")"
  if [[ -z "$rss5" || -z "$rss15" ]] || ! awk -v a="$rss15" -v b="$rss5" 'BEGIN {exit !(a < 1.3*b)}'; then errors+=("cycle-15 RSS $rss15 KiB was not below 1.3x cycle-5 RSS $rss5 KiB"); fi
  if [[ "$m19_ok" == 1 ]]; then record_verdict M19 PASS "The panel still satisfied the visibility predicate on cycle 15."; else record_verdict M19 FAIL "The cycle-15 panel check did not complete."; fi
  if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "Fifteen cycles kept RSS below 1.3x the cycle-5 value, leaks below 256 KiB, and the window count stable."; fi
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
  if pgrep -x speecher >"$CASE_DIR/post-kill-processes.txt" 2>&1; then
    fail_case "A Speecher process survived SIGKILL."
  elif ! launch_app; then
    fail_case "Relaunch after SIGKILL did not recover within 15 seconds."
  elif [[ "$(cli status | tail -1)" != idle ]]; then
    fail_case "The recovered instance did not answer status."
  elif tail -c "+$CASE_LOG_OFFSET" "$(app_log_path)" | grep -q 'Another Speecher instance'; then
    fail_case "The relaunch logged a duplicate-instance error."
  else
    pass_case "The app removed the stale local socket and relaunched to an answering idle instance."
  fi
fi

quit_case() {
  local id="$1" mode="$2"
  baseline_reset
  case_begin "$id"
  if ! launch_app; then fail_case "The app did not launch."; return; fi
  errors=()
  if [[ "$mode" == completed ]]; then
    cli start >/dev/null 2>&1 || errors+=("start failed")
    poll_status listening 10 >/dev/null || errors+=("session did not reach listening")
    sleep 1.1
    cli stop >/dev/null 2>&1 || errors+=("stop failed")
    poll_status idle 10 >/dev/null || errors+=("session did not complete")
  else
    cli start >/dev/null 2>&1 || errors+=("start failed")
    poll_status listening 10 >/dev/null || errors+=("session did not reach listening")
  fi
  bounded_osascript -e 'tell application id "io.github.firemonster612.speecher" to quit' \
    >"$CASE_DIR/quit.out" 2>"$CASE_DIR/quit.err" || errors+=("quit Apple Event failed or timed out")
  exited=0
  for _ in {1..50}; do if ! kill -0 "$APP_PID" >/dev/null 2>&1; then exited=1; break; fi; sleep 0.2; done
  wait "$APP_PID" 2>/dev/null
  rc=$?
  APP_PID=
  crash_count="$(find "$HOME/Library/Logs/DiagnosticReports" -type f -iname '*speecher*' -mmin -1 2>/dev/null | wc -l | tr -d ' ')"
  [[ "$exited" == 1 ]] || errors+=("process did not exit within 10 seconds")
  [[ "$rc" == 0 ]] || errors+=("directly launched process exited with code $rc")
  (( crash_count == 0 )) || errors+=("a Speecher diagnostic report appeared after quit")
  if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "The directly launched process exited 0 within 10 seconds without a crash report after $mode."; fi
}

quit_case M30 completed
quit_case M31 listening

baseline_reset
case_begin M25
if ! launch_app; then
  fail_case "The first instance did not launch."
else
  open -n "$APP_BUNDLE" >"$CASE_DIR/second-open.out" 2>"$CASE_DIR/second-open.err" || true
  for _ in {1..25}; do [[ "$(pgrep -x speecher | wc -l | tr -d ' ')" == 1 ]] && window_matches normal "$CASE_DIR/windows-settings.json" && break; sleep 0.2; done
  if [[ "$(pgrep -x speecher | wc -l | tr -d ' ')" != 1 ]]; then fail_case "The second GUI launch left more than one Speecher process."; elif ! window_matches normal "$CASE_DIR/windows-settings-final.json"; then fail_case "The second launch did not bring up the settings window."; else pass_case "The second GUI launch forwarded showMain and left exactly one process with settings onscreen."; fi
fi

finalize_run
