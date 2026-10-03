#!/usr/bin/env bash

# Vocabulary terms with context and Writing Profile limits, driven through the
# settings window's Terms view by AX clicks and typing: add Sev1 limited to
# Work and AI coding, then edit Kubernetes down to Work. Screenshots are
# screencapture of the real screen (the backing-store grab has no sheets), and
# the whole walk is filmed as frames assembled into an mp4. Scratch-branch-only.

set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DOMAIN=com.io-github-firemonster612.speecher
BUNDLE_ID=io.github.firemonster612.speecher
EVIDENCE="${EVIDENCE_ROOT:?}"
APP_BIN="${APP_BUNDLE:?}/Contents/MacOS/speecher"
CLICKER="${CLICKER:?}"
TCC_SEED="$HERE/tcc_seed.py"
SEED_JSON="${SEED_JSON:?}"
SHOTS="$EVIDENCE/shots"
FRAMES="$EVIDENCE/frames"
LOG="$EVIDENCE/harness.log"
RECORDING="$EVIDENCE/.recording"
USER_TCC_DB="$HOME/Library/Application Support/com.apple.TCC/TCC.db"
SYSTEM_TCC_DB='/Library/Application Support/com.apple.TCC/TCC.db'
KUBERNETES_CONTEXT='The container platform, when I talk about clusters, pods or deploys.'
SEV1_CONTEXT='Incident severity level, in on-call and incident chats.'
mkdir -p "$SHOTS" "$FRAMES" "$EVIDENCE/ax"
: >"$LOG"
errors=()
shot_index=0

log() { printf '%s %s\n' "$(date -u +%T)" "$*" | tee -a "$LOG"; }
fail() { log "FAIL: $*"; errors+=("$*"); }

# The AX driver, bounded at 60 s. Prints the command's result.
ax() {
  local out="$EVIDENCE/.ax.out" pid count=0 status
  osascript -l JavaScript "$HERE/vocabulary_ax.js" "$@" >"$out" 2>>"$LOG" &
  pid=$!
  while kill -0 "$pid" 2>/dev/null && (( count < 600 )); do
    sleep 0.1
    count=$((count + 1))
  done
  if kill -0 "$pid" 2>/dev/null; then
    kill -9 "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    log "ax $* timed out"
    return 124
  fi
  wait "$pid"
  status=$?
  printf 'ax %s -> %s: %s\n' "$*" "$status" "$(head -c 300 "$out")" >>"$LOG"
  cat "$out"
  return "$status"
}

shot() {
  shot_index=$((shot_index + 1))
  local file
  file="$SHOTS/$(printf '%02d' "$shot_index")-$1.png"
  screencapture -x "$file" || fail "screencapture could not take $1"
  log "shot $file"
}

dump() {
  ax dump "$1" >"$EVIDENCE/ax/$2.txt" 2>&1 || true
}

value_is() { [[ "$(ax value "$1" "$2" "$3" "${5:-0}")" == "$4" ]]; }
sheets_are() { [[ "$(ax sheets)" == "$1" ]]; }
edit_enabled() { [[ "$(ax enabled window AXButton 'Edit*')" == true ]]; }

wait_until() {
  local tries="$1"
  shift
  while (( tries-- > 0 )); do
    "$@" && return 0
    sleep 0.5
  done
  return 1
}

# activate SCOPE ROLE LABEL INDEX CHECK...: a System Events click at the
# element's centre, then AXPress, then a CGEvent click, until CHECK holds.
activate() {
  local scope="$1" role="$2" label="$3" index="$4" xy
  shift 4
  xy="$(ax find "$scope" "$role" "$label" "$index")" || { log "no $role '$label' in $scope"; return 1; }
  # shellcheck disable=SC2086
  ax click $xy >/dev/null
  wait_until 3 "$@" && { log "'$label': System Events click worked"; return 0; }
  ax press "$scope" "$role" "$label" "$index" >/dev/null
  wait_until 3 "$@" && { log "'$label': AXPress worked"; return 0; }
  # shellcheck disable=SC2086
  "$CLICKER" click $xy
  wait_until 3 "$@" && { log "'$label': CGEvent click worked"; return 0; }
  log "'$label': no click took effect"
  return 1
}

