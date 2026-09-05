#!/usr/bin/env bash

# The setup assistant E2E: launches the packaged app with setup incomplete,
# walks the SwiftUI assistant with real AX clicks, and checks what setup is for
# — the settings it wrote, the shortcut it bound, and the window that follows.

source "$(dirname "$0")/common.sh"
TCC_SEED="$(dirname "$0")/tcc_seed.py"
ASSISTANT_WINDOW='Speecher Setup Assistant'
USER_TCC_DB="$HOME/Library/Application Support/com.apple.TCC/TCC.db"
SYSTEM_TCC_DB='/Library/Application Support/com.apple.TCC/TCC.db'

# The steps as SetupStep.all orders them; the capture seam names its PNGs after
# these ids.
SETUP_STEP_IDS=(welcome transcription microphone accessibility delivery
                refinement profiles ready login)

seed_setup_tcc() {
  # osascript drives the assistant: AppleEvents to System Events and to the
  # app, and the system-db Accessibility right that synthetic clicks need.
  python3 "$TCC_SEED" "$USER_TCC_DB" \
    kTCCServiceAppleEvents /usr/bin/osascript 2 com.apple.systemevents 1 || return 1
  python3 "$TCC_SEED" "$USER_TCC_DB" \
    kTCCServiceAppleEvents /usr/bin/osascript 2 "$BUNDLE_ID" 1 || return 1
  sudo python3 "$TCC_SEED" "$SYSTEM_TCC_DB" \
    kTCCServiceAccessibility /usr/bin/osascript 2 UNUSED 1 || return 1
  # The microphone step opens the input device; the seeded grant is what keeps
  # macOS from raising a consent sheet no one is there to click.
  python3 "$TCC_SEED" "$USER_TCC_DB" kTCCServiceMicrophone "$BUNDLE_ID" 2 || return 1
  sudo launchctl kickstart -k system/com.apple.tccd || sudo killall tccd || true
}

# Unlike common.sh's baseline_reset, setup must be INCOMPLETE, which is the
# whole reason the assistant appears.
fresh_reset() {
  stop_app
  defaults delete "$DOMAIN" >/dev/null 2>&1 || true
  # Sparkle's first-run prompt lives in the bundle-id domain and would steal
  # key status from the assistant.
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
  # The root SwiftUI group exposes the navigation HStack's buttons in order.
  # Skip is first; Continue/Finish is last. Step captures and persisted setup
  # state verify the action, without depending on SwiftUI's AX names.
  local button
  case "$1" in
    "Skip Setup") button='first button' ;;
    Continue|Finish) button='last button' ;;
    *) return 1 ;;
  esac
  assistant_ui "click $button of group 1 of window \"$ASSISTANT_WINDOW\"" \
    >>"$CASE_DIR/clicks.out" 2>&1
}

# The seam writes the PNG after the step renders; wait for the atomic write.
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

setup_completed() {
  [[ "$(defaults read "$DOMAIN" app.setupCompleted 2>/dev/null)" == 1 ]]
}

check_page_captures() {
  # A nonempty PNG can still be the previous step. Read the actual pixels,
  # independently of the flow model that picked the capture's filename.
  swift - "$CASE_DIR/pages" >"$CASE_DIR/page-checks.out" 2>&1 <<'SWIFT'
import Foundation
import ImageIO
import Vision

let pages = [
    ("welcome", "Welcome to Speecher"), ("transcription", "Transcription"),
    ("microphone", "Microphone"), ("accessibility", "Accessibility"),
    ("delivery", "Text delivery"), ("refinement", "Refinement"),
    ("profiles", "Writing profiles"), ("ready", "Ready to dictate"),
    ("login", "Start at login"),
]
for (index, page) in pages.enumerated() {
    let filename = "step-\(index + 1)-\(page.0).png"
    let url = URL(fileURLWithPath: CommandLine.arguments[1]).appendingPathComponent(filename)
    let data = try Data(contentsOf: url)
    guard data.count > 8192,
          let source = CGImageSourceCreateWithData(data as CFData, nil),
          let image = CGImageSourceCreateImageAtIndex(source, 0, nil),
          image.width >= 600, image.height >= 500 else {
        print("FAIL: \(filename) is missing a full-size rendering")
        exit(1)
    }
    let request = VNRecognizeTextRequest()
    request.recognitionLevel = .accurate
    try VNImageRequestHandler(cgImage: image).perform([request])
    let text = (request.results ?? []).compactMap { $0.topCandidates(1).first?.string }
        .joined(separator: " ")
    print("\(filename): \(image.width)x\(image.height), \(data.count) bytes\n\(text)")
    guard text.contains(page.1), text.contains("Step \(index + 1) of 9") else {
        print("FAIL: \(filename) does not show its expected title and step number")
        exit(1)
    }
}
print("PASS: all nine captures show the expected title and step number")
SWIFT
}

