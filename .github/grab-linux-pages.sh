#!/usr/bin/env bash
# Grabs each page id in $PAGES from build/speecher into pages/linux-<page>.png:
# offscreen, KDE platform theme, Breeze Light in an isolated XDG_CONFIG_HOME,
# as AGENTS.md's "Look before you claim" describes. Run from the source root.
set -euo pipefail
: "${PAGES:?set PAGES to the page ids to grab}"
exe="${1:-build/speecher}"
export QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=kde XDG_CURRENT_DESKTOP=KDE
XDG_CONFIG_HOME="$(mktemp -d)" XDG_DATA_HOME="$(mktemp -d)" logs="$(mktemp -d)"
export XDG_CONFIG_HOME XDG_DATA_HOME
export SPEECHER_INSIGHTS_SEED="$PWD/docs/insights-mockup/seed-active.jsonl"
config="$XDG_CONFIG_HOME/io.github.firemonster612"
mkdir -p "$config" pages
{
  cat /usr/share/color-schemes/BreezeLight.colors
  printf '\n[Icons]\nTheme=breeze\n[KDE]\nwidgetStyle=Breeze\n'
} > "$XDG_CONFIG_HOME/kdeglobals"
# This build as the last one run and no update checks, so no "is installed"
# strip or update banner covers the page.
read -r number build < <("$exe" --version 2>/dev/null |
  sed -n 's/^speecher \(.*\) (build \([0-9]*\))$/\1 \2/p')
printf '[updates]\nautoCheck=false\nlastRunVersion=%s\nlastRunBuildNumber=%s\n' \
  "$number" "$build" > "$config/speecher.conf"
failed=0
for page in $PAGES; do
  if ! SPEECHER_GRAB_PAGE="$page" dbus-run-session -- "$exe" --grab "pages/linux-$page.png" \
    > "$logs/$page.log" 2>&1 || [ ! -s "pages/linux-$page.png" ]; then
    echo "linux-$page failed:"
    tail -20 "$logs/$page.log"
    failed=1
  fi
done
exit $failed
