#!/usr/bin/env python3
"""AT-SPI driver for the Linux update E2E flows.

Runs inside the virtual KDE Wayland session next to the app. Flow 1 exercises
the dictation-popup path: update chip during a dictation, one-click install
and restart, dictation restored after the restart, then the what's-new chip
with its auto-hide and its click-through to the settings What's New page.
Flow 2 exercises the settings-window banner: one-click install and restart,
settings window restored, installed banner afterwards.

Evidence: every assertion prints an `assert name=... ok` line, popup states
are copied from the popup capture directory, and settings-window states are
saved through the app's `grab` IPC command. Exits non-zero on the first
failed assertion.
"""

import glob
import os
import shutil
import subprocess
import sys
import time

import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi

APP = os.environ["E2E_APP"]
OUT = os.environ["E2E_FLOW_DIR"]
POPUP_FRAMES = os.environ["SPEECHER_POPUP_CAPTURE_DIR"]
NEW_VERSION = os.environ["E2E_NEW_VERSION"]
APP_ENV = dict(
    os.environ,
    APPIMAGE_EXTRACT_AND_RUN="1",
)


def log(message: str) -> None:
    print(message, flush=True)


def fail(message: str) -> None:
    log(f"assert name={message!r} ok=FALSE")
    log("E2E-RESULT: FAIL")
    sys.exit(1)


def ok(message: str) -> None:
    log(f"assert name={message!r} ok=true")


def app_command(*arguments: str) -> str:
    result = subprocess.run(
        [APP, *arguments], env=APP_ENV, capture_output=True, text=True, timeout=30
    )
    log(f"ipc {arguments} rc={result.returncode} out={result.stdout.strip()!r}")
    if result.returncode != 0:
        fail(f"ipc command {arguments} failed: {result.stderr.strip()}")
    return result.stdout.strip()


def descendants(node):
    yield node
    try:
        count = node.get_child_count()
    except Exception:
        return
    for index in range(count):
        try:
            child = node.get_child_at_index(index)
        except Exception:
            continue
        if child is not None:
            yield from descendants(child)


def showing(node) -> bool:
    # Hidden widgets stay in the AT-SPI tree with their names, so presence
    # alone would see a banner that auto-hid.
    try:
        return node.get_state_set().contains(Atspi.StateType.SHOWING)
    except Exception:
        return True


def find_containing(text: str, timeout: float, role: str | None = None):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        desktop = Atspi.get_desktop(0)
        for node in descendants(desktop):
            try:
                name = node.get_name() or ""
                if (
                    text in name
                    and (role is None or node.get_role_name() == role)
                    and showing(node)
                ):
                    return node
            except Exception:
                pass
        time.sleep(0.2)
    return None


def require(text: str, timeout: float, role: str | None = None):
    node = find_containing(text, timeout, role)
    if node is None:
        dump_accessibles()
        fail(f"accessible containing {text!r} not found within {timeout}s")
    return node


def require_absent(text: str, timeout: float) -> None:
    node = find_containing(text, timeout)
    if node is not None:
        fail(f"accessible containing {text!r} still present after {timeout}s")


def dump_accessibles() -> None:
    visible = []
    for node in descendants(Atspi.get_desktop(0)):
        try:
            name = node.get_name()
            if name:
                visible.append((node.get_role_name(), name))
        except Exception:
            pass
    log(f"accessible_nodes={visible[:120]}")


def click(node) -> None:
    action = node.get_action_iface()
    names = [action.get_action_name(i) for i in range(action.get_n_actions())]
    log(f"click name={node.get_name()!r} role={node.get_role_name()!r} actions={names}")
    if not action.do_action(0):
        fail(f"AT-SPI action failed for {node.get_name()!r}")


def newest_popup_frame() -> str | None:
    frames = glob.glob(os.path.join(POPUP_FRAMES, "frame-*.png"))
    return max(frames, key=os.path.getmtime) if frames else None


