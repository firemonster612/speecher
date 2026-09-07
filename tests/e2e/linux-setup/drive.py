#!/usr/bin/env python3
"""AT-SPI driver for the Linux first-run setup E2E flow.

Walks the setup assistant page by page: checks the provider Score lines on the
Transcription and Refinement pages, checks that the Global Shortcut page holds
Next until Install Speecher is clicked, clicks it, checks the AppImage moved
into ~/Applications with the command link, app menu entry and icon in place,
and finishes the assistant.

Evidence: every assertion prints an `assert name=... ok` line, and the app's
`grab` IPC command saves the Global Shortcut page before and after the
install. Exits non-zero on the first failed assertion.
"""

import configparser
import glob
import os
import shutil
import subprocess
import sys
import time

import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi

OUT = os.environ["E2E_FLOW_DIR"]
FAKE_HOME = os.environ["E2E_FAKE_HOME"]
APP_ENV = dict(os.environ, APPIMAGE_EXTRACT_AND_RUN="1")


def app_path() -> str:
    # Installing moves the image out of Downloads mid-flow; the CLI keeps
    # following it.
    installed = os.path.join(FAKE_HOME, "Applications", "Speecher.AppImage")
    return installed if os.path.isfile(installed) else os.environ["E2E_APP"]


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
        [app_path(), *arguments], env=APP_ENV, capture_output=True, text=True, timeout=30
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


def state(node, state_type) -> bool:
    try:
        return node.get_state_set().contains(state_type)
    except Exception:
        return False


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
                    and state(node, Atspi.StateType.SHOWING)
                ):
                    return node
            except Exception:
                pass
        time.sleep(0.2)
    return None


def dump_accessibles() -> None:
    visible = []
    for node in descendants(Atspi.get_desktop(0)):
        try:
            name = node.get_name()
            if name:
                visible.append((node.get_role_name(), name))
        except Exception:
            pass
    log(f"accessible_nodes={visible[:150]}")


def require(text: str, timeout: float, role: str | None = None):
    node = find_containing(text, timeout, role)
    if node is None:
        dump_accessibles()
        fail(f"accessible containing {text!r} not found within {timeout}s")
    return node


def click(node) -> None:
    action = node.get_action_iface()
    names = [action.get_action_name(i) for i in range(action.get_n_actions())]
    log(f"click name={node.get_name()!r} role={node.get_role_name()!r} actions={names}")
    if not action.do_action(0):
        fail(f"AT-SPI action failed for {node.get_name()!r}")


def next_button():
    return require("Next >", 10, role="button")


def wait_next_enabled(timeout: float = 10) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if state(next_button(), Atspi.StateType.ENABLED):
            return True
        time.sleep(0.2)
    return False


def go_next(expected_title: str) -> None:
    # Every gated page must open its gate before Next works; waiting for the
    # button is itself the assertion that the step completed.
    if not wait_next_enabled():
        fail(f"Next never became enabled on the way to {expected_title!r}")
    click(next_button())
    require(expected_title, 15)
    log(f"page {expected_title!r} reached")


def save_grab(stage: str) -> None:
    # Best-effort screenshot: the daemon renders a fresh assistant advanced to
    # the Global Shortcut page (SPEECHER_GRAB_PAGE) and saves it.
    try:
        before = set(glob.glob(os.path.join(OUT, "grabs", "window-*.png")))
        subprocess.run([app_path(), "grab"], env=APP_ENV, capture_output=True, timeout=30)
        time.sleep(1.0)
        added = set(glob.glob(os.path.join(OUT, "grabs", "window-*.png"))) - before
        if added:
            shutil.copy(added.pop(), os.path.join(OUT, f"setup-{stage}.png"))
            log(f"screenshot setup-{stage}.png")
    except Exception as error:
        log(f"grab {stage} skipped: {error}")


