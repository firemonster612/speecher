#!/usr/bin/env bash

# Provider-stats captures on the setup assistant: the Transcription and
# Refinement steps must show the registry's stats for whichever provider the
# picker names, and no stats when refinement is None. Scratch-branch-only.

source "$(dirname "$0")/common.sh"
TCC_SEED="$(dirname "$0")/tcc_seed.py"
ASSISTANT_WINDOW='Speecher Setup Assistant'
USER_TCC_DB="$HOME/Library/Application Support/com.apple.TCC/TCC.db"
SYSTEM_TCC_DB='/Library/Application Support/com.apple.TCC/TCC.db'
STEP_IDS=(welcome transcription microphone accessibility delivery refinement)

seed_setup_tcc() {
  python3 "$TCC_SEED" "$USER_TCC_DB" \
    kTCCServiceAppleEvents /usr/bin/osascript 2 com.apple.systemevents 1 || return 1
  python3 "$TCC_SEED" "$USER_TCC_DB" \
    kTCCServiceAppleEvents /usr/bin/osascript 2 "$BUNDLE_ID" 1 || return 1
  sudo python3 "$TCC_SEED" "$SYSTEM_TCC_DB" \
    kTCCServiceAccessibility /usr/bin/osascript 2 UNUSED 1 || return 1
  python3 "$TCC_SEED" "$USER_TCC_DB" kTCCServiceMicrophone "$BUNDLE_ID" 2 || return 1
  sudo launchctl kickstart -k system/com.apple.tccd || sudo killall tccd || true
}

fresh_reset() {
  stop_app
  defaults delete "$DOMAIN" >/dev/null 2>&1 || true
  defaults write "$BUNDLE_ID" SUEnableAutomaticChecks -bool false
  unset SPEECHER_E2E_STUB SPEECHER_E2E_SKIP_MIC_GATE SPEECHER_E2E_REAL_AUDIO
  launchctl unsetenv SPEECHER_E2E_STUB >/dev/null 2>&1 || true
  launchctl unsetenv SPEECHER_E2E_SKIP_MIC_GATE >/dev/null 2>&1 || true
}

launch_setup() {
  if pgrep -x speecher >"$CASE_DIR/prelaunch-processes.txt" 2>&1; then
    return 1
  fi
  mkdir -p "$CASE_DIR/pages"
  SPEECHER_E2E_SETUP_CAPTURE_DIR="$CASE_DIR/pages" \
    DYLD_FRAMEWORK_PATH="${QT_ROOT_DIR:-}/lib" \
    "$APP_BIN" >"$CASE_DIR/process.out" 2>&1 &
  APP_PID=$!
  poll_process 20
}

assistant_ui() {
  bounded_osascript -e "tell application \"System Events\" to tell process \"speecher\" to $1"
}

wait_for_assistant() {
  local deadline=$((SECONDS + 30))
  while (( SECONDS < deadline )); do
    if assistant_ui "get name of window \"$ASSISTANT_WINDOW\"" \
        >>"$CASE_DIR/assistant-ax.out" 2>&1; then
      return 0
    fi
    sleep 0.2
  done
  return 1
}

click_button() {
  # The root SwiftUI group exposes the navigation HStack's buttons in order:
  # Skip Setup, Back, Continue.
  local button
  case "$1" in
    "Skip Setup") button='first button' ;;
    Back) button='button 2' ;;
    Continue|Finish) button='last button' ;;
    *) return 1 ;;
  esac
  assistant_ui "click $button of group 1 of window \"$ASSISTANT_WINDOW\"" \
    >>"$CASE_DIR/clicks.out" 2>&1
}

wait_for_page_capture() {
  local index="$1" id="$2" count=0
  local file="$CASE_DIR/pages/step-$index-$id.png"
  while (( count < 50 )); do
    [[ -s "$file" ]] && return 0
    sleep 0.2
    count=$((count + 1))
  done
  return 1
}