# type_into SCOPE INDEX TEXT: the sheet's INDEXth text input gets TEXT.
type_into() {
  local scope="$1" index="$2" text="$3" xy
  xy="$(ax find "$scope" input '' "$index")" || { log "no text input #$index"; return 1; }
  # shellcheck disable=SC2086
  ax click $xy >/dev/null
  sleep 0.4
  ax type "$text" >/dev/null
  wait_until 3 value_is "$scope" input '' "\"$text\"" "$index" && return 0
  log "System Events typing did not land in input #$index; trying AX focus and CGEvent typing"
  ax focus "$scope" input '' "$index" >/dev/null
  ax press "$scope" input '' "$index" >/dev/null 2>&1 || true
  sleep 0.3
  "$CLICKER" type "$text"
  wait_until 3 value_is "$scope" input '' "\"$text\"" "$index"
}

# saved_entry TERM: the stored entry for TERM as JSON, once the settings have it.
# Qt stores the JSON as data or a string, so read it through a plist export.
saved_entry() {
  defaults export "$DOMAIN" - 2>/dev/null | python3 -c '
import json, plistlib, sys
term = sys.argv[1]
stored = plistlib.loads(sys.stdin.buffer.read()).get("stt.vocabularyEntries", "[]")
if isinstance(stored, bytes):
    stored = stored.decode()
for entry in json.loads(stored):
    if entry.get("term") == term:
        print(json.dumps({"context": entry.get("context", ""), "profiles": entry.get("profiles", [])}))
' "$1"
}

saved_is() { [[ "$(saved_entry "$1")" == "$2" ]]; }

seed_tcc() {
  python3 "$TCC_SEED" "$USER_TCC_DB" \
    kTCCServiceAppleEvents /usr/bin/osascript 2 com.apple.systemevents 1 || return 1
  python3 "$TCC_SEED" "$USER_TCC_DB" \
    kTCCServiceAppleEvents /usr/bin/osascript 2 "$BUNDLE_ID" 1 || return 1
  sudo python3 "$TCC_SEED" "$SYSTEM_TCC_DB" \
    kTCCServiceAccessibility /usr/bin/osascript 2 UNUSED 1 || return 1
  sudo python3 "$TCC_SEED" "$SYSTEM_TCC_DB" \
    kTCCServiceScreenCapture /usr/sbin/screencapture 2 UNUSED 1 || return 1
  sudo python3 "$TCC_SEED" "$SYSTEM_TCC_DB" \
    kTCCServiceAccessibility "$CLICKER" 2 UNUSED 1 || return 1
  sudo python3 "$TCC_SEED" "$SYSTEM_TCC_DB" \
    kTCCServicePostEvent "$CLICKER" 2 UNUSED 1 || return 1
  sudo launchctl kickstart -k system/com.apple.tccd || sudo killall tccd || true
}

