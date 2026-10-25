#!/bin/sh
# Runs a command against a real, unlocked Secret Service: a private D-Bus
# session and a gnome-keyring-daemon whose keyring lives in a throwaway home.
# Usage: dbus-run-session -- with_session_keyring.sh COMMAND...
set -eu
home=$(mktemp -d)
trap 'kill "${keyring_pid:-0}" 2>/dev/null || true; rm -rf "$home"' EXIT
export HOME="$home" XDG_DATA_HOME="$home/.local/share" XDG_RUNTIME_DIR="$home"
# Anything the bus starts for a name, a keyring replacing one that died
# included, gets the throwaway home too, not the caller's.
dbus-send --session --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus \
    org.freedesktop.DBus.UpdateActivationEnvironment \
    dict:string:string:HOME,"$HOME",XDG_DATA_HOME,"$XDG_DATA_HOME",XDG_RUNTIME_DIR,"$XDG_RUNTIME_DIR" >/dev/null
printf 'speecher-tests' | gnome-keyring-daemon --foreground --unlock --components=secrets >/dev/null 2>&1 &
keyring_pid=$!
# The daemon owns org.freedesktop.secrets once it is ready. Any other owner
# would be one the bus started for the name, with the person's own HOME.
owner() {
    dbus-send --session --print-reply --dest=org.freedesktop.DBus / \
        org.freedesktop.DBus.GetConnectionUnixProcessID string:org.freedesktop.secrets 2>/dev/null \
        | awk '/uint32/ { print $2 }'
}
for _ in $(seq 50); do
    [ "$(owner)" = "$keyring_pid" ] && break
    sleep 0.1
done
if [ "$(owner)" != "$keyring_pid" ]; then
    echo "with_session_keyring.sh: the private keyring did not start" >&2
    exit 1
fi
# Only with this does SecretStore let a test reach a keyring at all.
SPEECHER_TEST_PRIVATE_KEYRING=1 "$@"