# Continues from the current (first) step until the numbered step's PNG exists.
walk_to_step() {
  local target="$1" step
  for (( step = 1; step < target; step++ )); do
    wait_for_page_capture "$step" "${STEP_IDS[$((step - 1))]}" || return 1
    click_button Continue || return 1
    sleep 0.5
  done
  wait_for_page_capture "$target" "${STEP_IDS[$((target - 1))]}"
}

# Drives the step's pop-up button to the menu item $1. Each provider step has
# exactly one pop-up, and SwiftUI nests it in AX groups, so the search walks
# the window's entire contents for the first one rather than guessing a path.
drive_picker() {
  osascript - "$1" >>"$CASE_DIR/picker.out" 2>&1 <<'OSA' &
on run argv
  set targetValue to item 1 of argv
  tell application "System Events" to tell process "speecher"
    set allElements to entire contents of window "Speecher Setup Assistant"
    set thePopup to missing value
    repeat with e in allElements
      try
        if class of e is pop up button or class of e is menu button then
          set thePopup to e
          exit repeat
        end if
      end try
    end repeat
    if thePopup is missing value then
      set classNames to {}
      repeat with e in allElements
        try
          set end of classNames to (class of e as text)
        end try
      end repeat
      set AppleScript's text item delimiters to ", "
      error "no pop up buttons among " & (count of allElements) & " elements: " & (classNames as text)
    end if
    log "picker class: " & (class of thePopup as text)
    click thePopup
    delay 0.5
    click menu item targetValue of menu 1 of thePopup
  end tell
end run
OSA
  local pid=$! count=0
  while kill -0 "$pid" >/dev/null 2>&1 && (( count < 150 )); do
    sleep 0.2
    count=$((count + 1))
  done
  if kill -0 "$pid" >/dev/null 2>&1; then
    kill -9 "$pid" >/dev/null 2>&1 || true
    wait "$pid" 2>/dev/null || true
    return 124
  fi
  wait "$pid"
}

# Leaves and re-enters the current step so the capture seam writes it again.
recapture_step() {
  local index="$1" id="$2"
  rm -f "$CASE_DIR/pages/step-$index-$id.png"
  click_button Back || return 1
  sleep 0.7
  click_button Continue || return 1
  wait_for_page_capture "$index" "$id"
}

ocr() {
  swift - "$1" <<'SWIFT'
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
}

# The stats are real pixels or they are nothing: OCR the capture and demand the
# marker text.
expect_text() {
  local png="$1" marker="$2" text
  text="$(ocr "$png")"
  printf '%s: %s\n' "$(basename "$png")" "$text" >>"$CASE_DIR/ocr.txt"
  [[ "$text" == *"$marker"* ]]
}

expect_no_text() {
  local png="$1" marker="$2" text
  text="$(ocr "$png")"
  printf '%s: %s\n' "$(basename "$png")" "$text" >>"$CASE_DIR/ocr.txt"
  [[ "$text" != *"$marker"* ]]
}

if ! seed_setup_tcc; then
  record_verdict PROVIDER-TCC FAIL "The runner refused the TCC seeds the AX driver needs."
  exit 1
fi

# P1: the defaults (Claude Voice, OpenAI) show their stats on their steps.
fresh_reset
case_begin P1
if ! launch_setup || ! wait_for_assistant; then
  fail_case "The setup assistant did not appear on a fresh profile."
