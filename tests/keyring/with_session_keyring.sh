#!/bin/sh
# Runs a command against a real, unlocked Secret Service: a private D-Bus
# session and a gnome-keyring-daemon whose keyring lives in a throwaway home.
# Usage: dbus-run-session -- with_session_keyring.sh COMMAND...
set -eu
home=$(mktemp -d)
trap 'kill "${keyring_pid:-0}" 2>/dev/null || true; rm -rf "$home"' EXIT
export HOME="$home" XDG_DATA_HOME="$home/.local/share" XDG_RUNTIME_DIR="$home"
printf 'speecher-tests' | gnome-keyring-daemon --foreground --unlock --components=secrets >/dev/null 2>&1 &
keyring_pid=$!
# The daemon owns org.freedesktop.secrets once it is ready.
for _ in $(seq 50); do
    dbus-send --session --print-reply --dest=org.freedesktop.DBus / \
        org.freedesktop.DBus.NameHasOwner string:org.freedesktop.secrets 2>/dev/null \
        | grep -q 'boolean true' && break
    sleep 0.1
done
SPEECHER_TEST_EXPECT_KEYRING=1 "$@"
