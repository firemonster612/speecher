#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: $0 <stable|nightly> <build-number> <base-name>" >&2
  exit 1
fi

channel="$1"
build_number="$2"
base_name="$3"
case "$channel" in
  stable) printf '%s\n' "$base_name" ;;
  nightly)
    if [[ ! "$build_number" =~ ^[0-9]+$ ]]; then
      echo "Invalid nightly build number: $build_number" >&2
      exit 1
    fi
    printf '%s-build%s.%s\n' "${base_name%.*}" "$build_number" "${base_name##*.}"
    ;;
  *) echo "Unknown release channel: $channel" >&2; exit 1 ;;
esac
