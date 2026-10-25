#!/usr/bin/env bash
# Checks the version cmake/GitVersion.cmake gives a build of a stable tag in
# the clones a release rebuild can come from, and that a tag off master and an
# untagged commit stay nightlies.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@example.com GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.com
printf 'include("%s/cmake/GitVersion.cmake")\nmessage(NOTICE "${SPEECHER_VERSION}")\n' "$ROOT" > "$WORK/print.cmake"

version_in() {
  (cd "$1" && cmake -DPROJECT_VERSION=0.0.1 -P "$WORK/print.cmake" 2>&1 >/dev/null | tail -n 1)
}

failed=0
expect() {
  local name=$1 want=$2 got
  got="$(version_in "$3")"
  if [[ "$got" == $want ]]; then
    echo "PASS $name: $got"
  else
    echo "FAIL $name: $got, expected $want" >&2
    failed=1
  fi
}

origin="$WORK/origin"
git init -q -b master "$origin"
git -C "$origin" commit -q --allow-empty -m one
git -C "$origin" commit -q --allow-empty -m two
git -C "$origin" tag v1.2.3
git -C "$origin" switch -q -c side
git -C "$origin" commit -q --allow-empty -m side
git -C "$origin" tag v9.9.9
git -C "$origin" switch -q master
git -C "$origin" commit -q --allow-empty -m three

git clone -q "$origin" "$WORK/clone"
git -C "$WORK/clone" checkout -q v1.2.3
expect "tag in a full clone" 1.2.3 "$WORK/clone"

# The developer's master stopped before the tag; origin/master has it.
git -C "$WORK/clone" branch -q -f master v1.2.3~1
expect "tag with a stale local master" 1.2.3 "$WORK/clone"

git init -q "$WORK/tag-only"
git -C "$WORK/tag-only" fetch -q "$origin" refs/tags/v1.2.3:refs/tags/v1.2.3
git -C "$WORK/tag-only" checkout -q v1.2.3
expect "clone of the tag alone" 1.2.3 "$WORK/tag-only"

git -C "$WORK/clone" checkout -q v9.9.9
expect "tag off master" '0.0.1-nightly.*' "$WORK/clone"

git -C "$WORK/clone" checkout -q origin/master
expect "commit after the tag" '1.2.4-nightly.*' "$WORK/clone"

exit "$failed"
