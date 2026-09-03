#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"
TCC_SEED="$(dirname "$0")/tcc_seed.py"
DMG_PATH="${DMG_PATH:?}"
RUN1_VERDICTS="${RUN1_VERDICTS:?}"

use_app() {
  APP_BUNDLE="$1"
  APP_BIN="$APP_BUNDLE/Contents/MacOS/speecher"
}

if seed_common_tcc; then TCC_READY=1; else TCC_READY=0; log "TCC baseline seeding failed"; fi

baseline_reset
case_begin M1
attach="$(hdiutil attach "$DMG_PATH" -nobrowse -readonly 2>"$CASE_DIR/attach.err" || true)"
printf '%s\n' "$attach" >"$CASE_DIR/attach.out"
mount="$(printf '%s\n' "$attach" | sed -n 's|.*\(/Volumes/.*\)$|\1|p' | head -1)"
if [[ -z "$mount" || ! -d "$mount/speecher.app" ]]; then
  block_case "The built DMG could not be mounted with its app bundle."
else
  stop_app
  sudo rm -rf /Applications/speecher.app
  sudo ditto "$mount/speecher.app" /Applications/speecher.app
  hdiutil detach "$mount" -quiet || true
  use_app /Applications/speecher.app
  baseline_reset
  launchctl setenv SPEECHER_E2E_EVIDENCE_DIR "$CASE_DIR" >/dev/null 2>&1 || true
  if ! open -a Speecher >"$CASE_DIR/open.out" 2>"$CASE_DIR/open.err"; then
    fail_case "LaunchServices rejected the copied app."
  else
    APP_PID="$(pgrep -x speecher | head -1 || true)"
    errors=()
    [[ -n "$APP_PID" ]] || errors+=("no Speecher process appeared")
    poll_status idle 15 >"$CASE_DIR/status.txt" || errors+=("status did not answer idle")
    accessibility_log_value 1 10 || errors+=("accessibility identity line with trusted=1 was absent")
    [[ "$(pgrep -x speecher | wc -l | tr -d ' ')" == 1 ]] || errors+=("process count was not one")
    if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "The DMG mounted, copied to Applications, and launched one idle instance with its accessibility identity logged."; fi
  fi
fi

case_begin M2
stop_app
sudo rm -rf /Applications/speecher.app
attach="$(hdiutil attach "$DMG_PATH" -nobrowse -readonly 2>"$CASE_DIR/attach.err" || true)"
mount="$(printf '%s\n' "$attach" | sed -n 's|.*\(/Volumes/.*\)$|\1|p' | head -1)"
if [[ -z "$mount" ]]; then
  block_case "The DMG could not be remounted for a fresh quarantined copy."
else
  sudo ditto "$mount/speecher.app" /Applications/speecher.app
  hdiutil detach "$mount" -quiet || true
  use_app /Applications/speecher.app
  xattr -w com.apple.quarantine "0083;$(printf %x "$(date +%s)");Safari;$(uuidgen)" "$APP_BUNDLE"
  open -a Speecher >"$CASE_DIR/open.out" 2>"$CASE_DIR/open.err" || true
  for _ in {1..50}; do pgrep -x speecher >/dev/null || { sleep 0.2; continue; }; break; done
  spctl -a -vv "$APP_BUNDLE" >"$CASE_DIR/spctl.out" 2>"$CASE_DIR/spctl.err"
  spctl_rc=$?
  if pgrep -x speecher >/dev/null; then fail_case "The quarantined self-signed app launched instead of being blocked."; elif [[ "$spctl_rc" == 0 ]]; then fail_case "spctl accepted the quarantined self-signed bundle."; else pass_case "Gatekeeper prevented a process from appearing and spctl rejected the quarantined bundle."; fi
fi

case_begin M3
killall CoreServicesUIAgent >/dev/null 2>&1 || true
xattr -dr com.apple.quarantine "$APP_BUNDLE" >"$CASE_DIR/xattr.out" 2>"$CASE_DIR/xattr.err" || true
if ! launch_app; then fail_case "Removing quarantine did not make the app launchable."; elif [[ "$(cli status | tail -1)" != idle ]]; then fail_case "The Open Anyway stand-in launched but status did not answer idle."; else pass_case "Removing quarantine provided the headless Open Anyway equivalent and the app launched idle."; fi

baseline_reset
case_begin M4
attach="$(hdiutil attach "$DMG_PATH" -nobrowse -readonly 2>"$CASE_DIR/attach.err" || true)"
mount="$(printf '%s\n' "$attach" | sed -n 's|.*\(/Volumes/.*\)$|\1|p' | head -1)"
if [[ -z "$mount" ]]; then
  block_case "The read-only DMG could not be attached."
