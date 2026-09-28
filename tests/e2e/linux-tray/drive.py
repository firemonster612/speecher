#!/usr/bin/env python3
"""D-Bus driver for the Linux tray icon E2E flow.

Reads the StatusNotifierItem the daemon registered with the rig's watcher,
checks its tooltip, icon and menu, starts and stops a dictation through the
menu over com.canonical.dbusmenu, and quits the daemon through the menu.

Evidence: every assertion prints an `assert name=... ok` line and the run ends
with E2E-RESULT: PASS|FAIL. Exits non-zero on the first failed assertion.
"""

import os
import subprocess
import sys
import time

from gi.repository import Gio, GLib

OUT = os.environ["E2E_FLOW_DIR"]
APP = os.environ["E2E_APP"]
APP_ENV = dict(os.environ, APPIMAGE_EXTRACT_AND_RUN="1")
BUS = Gio.bus_get_sync(Gio.BusType.SESSION, None)

SNI_INTERFACE = "org.kde.StatusNotifierItem"
MENU_INTERFACE = "com.canonical.dbusmenu"


def log(message: str) -> None:
    print(message, flush=True)


def fail(message: str) -> None:
    log(f"assert name={message!r} ok=FALSE")
    tail = os.path.join(OUT, "app-stdio.log")
    if os.path.isfile(tail):
        with open(tail) as handle:
            log("app-stdio tail: " + " | ".join(handle.readlines()[-15:]))
    log("E2E-RESULT: FAIL")
    sys.exit(1)


def ok(message: str) -> None:
    log(f"assert name={message!r} ok=true")


def call(service, path, interface, method, params=None):
    return BUS.call_sync(service, path, interface, method, params, None,
                         Gio.DBusCallFlags.NONE, 5000, None)


def sni_property(service, path, name):
    result = call(service, path, "org.freedesktop.DBus.Properties", "Get",
                  GLib.Variant("(ss)", (SNI_INTERFACE, name)))
    return result.unpack()[0]


def tooltip_strings(service, path) -> list[str]:
    tooltip = sni_property(service, path, "ToolTip")
    return [part for part in tooltip if isinstance(part, str) and part]


def wait_for_item(timeout: float = 30):
    events = os.path.join(OUT, "sni-events.log")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if os.path.isfile(events):
            for line in open(events):
                if line.startswith("registered "):
                    _, service, path = line.split()
                    return service, path
        time.sleep(0.2)
    fail(f"no StatusNotifierItem was registered within {timeout}s")


def item_unregistered(timeout: float = 15) -> bool:
    events = os.path.join(OUT, "sni-events.log")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if any(line.startswith("unregistered ") for line in open(events)):
            return True
        time.sleep(0.2)
    return False


def menu_items(service, menu_path) -> dict[str, int]:
    call(service, menu_path, MENU_INTERFACE, "AboutToShow",
         GLib.Variant("(i)", (0,)))
    result = call(service, menu_path, MENU_INTERFACE, "GetLayout",
                  GLib.Variant("(iias)", (0, -1, [])))
    _revision, layout = result.unpack()
    items: dict[str, int] = {}

    def walk(node):
        node_id, props, children = node
        label = props.get("label", "")
        if label:
            items[label] = node_id
        for child in children:
            walk(child)

    walk(layout)
    return items


def menu_click(service, menu_path, item_id: int) -> None:
    call(service, menu_path, MENU_INTERFACE, "Event",
         GLib.Variant("(isvu)", (item_id, "clicked", GLib.Variant("s", ""), 0)))


def wait_tooltip(service, path, wanted: str, timeout: float = 20) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if wanted in tooltip_strings(service, path):
            return True
        time.sleep(0.3)
    return False


def app_status() -> str:
    result = subprocess.run([APP, "status"], env=APP_ENV,
                            capture_output=True, text=True, timeout=30)
    return result.stdout.strip()


def wait_status(wanted: str, timeout: float = 20) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if app_status() == wanted:
            return True
        time.sleep(0.5)
    return False


def main() -> None:
    service, path = wait_for_item()
    ok("the daemon registers a StatusNotifierItem on startup")
    log(f"item service={service} path={path}")

    icon_name = sni_property(service, path, "IconName")
    log(f"IconName={icon_name!r} ToolTip={tooltip_strings(service, path)!r}")
    if icon_name not in ("io.github.firemonster612.speecher",
                        "audio-input-microphone"):
        fail(f"unexpected tray icon name {icon_name!r}")
    ok("the tray icon uses the app icon (or its themed fallback)")
    if "Speecher" not in tooltip_strings(service, path):
        fail("the tray tooltip does not say Speecher")
    ok("the tray tooltip says Speecher while idle")

    menu_path = sni_property(service, path, "Menu")
    items = menu_items(service, menu_path)
    log(f"menu items={items}")
    for label in ("Start dictation", "Settings…", "Quit Speecher"):
        if label not in items:
            fail(f"menu is missing {label!r}")
    ok("the menu offers Start dictation, Settings… and Quit Speecher")

    menu_click(service, menu_path, items["Start dictation"])
    if not wait_tooltip(service, path, "Speecher is listening"):
        fail(f"tooltip never showed listening; status={app_status()!r}")
    ok("Start dictation from the tray menu starts listening")
    if not wait_status("listening"):
        fail(f"daemon state is {app_status()!r}, not listening")
    ok("the daemon reports the listening state over IPC")
    items = menu_items(service, menu_path)
    if "Stop dictation" not in items:
        fail(f"menu did not flip to Stop dictation while listening: {items}")
    ok("the menu flips to Stop dictation while listening")

    menu_click(service, menu_path, items["Stop dictation"])
    if not wait_tooltip(service, path, "Speecher"):
        fail(f"tooltip never returned to idle; status={app_status()!r}")
    if not wait_status("idle"):
        fail(f"daemon state is {app_status()!r}, not idle after the stop")
    ok("Stop dictation from the tray menu returns the daemon to idle")
    items = menu_items(service, menu_path)
    if "Start dictation" not in items:
        fail(f"menu did not flip back to Start dictation: {items}")
    ok("the menu flips back to Start dictation when idle")

    # Qt registers the item by object path, so `service` is a unique bus name;
    # resolve the daemon pid from the bus before quitting.
    try:
        result = call("org.freedesktop.DBus", "/org/freedesktop/DBus",
                      "org.freedesktop.DBus", "GetConnectionUnixProcessID",
                      GLib.Variant("(s)", (service,)))
        pid = result.unpack()[0]
    except GLib.Error as error:
        fail(f"could not resolve the daemon pid from {service!r}: {error}")
    log(f"daemon pid={pid}")
    menu_click(service, menu_path, items["Quit Speecher"])
    if not item_unregistered():
        fail("the StatusNotifierItem is still on the bus after Quit")
    ok("Quit removes the tray item from the bus")
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline and os.path.exists(f"/proc/{pid}"):
        time.sleep(0.3)
    if os.path.exists(f"/proc/{pid}"):
        fail(f"daemon pid {pid} is still alive after Quit")
    ok("Quit ends the daemon process")

    log("E2E-RESULT: PASS")


if __name__ == "__main__":
    main()