seed_settings() {
  defaults delete "$DOMAIN" >/dev/null 2>&1 || true
  defaults write "$DOMAIN" app.setupCompleted -bool true
  defaults write "$DOMAIN" app.launchAtLogin -bool false
  defaults write "$DOMAIN" ui.soundsEnabled -bool false
  defaults write "$DOMAIN" updates.autoCheck -bool false
  defaults write "$DOMAIN" updates.autoInstall -bool false
  # This build as the last one run, so no "is installed" strip covers the page.
  local version number build
  version="$(DYLD_FRAMEWORK_PATH="${QT_ROOT_DIR:-}/lib" "$APP_BIN" --version | head -1)"
  number="$(printf '%s\n' "$version" | sed -n 's/^speecher \(.*\) (build \([0-9]*\))$/\1/p')"
  build="$(printf '%s\n' "$version" | sed -n 's/^speecher \(.*\) (build \([0-9]*\))$/\2/p')"
  defaults write "$DOMAIN" updates.lastRunVersion -string "$number"
  defaults write "$DOMAIN" updates.lastRunBuildNumber -int "${build:-0}"
  defaults write "$DOMAIN" stt.vocabularyEntries -string "$(cat "$SEED_JSON")"
  defaults write "$DOMAIN" refinement.writingProfiles -string \
    '[{"profile":"custom_standup_notes","cleanupStrength":"balanced","tone":"","instructions":"","name":"Standup notes"}]'
  # The settings window reopens on the pane last shown (AppModel.reopenPane).
  defaults write "$BUNDLE_ID" lastSettingsPane -string vocabulary:terms
  defaults write "$BUNDLE_ID" SUEnableAutomaticChecks -bool false
}

record() {
  : >"$RECORDING"
  local n=0
  while [[ -f "$RECORDING" ]]; do
    screencapture -x -C "$FRAMES/frame-$(printf '%06d' "$n").png" 2>/dev/null && n=$((n + 1))
    sleep 0.1
  done
}

# --- Setup -----------------------------------------------------------------

seed_tcc >"$EVIDENCE/tcc-seeding.log" 2>&1 || fail "the runner refused the TCC seeds"
seed_settings
pkill -9 -x speecher 2>/dev/null || true
DYLD_FRAMEWORK_PATH="${QT_ROOT_DIR:-}/lib" "$APP_BIN" >"$EVIDENCE/process.out" 2>&1 &
APP_PID=$!
wait_until 60 ax frame >/dev/null || fail "the settings window never appeared"
ax front >/dev/null
sleep 2
log "window frame $(ax frame)"
record &
RECORDER=$!
started=$SECONDS

# --- 1. The table ----------------------------------------------------------

dump window window-start
[[ "$(ax has window Kubernetes)" == yes ]] || fail "the Terms table does not show Kubernetes"
[[ "$(ax hastext window 'Work, AI coding')" == yes ]] \
  || fail "the Profiles column does not read 'Work, AI coding' for Kubernetes"
[[ "$(ax hastext window "$KUBERNETES_CONTEXT")" == yes ]] \
  || fail "Kubernetes' context line is not in the table"
ax find window AXButton 'Edit*' >/dev/null || fail "the accessory bar has no Edit… button"
shot table

# --- 2. Add Sev1 -----------------------------------------------------------

if activate window AXButton Add 0 sheets_are 1; then
  sleep 1
  dump sheet add-sheet-open
  [[ "$(ax hastext sheet 'New term')" == yes ]] || fail "the add sheet is not titled New term"
  type_into sheet 0 Sev1 || fail "could not type the term Sev1"
  type_into sheet 1 "$SEV1_CONTEXT" || fail "could not type Sev1's context"
  activate sheet AXRadioButton 'Only these Writing Profiles:' 0 \
    value_is sheet AXRadioButton 'Only these Writing Profiles:' 1 \
    || fail "could not choose 'Only these Writing Profiles:'"
  activate sheet AXCheckBox Work 0 value_is sheet AXCheckBox Work 1 || fail "could not tick Work"
  activate sheet AXCheckBox 'AI coding' 0 value_is sheet AXCheckBox 'AI coding' 1 \
    || fail "could not tick AI coding"
  sleep 0.5
  dump sheet add-sheet-filled
  shot add-sheet
  activate sheet AXButton Add 0 sheets_are 0 || fail "the sheet's Add did not close it"
  sleep 1
  [[ "$(ax has window Sev1)" == yes ]] || fail "Sev1 is not in the table after Add"
  wait_until 20 saved_is Sev1 "{\"context\": \"$SEV1_CONTEXT\", \"profiles\": [\"work\", \"ai_coding\"]}" \
    || fail "Sev1 was not saved with its context and Work, AI coding: $(saved_entry Sev1)"
  shot table-with-sev1