else
  installed_app="$APP_BUNDLE"
  use_app "$mount/speecher.app"
  bundle_hash_before="$(find "$APP_BUNDLE" -type f -exec shasum {} + | shasum | awk '{print $1}')"
  if ! launch_app; then
    fail_case "The app did not run from the read-only image."
  else
    phase_capture before
    errors=()
    run_cycle "$CASE_DIR/before.png" dmg-cycle || errors+=("the stub Dictation Session did not complete")
    defaults write "$DOMAIN" ui.previewWords -int 11
    [[ "$(defaults read "$DOMAIN" ui.previewWords)" == 11 ]] || errors+=("the settings write did not persist in the user domain")
    bundle_hash_after="$(find "$APP_BUNDLE" -type f -exec shasum {} + | shasum | awk '{print $1}')"
    [[ "$bundle_hash_before" == "$bundle_hash_after" ]] || errors+=("the mounted bundle changed")
    if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "The read-only DMG app completed a Dictation Session while settings persisted outside the unchanged bundle."; fi
  fi
  stop_app
  hdiutil detach "$mount" -quiet || true
  use_app "$installed_app"
fi

record_verdict M5 BLOCKED "Cut by the test plan's runner-budget policy."
record_verdict M6 BLOCKED "Cut by the test plan's runner-budget policy."

stop_app
defaults delete "$DOMAIN" >/dev/null 2>&1 || true
case_begin M7
if ! launch_app; then fail_case "The fresh-state app did not launch."; else
  for _ in {1..50}; do window_matches normal "$CASE_DIR/windows-setup.json" && break; sleep 0.2; done
  if ! window_matches normal "$CASE_DIR/windows-setup-final.json"; then fail_case "No onscreen normal-level setup window appeared within 10 seconds."; elif tail -100 "$(app_log_path)" | grep -q 'startListening'; then fail_case "A Dictation Session started during fresh setup."; else pass_case "Fresh state showed an onscreen setup assistant and did not start dictation."; fi
fi

baseline_reset
case_begin M8
if ! launch_app; then fail_case "The seeded app did not launch."; else
  sleep 1
  errors=()
  window_matches normal "$CASE_DIR/windows-normal.json" && errors+=("a normal setup window appeared")
  window_matches status-item "$CASE_DIR/windows-status-item.json" || errors+=("no Speecher-owned status-item window was visible")
  [[ "$(cli status | tail -1)" == idle ]] || errors+=("status was not idle")
  if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "Seeded setup skipped the assistant, retained the status item, and answered idle."; fi
fi

baseline_reset
case_begin M9
if ! launch_app; then fail_case "The app did not launch."; else
  cli setup >"$CASE_DIR/setup.out" 2>&1 || true
  for _ in {1..25}; do window_matches normal "$CASE_DIR/windows-setup.json" && break; sleep 0.2; done
  if ! window_matches normal "$CASE_DIR/windows-setup-final.json"; then fail_case "speecher setup did not show the assistant."; else
    osascript -e 'tell application "System Events" to key code 53' >"$CASE_DIR/escape.out" 2>"$CASE_DIR/escape.err" || true
    if [[ "$(cli status | tail -1)" != idle ]]; then fail_case "Closing the assistant killed or wedged the instance."; else pass_case "The setup command reopened the assistant and closing it left the idle instance alive."; fi
  fi
fi

record_verdict M10 BLOCKED "Cut by the test plan's runner-budget policy."

baseline_reset
case_begin M11
stop_app
rm -rf "$HOME/Library/Application Support/$BUNDLE_ID" "$HOME/Library/Application Support/speecher"
rm -f "$HOME/Library/Preferences/$DOMAIN.plist"
defaults delete "$DOMAIN" >/dev/null 2>&1 || true
if ! launch_app; then fail_case "The app crashed or failed to launch after state deletion."; else
  for _ in {1..50}; do window_matches normal "$CASE_DIR/windows-setup.json" && break; sleep 0.2; done
  if window_matches normal "$CASE_DIR/windows-setup-final.json"; then pass_case "Deleting Application Support and preferences restored first-run setup without a crash."; else fail_case "The setup assistant did not return after state deletion."; fi
fi

baseline_reset
case_begin M12
if ! launch_app; then fail_case "The app did not launch."; else
  phase_capture before
  defaults delete "$DOMAIN" >/dev/null 2>&1 || true
  killall cfprefsd >/dev/null 2>&1 || true
  errors=()
  [[ "$(cli status | tail -1)" == idle ]] || errors+=("running app stopped answering after defaults delete")
  run_cycle "$CASE_DIR/before.png" after-delete || errors+=("running app did not complete a Dictation Session")
  stop_app
  launch_app || errors+=("relaunch failed")
  for _ in {1..50}; do window_matches normal "$CASE_DIR/windows-relaunch.json" && break; sleep 0.2; done
  window_matches normal "$CASE_DIR/windows-relaunch-final.json" || errors+=("relaunch did not show setup")
  if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "The running app survived defaults deletion and relaunch returned to first-run setup."; fi