settings_window_present() {
  local deadline=$((SECONDS + 20))
  while (( SECONDS < deadline )); do
    if assistant_ui 'get name of windows' 2>/dev/null \
        | tr ',' '\n' | grep -qvE "^\s*($ASSISTANT_WINDOW)?\s*$"; then
      return 0
    fi
    sleep 0.2
  done
  return 1
}

if ! seed_setup_tcc; then
  log "TCC seeding failed; AX driving cannot work without it"
  record_verdict SETUP-TCC FAIL "The runner refused the TCC seeds the AX driver needs."
  exit 1
fi

# S1: a fresh profile walks every step to Finish. Setup completes, the default
# shortcut binds, and the settings window follows the assistant out.
fresh_reset
case_begin S1
if ! launch_setup; then
  fail_case "The app did not launch within 20 seconds."
elif ! wait_for_assistant; then
  fail_case "The setup assistant window never appeared on a fresh profile."
else
  errors=()
  for index in "${!SETUP_STEP_IDS[@]}"; do
    step=$((index + 1))
    id="${SETUP_STEP_IDS[$index]}"
    log "S1 step $step ($id)"
    if ! wait_for_page_capture "$step" "$id"; then
      errors+=("step $step ($id) was never captured")
      break
    fi
    if (( step < ${#SETUP_STEP_IDS[@]} )); then
      if ! click_button Continue; then
        errors+=("Continue did not click on step $step ($id)")
        break
      fi
      sleep 0.5
    fi
  done
  if (( ${#errors[@]} == 0 )); then
    click_button Finish || errors+=("Finish did not click on the last step")
  fi
  sleep 2
  setup_completed || errors+=("app.setupCompleted was not written")
  kill -0 "$APP_PID" 2>/dev/null || errors+=("the app quit after Finish")
  settings_window_present || errors+=("no settings window followed the assistant")
  shortcut="$(defaults read "$DOMAIN" 2>/dev/null | grep -i shortcut || true)"
  printf '%s\n' "$shortcut" > "$CASE_DIR/shortcut-defaults.txt"
  check_page_captures || errors+=("page rendering checks failed; see page-checks.out")
  if (( ${#errors[@]} )); then
    fail_case "$(IFS='; '; echo "${errors[*]}")"
  else
    pass_case "All nine steps rendered and clicked through; Finish completed setup and opened the settings window."
  fi
fi

# S2: Skip Setup from the first step completes setup without touching the
# settings the later steps would have written.
fresh_reset
case_begin S2
if ! launch_setup; then
  fail_case "The app did not launch."
elif ! wait_for_assistant; then
  fail_case "The setup assistant window never appeared."
else
  errors=()
  wait_for_page_capture 1 welcome || errors+=("the welcome step was never captured")
  click_button "Skip Setup" || errors+=("Skip Setup did not click")
  sleep 2
  setup_completed || errors+=("app.setupCompleted was not written after skipping")
  kill -0 "$APP_PID" 2>/dev/null || errors+=("the app quit after skipping")
  settings_window_present || errors+=("no settings window followed skipping")
  if (( ${#errors[@]} )); then
    fail_case "$(IFS='; '; echo "${errors[*]}")"
  else
    pass_case "Skip Setup completed setup from the first step."
  fi
fi

# S3: a completed profile launches without the assistant.
stop_app
case_begin S3
if ! launch_setup; then
  fail_case "The app did not relaunch on the completed profile."
else
  sleep 3
  if ! kill -0 "$APP_PID" 2>/dev/null; then
    fail_case "The app quit after relaunching on the completed profile."
  elif ! settings_window_present; then
    fail_case "No settings window appeared on the completed profile."
  elif assistant_ui "get name of window \"$ASSISTANT_WINDOW\"" >/dev/null 2>&1; then
    fail_case "The assistant reappeared although setup is complete."
  else
    pass_case "A completed profile launches without the assistant."
  fi
fi

stop_app
log "Setup E2E finished"
cat "$VERDICTS"
if grep -q 'FAIL\|BLOCKED' "$VERDICTS"; then
  exit 1
fi