else
  errors=()
  walk_to_step 2 || errors+=("could not reach the transcription step")
  if (( ${#errors[@]} == 0 )); then
    cp "$CASE_DIR/pages/step-2-transcription.png" "$CASE_DIR/transcription-claude.png"
    expect_text "$CASE_DIR/transcription-claude.png" "Deepgram Nova 3" \
      || errors+=("the transcription step does not show the Claude Voice stats")
    for (( step = 2; step < 6; step++ )); do
      click_button Continue || errors+=("Continue failed on step $step")
      sleep 0.5
    done
    wait_for_page_capture 6 refinement || errors+=("could not reach the refinement step")
  fi
  if (( ${#errors[@]} == 0 )); then
    cp "$CASE_DIR/pages/step-6-refinement.png" "$CASE_DIR/refinement-openai.png"
    expect_text "$CASE_DIR/refinement-openai.png" "About 3 seconds" \
      || errors+=("the refinement step does not show the OpenAI stats")
  fi
  if (( ${#errors[@]} )); then
    fail_case "$(IFS='; '; echo "${errors[*]}")"
  else
    pass_case "Claude Voice and OpenAI stats render on their setup steps."
  fi
fi

# P2: pre-seeded ChatGPT Codex and Anthropic show their stats.
fresh_reset
defaults write "$DOMAIN" stt.provider codex
defaults write "$DOMAIN" refinement.provider anthropic
case_begin P2
if ! launch_setup || ! wait_for_assistant; then
  fail_case "The setup assistant did not appear with pre-seeded providers."
else
  errors=()
  walk_to_step 2 || errors+=("could not reach the transcription step")
  if (( ${#errors[@]} == 0 )); then
    cp "$CASE_DIR/pages/step-2-transcription.png" "$CASE_DIR/transcription-codex.png"
    expect_text "$CASE_DIR/transcription-codex.png" "GPT Live Transcribe" \
      || errors+=("the transcription step does not show the ChatGPT Codex stats")
    for (( step = 2; step < 6; step++ )); do
      click_button Continue || errors+=("Continue failed on step $step")
      sleep 0.5
    done
    wait_for_page_capture 6 refinement || errors+=("could not reach the refinement step")
  fi
  if (( ${#errors[@]} == 0 )); then
    cp "$CASE_DIR/pages/step-6-refinement.png" "$CASE_DIR/refinement-anthropic.png"
    expect_text "$CASE_DIR/refinement-anthropic.png" "Claude Sonnet" \
      || errors+=("the refinement step does not show the Anthropic stats")
  fi
  if (( ${#errors[@]} )); then
    fail_case "$(IFS='; '; echo "${errors[*]}")"
  else
    pass_case "ChatGPT Codex and Anthropic stats render on their setup steps."
  fi
fi

# P3: driving the pickers updates the stats live, and None hides the block.
fresh_reset
case_begin P3
if ! launch_setup || ! wait_for_assistant; then
  fail_case "The setup assistant did not appear for the picker-driving case."
else
  errors=()
  walk_to_step 2 || errors+=("could not reach the transcription step")
  if (( ${#errors[@]} == 0 )); then
    drive_picker "ChatGPT Codex" \
      || errors+=("could not drive the transcription picker to ChatGPT Codex")
    sleep 0.5
    recapture_step 2 transcription || errors+=("the transcription step was not recaptured")
  fi
  if (( ${#errors[@]} == 0 )); then
    cp "$CASE_DIR/pages/step-2-transcription.png" "$CASE_DIR/transcription-picked-codex.png"
    expect_text "$CASE_DIR/transcription-picked-codex.png" "GPT Live Transcribe" \
      || errors+=("driving the picker did not update the transcription stats")
    for (( step = 2; step < 6; step++ )); do
      click_button Continue || errors+=("Continue failed on step $step")
      sleep 0.5
    done
    wait_for_page_capture 6 refinement || errors+=("could not reach the refinement step")
  fi
  if (( ${#errors[@]} == 0 )); then
    drive_picker "None" \
      || errors+=("could not drive the refinement picker to None")
    sleep 0.5
    recapture_step 6 refinement || errors+=("the refinement step was not recaptured")
  fi
  if (( ${#errors[@]} == 0 )); then
    cp "$CASE_DIR/pages/step-6-refinement.png" "$CASE_DIR/refinement-picked-none.png"
    expect_no_text "$CASE_DIR/refinement-picked-none.png" "Default model" \
      || errors+=("None still shows a stats block")
  fi
  if (( ${#errors[@]} )); then
    fail_case "$(IFS='; '; echo "${errors[*]}")"
  else
    pass_case "Driving the pickers updates the stats live and None hides them."
  fi
fi

stop_app
log "Provider steps E2E finished"
cat "$VERDICTS"
if grep -q 'FAIL\|BLOCKED' "$VERDICTS"; then
  exit 1
fi