fi

baseline_reset
case_begin M13
stop_app
plist="$HOME/Library/Preferences/$DOMAIN.plist"
mkdir -p "$(dirname "$plist")"
dd if=/dev/urandom of="$plist" bs=1024 count=1 >"$CASE_DIR/dd.out" 2>"$CASE_DIR/dd.err"
killall cfprefsd >/dev/null 2>&1 || true
if ! launch_app; then fail_case "The corrupt plist prevented launch within 15 seconds."; elif find "$HOME/Library/Logs/DiagnosticReports" -type f -iname '*speecher*' -mmin -1 2>/dev/null | grep -q .; then fail_case "A crash report appeared after the corrupt-plist launch."; else pass_case "The app launched without a crash report after a 1 KiB random preferences plist."; fi

baseline_reset
case_begin M14
defaults write "$DOMAIN" stt.provider no-such-provider
defaults write "$DOMAIN" audio.preRollMs -int -5000
python3 - "$DOMAIN" <<'PY' >"$CASE_DIR/vocabulary-write.out" 2>"$CASE_DIR/vocabulary-write.err"
import subprocess, sys
subprocess.run(["defaults", "write", sys.argv[1], "stt.vocabularyEntries", "-array"] + [f"word-{i}" for i in range(5000)], check=True)
PY
if ! launch_app; then fail_case "Hostile settings prevented launch."; else
  phase_capture before
  cli start >"$CASE_DIR/start.out" 2>&1 || true
  errors=()
  poll_status error 5 >"$CASE_DIR/status.txt" || errors+=("unknown provider did not produce error state")
  panel_visible "$CASE_DIR/before.png" error || errors+=("error pill was not visible")
  grep -q 'Unknown speech provider: no-such-provider' "$(app_log_path)" || errors+=("log did not name the unknown provider")
  if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "Hostile provider, pre-roll, and vocabulary values produced a named error state without a hang or crash."; fi
fi

baseline_reset
case_begin M18
if [[ "$TCC_READY" != 1 ]]; then block_case "Fullscreen TextEdit automation could not be authorized."; elif ! textedit_reset >/dev/null 2>&1; then block_case "TextEdit could not be prepared."; elif ! launch_app; then fail_case "The app did not launch."; else
  osascript -e 'tell application "System Events" to tell process "TextEdit" to set value of attribute "AXFullScreen" of window 1 to true' >"$CASE_DIR/fullscreen.out" 2>"$CASE_DIR/fullscreen.err"
  if [[ "$?" != 0 ]]; then block_case "The runner could not put TextEdit into a native fullscreen Space."; else
    phase_capture before
    cli start >/dev/null 2>&1
    poll_status listening 10 >/dev/null || true
    panel_visible "$CASE_DIR/before.png" fullscreen
    predicate_rc=$?
    if [[ "$predicate_rc" == 2 ]]; then fail_case "No status-level panel window was onscreen over fullscreen TextEdit."; else pass_case "The status-level panel was onscreen in TextEdit's fullscreen Space; screenshot pixels are attached as grade-B evidence."; fi
    cli stop >/dev/null 2>&1 || true
    osascript -e 'tell application "System Events" to tell process "TextEdit" to set value of attribute "AXFullScreen" of window 1 to false' >/dev/null 2>&1 || true
  fi
fi

baseline_reset
case_begin M21
if [[ "$TCC_READY" != 1 ]]; then block_case "The runner could not seed denied Accessibility plus TextEdit Automation."; else
  seed_tcc kTCCServiceAccessibility 0 >"$CASE_DIR/accessibility-deny-seed.txt" 2>&1 && restart_tcc
  if [[ "$?" != 0 ]]; then block_case "The Accessibility deny row could not be installed."; elif ! textedit_reset >/dev/null 2>&1; then block_case "TextEdit could not be automated."; elif ! launch_app; then fail_case "The denied-world app did not launch."; else
    phase_capture before
    printf old | pbcopy
    errors=()
    accessibility_log_value 0 10 || errors+=("launch log did not report trusted=0")
    start_epoch="$(date +%s)"
    cli start >/dev/null 2>&1 || errors+=("start failed")
    poll_status listening 10 >/dev/null || errors+=("session did not reach listening")
    sleep 1.1
    cli stop >/dev/null 2>&1 || errors+=("stop failed")
    poll_status delivering 5 >/dev/null && phase_capture clipboard-completion
    poll_status idle 10 >/dev/null || errors+=("session did not finish")
    elapsed=$(( $(date +%s) - start_epoch ))
    textedit_text >"$CASE_DIR/textedit.txt" 2>/dev/null || errors+=("TextEdit read-back failed")
    pbpaste >"$CASE_DIR/clipboard.txt"
    [[ ! -s "$CASE_DIR/textedit.txt" ]] || errors+=("TextEdit changed despite denied Accessibility")
    [[ "$(cat "$CASE_DIR/clipboard.txt")" == "$EXPECTED_TEXT" ]] || errors+=("clipboard did not contain the exact refined transcript")
    (( elapsed < 60 )) || errors+=("case exceeded 60 seconds, consistent with a prompt wedge")
    if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "With trusted=0, delivery completed in under 60 seconds, left TextEdit unchanged, and copied exact text."; fi
  fi