else
  fail "Add did not open the add sheet"
  dump window add-failed
fi

# --- 3. Edit Kubernetes ----------------------------------------------------

xy="$(ax rowof Kubernetes)"
# shellcheck disable=SC2086
[[ -n "$xy" ]] && ax click $xy >/dev/null
if ! wait_until 3 edit_enabled; then
  log "clicking the Kubernetes row did not select it; trying AXSelected, then a CGEvent click"
  ax selectrow Kubernetes >/dev/null
  # shellcheck disable=SC2086
  wait_until 3 edit_enabled || { [[ -n "$xy" ]] && "$CLICKER" click $xy; }
fi
if ! wait_until 3 edit_enabled; then
  fail "could not select the Kubernetes row (Edit… stayed disabled)"
elif activate window AXButton 'Edit*' 0 sheets_are 1; then
  sleep 1
  dump sheet edit-sheet-open
  [[ "$(ax hastext sheet Kubernetes)" == yes ]] || fail "the edit sheet is not titled Kubernetes"
  value_is sheet input '' '"Kubernetes"' 0 || fail "the edit sheet's term field is not Kubernetes"
  value_is sheet input '' "\"$KUBERNETES_CONTEXT\"" 1 || fail "the edit sheet does not show Kubernetes' context"
  value_is sheet AXRadioButton 'Only these Writing Profiles:' 1 \
    || fail "the edit sheet is not on 'Only these Writing Profiles:'"
  value_is sheet AXCheckBox Work 1 || fail "Work is not ticked in the edit sheet"
  value_is sheet AXCheckBox 'AI coding' 1 || fail "AI coding is not ticked in the edit sheet"
  shot edit-sheet
  activate sheet AXCheckBox 'AI coding' 0 value_is sheet AXCheckBox 'AI coding' 0 \
    || fail "could not untick AI coding"
  sleep 0.5
  shot edit-sheet-unticked
  activate sheet AXButton OK 0 sheets_are 0 || fail "the sheet's OK did not close it"
  sleep 1
  [[ "$(ax hastext window Work)" == yes ]] || fail "no row's Profiles reads Work after the edit"
  wait_until 20 saved_is Kubernetes "{\"context\": \"$KUBERNETES_CONTEXT\", \"profiles\": [\"work\"]}" \
    || fail "Kubernetes was not saved limited to Work: $(saved_entry Kubernetes)"
  shot table-after-edit
else
  fail "Edit… did not open the edit sheet"
fi
dump window window-end

# --- Film ------------------------------------------------------------------

rm -f "$RECORDING"
wait "$RECORDER" 2>/dev/null
elapsed=$((SECONDS - started))
frames=$(find "$FRAMES" -name 'frame-*.png' | wc -l | tr -d ' ')
rate=$(( frames / (elapsed > 0 ? elapsed : 1) ))
(( rate < 1 )) && rate=1
log "frames=$frames elapsed=${elapsed}s rate=$rate"
(( frames >= 20 )) || fail "only $frames frames were recorded"
ffmpeg -y -framerate "$rate" -i "$FRAMES/frame-%06d.png" \
  -vf 'scale=trunc(iw/2)*2:trunc(ih/2)*2' -pix_fmt yuv420p \
  "$EVIDENCE/vocabulary-flow.mp4" >"$EVIDENCE/ffmpeg.out" 2>&1 \
  || fail "ffmpeg could not assemble the video"

kill -9 "$APP_PID" 2>/dev/null || true
defaults export "$DOMAIN" "$EVIDENCE/saved-settings.plist" 2>&1 || true
if (( ${#errors[@]} )); then
  printf '%s\n' "${errors[@]}" >"$EVIDENCE/failures.txt"
  log "FAILED: ${#errors[@]} check(s)"
  exit 1
fi
log "PASS: vocabulary add and edit on film"
