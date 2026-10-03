#!/usr/bin/env bash

# Vocabulary terms with context, Writing Profile limits, Key term and
# Priority, driven through the settings window's Terms view: add Sev1 as a
# priority key term limited to Work and AI coding (Priority disables while Key
# term is off), then edit Kubernetes down to Work. AX finds the controls;
# clicks and typing are real input events. Screenshots are screencapture of the real screen (the
# backing-store grab has no sheets), and the whole walk is filmed as frames
# assembled into an mp4. Scratch-branch-only.
#
# What the runner's AX tree shows: SwiftUI's accessory-bar buttons, the
# sheet's buttons, radio buttons and checkboxes carry no title, so they are
# found by order; the table is an outline. System Events' "click at" presses
# the element under the point rather than clicking (a radio group's press
# selects its first row), so clicks are CGEvents.

set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DOMAIN=com.io-github-firemonster612.speecher
BUNDLE_ID=io.github.firemonster612.speecher
EVIDENCE="${EVIDENCE_ROOT:?}"
APP_BIN="${APP_BUNDLE:?}/Contents/MacOS/speecher"
TOOL="${TOOL:?}"
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
# The controls by order. Accessory bar: Add, Remove, Edit…, Import CSV…,
# Undo delete. Sheet buttons: Cancel, then Add or OK. Sheet radio buttons:
# Every Writing Profile, Only these Writing Profiles:. Sheet checkboxes: Key
# term, Priority, then Work, Email, Personal, AI coding, Other, Standup notes.
BAR_ADD=0
BAR_EDIT=2
SHEET_CONFIRM=1
RADIO_ONLY=1
CHECK_KEY_TERM=0
CHECK_PRIORITY=1
CHECK_WORK=2
CHECK_AI_CODING=5
mkdir -p "$SHOTS" "$FRAMES" "$EVIDENCE/ax"
: >"$LOG"
errors=()
shot_index=0
last_shot=

log() { printf '%s %s\n' "$(date -u +%T)" "$*" | tee -a "$LOG"; }
fail() { log "FAIL: $*"; errors+=("$*"); }

# The AX driver, bounded at 90 s. Prints the command's result.
ax() {
  local out="$EVIDENCE/.ax.out" pid count=0 status
  osascript -l JavaScript "$HERE/vocabulary_ax.js" "$@" >"$out" 2>>"$LOG" &
  pid=$!
  while kill -0 "$pid" 2>/dev/null && (( count < 900 )); do
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
  last_shot="$SHOTS/$(printf '%02d' "$shot_index")-$1.png"
  screencapture -x "$last_shot" || fail "screencapture could not take $1"
  log "shot $last_shot"
}

dump() {
  ax dump "$1" >"$EVIDENCE/ax/$2.txt" 2>&1 || true
}

value_is() { [[ "$(ax value "$1" "$2" '' "$3")" == "$4" ]]; }
priority_enabled_is() { [[ "$(ax enabled sheet AXCheckBox '' "$CHECK_PRIORITY")" == "$1" ]]; }
# badges_are TERM KEY PRIORITY: whether TERM's row shows the Key term and
# Priority pills as KEY and PRIORITY (yes or no) say, Key term over Priority
# in the Speech column after the term's.
badges_are() {
  local expected=''
  [[ "$2" == yes ]] && expected='Key term'
  [[ "$3" == yes ]] && expected="${expected:+$expected,}Priority"
  [[ "$(ax pills "$1")" == "$expected" ]]
}
expect_badges() {
  badges_are "$@" \
    || fail "$1's row does not show Key term=$2 Priority=$3 in the Speech column: $(ax pills "$1")"
}
sheets_are() { [[ "$(ax sheets)" == "$1" ]]; }
edit_enabled() { [[ "$(ax enabled window unnamed '' "$BAR_EDIT")" == true ]]; }

wait_until() {
  local tries="$1"
  shift
  while (( tries-- > 0 )); do
    "$@" && return 0
    sleep 0.5
  done
  return 1
}

# activate SCOPE ROLE INDEX CHECK...: a CGEvent click at the INDEXth ROLE's
# centre, then AXPress, until CHECK holds.
activate() {
  local scope="$1" role="$2" index="$3" xy
  shift 3
  xy="$(ax find "$scope" "$role" '' "$index")" || { log "no $role #$index in $scope"; return 1; }
  # shellcheck disable=SC2086
  "$TOOL" click $xy
  wait_until 3 "$@" && { log "$role #$index: click worked"; return 0; }
  ax press "$scope" "$role" '' "$index" >/dev/null
  wait_until 3 "$@" && { log "$role #$index: AXPress worked"; return 0; }
  log "$role #$index: neither a click nor AXPress took effect"
  return 1
}