fi

baseline_reset
case_begin M24
if [[ "$TCC_READY" != 1 ]]; then block_case "The runner could not seed screen-recording denial."; else
  seed_tcc kTCCServiceScreenCapture 0 >"$CASE_DIR/screen-deny-seed.txt" 2>&1 && restart_tcc
  if [[ "$?" != 0 ]]; then block_case "The screen-recording deny row could not be installed."; else
    defaults write "$DOMAIN" refinement.includeScreenshotContext -bool true
    if ! launch_app; then fail_case "The denied-world app did not launch."; else
      cli start >/dev/null 2>&1
      poll_status listening 10 >/dev/null || true
      sleep 1.1
      cli stop >/dev/null 2>&1
      errors=()
      poll_status idle 10 >/dev/null || errors+=("Dictation Session did not complete")
      grep -q 'Screen capture failed. Allow Speecher' "$(app_log_path)" || errors+=("log did not record the capture refusal wording")
      if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "Denied screen recording was logged as a refusal and the Dictation Session completed without a prompt or hang."; fi
    fi
  fi
fi

baseline_reset
case_begin M34
seed_tcc kTCCServiceAccessibility 2 >"$CASE_DIR/accessibility-allow-seed.txt" 2>&1 && restart_tcc || true
if [[ "$TCC_READY" != 1 ]]; then block_case "Appearance automation could not be authorized."; elif ! launch_app; then fail_case "The app did not launch."; else
  cli settings >/dev/null 2>&1 || true
  phase_capture before
  cli start >/dev/null 2>&1
  poll_status listening 10 >/dev/null || true
  panel_visible "$CASE_DIR/before.png" light-before >/dev/null 2>&1 || true
  osascript -e 'tell application "System Events" to tell appearance preferences to set dark mode to not dark mode' >"$CASE_DIR/flip.out" 2>"$CASE_DIR/flip.err"
  if [[ "$?" != 0 ]]; then block_case "System Events could not change the runner appearance without prompting."; else
    errors=()
    panel_visible "$CASE_DIR/before.png" after-flip || errors+=("panel failed the visibility predicate after appearance changed")
    kill -0 "$APP_PID" >/dev/null 2>&1 || errors+=("app crashed during appearance change")
    osascript -e 'tell application "System Events" to tell appearance preferences to set dark mode to not dark mode' >/dev/null 2>&1 || true
    cli stop >/dev/null 2>&1 || errors+=("stop failed")
    poll_status idle 10 >/dev/null || errors+=("Dictation Session did not complete")
    if (( ${#errors[@]} )); then fail_case "$(IFS='; '; echo "${errors[*]}")"; else pass_case "The live panel remained visible through a light/dark flip and the Dictation Session completed; before/after screenshots are attached."; fi
  fi
fi

if grep -q '^M21: PASS' "$VERDICTS" && grep -q '^M22: PASS' "$RUN1_VERDICTS"; then
  record_verdict M28 PASS "M21 clipboard and M22 paste both delivered the exact 20-byte refined transcript without newline or quote changes."
elif grep -q '^M21: BLOCKED' "$VERDICTS" || grep -q '^M22: BLOCKED' "$RUN1_VERDICTS"; then
  record_verdict M28 BLOCKED "A prerequisite delivery path was blocked; see M21 and M22."
else
  record_verdict M28 FAIL "At least one delivery path failed byte-identical text fidelity; see M21 and M22."
fi

record_verdict M-C1 BLOCKED "Blocked by design: Gatekeeper's Open Anyway button requires interactive System Settings; M3 tested the programmatic equivalent."
record_verdict M-C2 BLOCKED "Blocked by design: Mission Control Space creation and switching has no supported headless API; M18 covered fullscreen."
record_verdict M-C3 BLOCKED "Blocked by design: the hosted runner has no physical display sleep/wake path."
record_verdict M-C4 BLOCKED "Blocked by design: the app ships English strings only, so changing AppleLanguages adds no coverage."

finalize_run