def save_popup(stage: str) -> None:
    time.sleep(0.4)  # let the capture timer take a frame of the current state
    frame = newest_popup_frame()
    if frame:
        shutil.copy(frame, os.path.join(OUT, f"popup-{stage}.png"))
        log(f"screenshot popup-{stage}.png from {os.path.basename(frame)}")


def save_window(stage: str) -> None:
    before = set(glob.glob(os.path.join(OUT, "grabs", "window-*.png")))
    app_command("grab")
    time.sleep(0.3)
    added = set(glob.glob(os.path.join(OUT, "grabs", "window-*.png"))) - before
    if added:
        shutil.copy(added.pop(), os.path.join(OUT, f"window-{stage}.png"))
        log(f"screenshot window-{stage}.png")


def wait_for_new_process(old_pid: int, marker: str, timeout: float) -> int:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        node = find_containing(marker, 0.3)
        if node is not None:
            try:
                pid = node.get_process_id()
            except Exception:
                pid = -1
            if pid > 0 and pid != old_pid:
                return pid
        time.sleep(0.2)
    dump_accessibles()
    fail(f"no relaunched process showing {marker!r} within {timeout}s")
    return -1


def flow_popup() -> None:
    # The daemon was launched by inner.sh; startup auto-check runs against the
    # local manifest (updates/lastCheckTime seeded to 0).
    app_command("start")
    chip = require("Install and restart", 45)
    old_pid = chip.get_process_id()
    ok("update chip visible on the popup during a dictation")
    save_popup("update-chip")

    click(chip)
    require("Restarting after this dictation", 60)
    ok("one click downloads, installs, and parks the restart until idle")
    save_popup("restart-pending")

    app_command("stop")
    new_pid = wait_for_new_process(old_pid, "See what's new", 60)
    executable = os.readlink(f"/proc/{new_pid}/exe")
    version = subprocess.run(
        [executable, "--version"], capture_output=True, text=True, env=APP_ENV
    ).stdout.strip()
    log(f"restarted new_pid={new_pid} exe={executable} version={version!r}")
    if NEW_VERSION not in version:
        fail(f"restarted process reports {version!r}, expected {NEW_VERSION!r}")
    ok("restart swapped in the new build")

    # The relaunched process restored the dictation (popup visible again) and
    # offers what's new above it.
    require("See what's new", 5)
    ok("dictation restored after restart, what's-new chip offered")
    save_popup("whats-new-chip")

    time.sleep(8)
    if find_containing("See what's new", 0.5) is not None:
        fail("what's-new chip did not auto-hide within 8s")
    ok("what's-new chip auto-hides after a few seconds")

    # Auto-hiding the popup chip does not consume the offer: the settings
    # banner still carries it, and it opens the What's New page.
    app_command("stop")
    app_command("settings")
    whats_new = require("See what's new", 20)
    ok("the offer survives auto-hide; the settings banner still shows it")
    click(whats_new)
    require("What's New", 20)
    ok("see what's new opens the What's New page")
    save_window("whats-new-page")
    app_command("quit")


def flow_settings() -> None:
    app_command("settings")
    banner = require("is available", 45)
    install = require("Install and restart", 10)
    old_pid = install.get_process_id()
    ok("settings banner offers the update")
    save_window("banner-available")

    click(install)
    new_pid = wait_for_new_process(old_pid, "See what's new", 90)
    log(f"restarted settings flow new_pid={new_pid}")
    ok("settings one-click install restarted into the new build")

    # The relaunched process reopened the settings window (restore) and shows
    # the installed banner.
    require("is installed", 20)
    ok("settings window restored with the installed banner")
    save_window("banner-installed")
    app_command("quit")


def main() -> None:
    Atspi.init()
    os.makedirs(os.path.join(OUT, "grabs"), exist_ok=True)
    flow = os.environ["E2E_FLOW"]
    if flow == "popup":
        flow_popup()
    elif flow == "settings":
        flow_settings()
    else:
        fail(f"unknown flow {flow!r}")
    log("E2E-RESULT: PASS")


if __name__ == "__main__":
    main()