def main() -> None:
    source_image = os.path.join(FAKE_HOME, "Downloads", "Speecher.AppImage")
    installed_image = os.path.join(FAKE_HOME, "Applications", "Speecher.AppImage")
    command_link = os.path.join(FAKE_HOME, ".local", "bin", "speecher")
    desktop_file = os.path.join(
        FAKE_HOME, ".local", "share", "applications",
        "io.github.firemonster612.speecher.desktop")
    icon = os.path.join(
        FAKE_HOME, ".local", "share", "icons", "hicolor", "scalable", "apps",
        "io.github.firemonster612.speecher.svg")

    # The main window must exist for grab screenshots; the assistant then
    # opens on top of it.
    app_command("settings")
    time.sleep(1)
    app_command("setup")
    require("Welcome to Speecher", 20)
    ok("setup assistant opens on a fresh profile")
    if find_containing("Skip setup", 2) is not None:
        fail("Skip setup is offered while the install is still pending")
    ok("Skip setup is hidden until Speecher is installed")

    go_next("Transcription")
    require("Score", 10)
    require("8 / 10", 10)
    ok("the transcription provider stats include a score out of 10")
    require("is ready", 15)
    ok("the transcription gate opens once the provider checks out")

    go_next("Microphone")
    require("Microphone input detected.", 15)
    ok("the microphone gate opens once input is detected")

    go_next("Desktop accessibility")
    go_next("Text delivery")
    # On a host with a working or half-installed ydotool the gate can be
    # legitimately open already; the opt-out flow only exists while it holds.
    if wait_next_enabled(2):
        log("text delivery gate already open on this host; skipping the opt-out flow")
    else:
        ok("the text delivery gate holds without a virtual keyboard")
        click(require("without the virtual keyboard", 5, role="check box"))
        if not wait_next_enabled(5):
            fail("choosing clipboard-only paste did not open the text delivery gate")
        ok("choosing clipboard-only paste opens the text delivery gate")

    go_next("Refinement")
    require("Score", 10)
    require("8 / 10", 10)
    ok("the refinement provider stats include a score out of 10")

    go_next("Writing profiles")
    go_next("Global Shortcut")

    install = require("Install Speecher", 10, role="button")
    if state(next_button(), Atspi.StateType.ENABLED):
        fail("Next is enabled on the Global Shortcut page before installing")
    ok("Next stays off until Speecher is installed")
    # A shortcut bound before the move would break the moment the image moves.
    if find_containing("add a shortcut that runs", 2) is not None:
        fail("shortcut instructions are shown before the install")
    ok("shortcut controls wait for the install")
    if not os.path.isfile(source_image):
        fail(f"the AppImage is not at its starting place {source_image}")
    save_grab("before-install")

    click(install)
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline and not os.path.isfile(installed_image):
        time.sleep(0.3)
    if not os.path.isfile(installed_image):
        fail(f"the AppImage did not arrive in {installed_image}")
    if os.path.exists(source_image):
        fail("the original AppImage is still in Downloads after the install")
    ok("installing moved the AppImage into ~/Applications")

    if os.path.realpath(command_link) != os.path.realpath(installed_image):
        fail(f"{command_link} does not point at {installed_image}")
    if not os.path.isfile(desktop_file) or not os.path.isfile(icon):
        fail("the app menu entry or icon is missing after the install")
    ok("the speecher command, app menu entry and icon are installed")

    deadline = time.monotonic() + 10
    while time.monotonic() < deadline and not state(next_button(), Atspi.StateType.ENABLED):
        time.sleep(0.2)
    if not state(next_button(), Atspi.StateType.ENABLED):
        fail("Next did not come on after installing")
    ok("installing opens the gate to the next page")
    require("Skip setup", 10)
    ok("Skip setup returns once Speecher is installed")
    command = require("toggle", 10)
    if ".local" not in (command.get_name() or "").replace("​", ""):
        fail("the manual shortcut command does not go through ~/.local/bin")
    ok("the shortcut controls appear after the install with the installed path")
    save_grab("after-install")

    go_next("Ready to dictate")
    click(require("Finish", 10, role="button"))
    time.sleep(2)

    config_path = os.path.join(
        os.environ.get("XDG_CONFIG_HOME", os.path.join(OUT, "config")),
        "io.github.firemonster612", "speecher.conf")
    parser = configparser.ConfigParser()
    parser.read(config_path)
    if parser.get("app", "setupCompleted", fallback="false") != "true":
        fail(f"setupCompleted is not true in {config_path}")
    ok("finishing the assistant marks setup completed")

    app_command("quit")
    log("E2E-RESULT: PASS")


if __name__ == "__main__":
    main()