# type_into INDEX TEXT: the sheet's INDEXth text input gets TEXT, clicked into
# and typed as a person would; AX focus is the fallback.
type_into() {
  local index="$1" text="$2" xy
  xy="$(ax find sheet input '' "$index")" || { log "no text input #$index"; return 1; }
  # shellcheck disable=SC2086
  "$TOOL" click $xy
  sleep 0.4
  "$TOOL" type "$text"
  wait_until 3 value_is sheet input "$index" "\"$text\"" && return 0
  log "typing after a click did not fill input #$index; trying AX focus"
  ax focus sheet input '' "$index" >/dev/null
  sleep 0.3
  "$TOOL" type "$text"
  wait_until 3 value_is sheet input "$index" "\"$text\""
}

# sheet_title: what Vision reads in the open sheet's heading, from the last shot.
sheet_title() {
  local frame x y w h
  frame="$(ax frameof sheet AXHeading '' 0)" || return 1
  IFS=, read -r x y w h <<<"$frame"
  "$TOOL" ocr "$last_shot" "$((x - 6)),$((y - 6)),$((w + 160)),$((h + 12))"
}

# saved_entry TERM: the stored entry for TERM as JSON. Qt stores the JSON as
# data or a string, so it is read through a plist export.
saved_entry() {
  defaults export "$DOMAIN" - 2>/dev/null | python3 -c '
import json, plistlib, sys
term = sys.argv[1]
stored = plistlib.loads(sys.stdin.buffer.read()).get("stt.vocabularyEntries", "[]")
if isinstance(stored, bytes):
    stored = stored.decode()
for entry in json.loads(stored):
    if entry.get("term") == term:
        print(json.dumps({"context": entry.get("context", ""), "profiles": entry.get("profiles", []),
                          "starred": entry.get("starred", False), "keyTerm": entry.get("keyTerm", True)}))
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
    kTCCServiceAccessibility "$TOOL" 2 UNUSED 1 || return 1
  sudo python3 "$TCC_SEED" "$SYSTEM_TCC_DB" \
    kTCCServicePostEvent "$TOOL" 2 UNUSED 1 || return 1
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
row="$(ax row Kubernetes)"
log "Kubernetes row: $row"
[[ "$row" == *"| $KUBERNETES_CONTEXT |"* && "$row" == *"| Work, AI coding |"* ]] \
  || fail "the Kubernetes row does not show its context line and Work, AI coding"
# Seeded: Kubernetes, Aoife Byrne and Speecher have priority; Grafana is no key term.
expect_badges Kubernetes yes yes
expect_badges 'Aoife Byrne' yes yes
expect_badges Speecher yes yes
expect_badges PR yes no
expect_badges Grafana no no
[[ "$(ax enabled window unnamed '' "$BAR_EDIT")" == false ]] \
  || fail "Edit… is not in the bar, or is enabled with nothing selected"
shot table

# --- 2. Add Sev1 -----------------------------------------------------------

if activate window unnamed "$BAR_ADD" sheets_are 1; then
  sleep 1
  dump sheet add-sheet-open
  type_into 0 Sev1 || fail "could not type the term Sev1"
  type_into 1 "$SEV1_CONTEXT" || fail "could not type Sev1's context"
  value_is sheet AXCheckBox "$CHECK_KEY_TERM" 1 || fail "a new term is not a key term"
  value_is sheet AXCheckBox "$CHECK_PRIORITY" 0 || fail "a new term already has priority"
  activate sheet AXCheckBox "$CHECK_PRIORITY" value_is sheet AXCheckBox "$CHECK_PRIORITY" 1 \
    || fail "could not tick Priority"
  activate sheet AXCheckBox "$CHECK_KEY_TERM" value_is sheet AXCheckBox "$CHECK_KEY_TERM" 0 \
    || fail "could not untick Key term"
  wait_until 3 priority_enabled_is false || fail "Priority stayed enabled with Key term off"
  xy="$(ax find sheet AXCheckBox '' "$CHECK_PRIORITY")"
  # shellcheck disable=SC2086
  [[ -n "$xy" ]] && "$TOOL" click $xy
  sleep 1
  value_is sheet AXCheckBox "$CHECK_PRIORITY" 1 || fail "a click changed the disabled Priority box"
  dump sheet add-sheet-key-term-off
  shot add-sheet-key-term-off
  activate sheet AXCheckBox "$CHECK_KEY_TERM" value_is sheet AXCheckBox "$CHECK_KEY_TERM" 1 \
    || fail "could not tick Key term again"
  wait_until 3 priority_enabled_is true || fail "Priority stayed disabled with Key term on"
  activate sheet AXRadioButton "$RADIO_ONLY" value_is sheet AXRadioButton "$RADIO_ONLY" 1 \
    || fail "could not choose 'Only these Writing Profiles:'"
  activate sheet AXCheckBox "$CHECK_WORK" value_is sheet AXCheckBox "$CHECK_WORK" 1 \
    || fail "could not tick Work"
  activate sheet AXCheckBox "$CHECK_AI_CODING" value_is sheet AXCheckBox "$CHECK_AI_CODING" 1 \
    || fail "could not tick AI coding"
  sleep 0.5
  dump sheet add-sheet-filled
  shot add-sheet
  title="$(sheet_title)"
  log "add sheet title: $title"
  [[ "$title" == *"New term"* ]] || fail "the add sheet is not titled New term: $title"
  activate sheet unnamed "$SHEET_CONFIRM" sheets_are 0 || fail "the sheet's Add did not close it"
  sleep 1
  row="$(ax row Sev1)"
  log "Sev1 row: $row"
  [[ "$row" == *"| $SEV1_CONTEXT |"* && "$row" == *"| Work, AI coding |"* ]] \
    || fail "the table's Sev1 row does not show its context and Work, AI coding: $row"
  expect_badges Sev1 yes yes
  wait_until 20 saved_is Sev1 "{\"context\": \"$SEV1_CONTEXT\", \"profiles\": [\"work\", \"ai_coding\"], \"starred\": true, \"keyTerm\": true}" \
    || fail "Sev1 was not saved with its context and Work, AI coding: $(saved_entry Sev1)"
  shot table-with-sev1
else
  fail "Add did not open the add sheet"
  dump window add-failed
fi

# --- 3. Edit Kubernetes ----------------------------------------------------

xy="$(ax rowof Kubernetes)"
# shellcheck disable=SC2086
[[ -n "$xy" ]] && "$TOOL" click $xy
if ! wait_until 3 edit_enabled; then
  log "clicking the Kubernetes row did not select it; trying AXSelected"
  ax selectrow Kubernetes >/dev/null
fi
if ! wait_until 3 edit_enabled; then
  fail "could not select the Kubernetes row (Edit… stayed disabled)"
elif activate window unnamed "$BAR_EDIT" sheets_are 1; then
  sleep 1
  dump sheet edit-sheet-open
  value_is sheet input 0 '"Kubernetes"' || fail "the edit sheet's term field is not Kubernetes"
  value_is sheet input 1 "\"$KUBERNETES_CONTEXT\"" || fail "the edit sheet does not show Kubernetes' context"
  value_is sheet AXRadioButton "$RADIO_ONLY" 1 || fail "the edit sheet is not on 'Only these Writing Profiles:'"
  value_is sheet AXCheckBox "$CHECK_WORK" 1 || fail "Work is not ticked in the edit sheet"
  value_is sheet AXCheckBox "$CHECK_AI_CODING" 1 || fail "AI coding is not ticked in the edit sheet"
  value_is sheet AXCheckBox "$CHECK_KEY_TERM" 1 || fail "Key term is not ticked in the edit sheet"
  value_is sheet AXCheckBox "$CHECK_PRIORITY" 1 || fail "Priority is not ticked in the edit sheet"
  priority_enabled_is true || fail "Priority is disabled in the edit sheet of a key term"
  shot edit-sheet
  title="$(sheet_title)"
  log "edit sheet title: $title"
  [[ "$title" == *Kubernetes* ]] || fail "the edit sheet is not titled Kubernetes: $title"
  activate sheet AXCheckBox "$CHECK_AI_CODING" value_is sheet AXCheckBox "$CHECK_AI_CODING" 0 \
    || fail "could not untick AI coding"
  sleep 0.5
  shot edit-sheet-unticked
  activate sheet unnamed "$SHEET_CONFIRM" sheets_are 0 || fail "the sheet's OK did not close it"
  sleep 1
  row="$(ax row Kubernetes)"
  log "Kubernetes row: $row"
  [[ "$row" == *"| Work |"* && "$row" != *"AI coding"* ]] \
    || fail "the table's Kubernetes row does not read Work alone: $row"
  wait_until 20 saved_is Kubernetes "{\"context\": \"$KUBERNETES_CONTEXT\", \"profiles\": [\"work\"], \"starred\": true, \"keyTerm\": true}" \
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
