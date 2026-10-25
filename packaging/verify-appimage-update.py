#!/usr/bin/env python3
"""Updates a released Speecher AppImage to a new one through the old app's own
updater, on a disposable Linux desktop, and checks what an installed user keeps.

  SPEECHER_DISPOSABLE_MACHINE=1 packaging/verify-appimage-update.py \\
      OLD.AppImage NEW.AppImage EVIDENCE_DIR [--keep-state]

Run it as the desktop user, inside the desktop session (KDE Plasma for the
KGlobalAccel checks), with sudo or as root. It changes the machine: /etc/hosts
sends github.com to a local HTTPS server, a throwaway CA joins the system trust
store, and the user's Speecher state becomes tests/migration/master-0.2.0.conf
with the command link, menu entry and icon 0.2.0's integration writes. With
--keep-state the AppImage, settings, keyring and integration already there are
used as they are, so a person can seed them through the old app first.

The released app has no feed override, so it fetches the real manifest URL,
which the local server answers with the new AppImage and its SHA-256. The old
app downloads, verifies and swaps it on its own; the script then presses the
old window's own "Restart now" over AT-SPI. Each check is a PASS or FAIL line
in EVIDENCE_DIR/summary.txt; the exit status is 1 if any failed.
"""

import argparse
import hashlib
import http.server
import json
import os
import re
import shutil
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

APP_ID = 'io.github.firemonster612.speecher'
REPO = Path(__file__).resolve().parent.parent
HOME = Path.home()
INSTALLED = HOME / 'Applications/Speecher-x86_64.AppImage'
SETTINGS = HOME / '.config/io.github.firemonster612/speecher.conf'
DATA = HOME / '.local/share/io.github.firemonster612/speecher'
LINK = HOME / '.local/bin/speecher'
DESKTOP = HOME / f'.local/share/applications/{APP_ID}.desktop'
ICON = HOME / f'.local/share/icons/hicolor/scalable/apps/{APP_ID}.svg'
MANIFEST_PATH = '/firemonster612/speecher/releases/latest/download/update-manifest.json'
IMAGE_PATH = '/firemonster612/speecher/releases/download/e2e/Speecher-x86_64.AppImage'
STUB_TRANSCRIPT = 'speecher migration stub transcript'
KGLOBALACCEL = ['gdbus', 'call', '--session', '--dest', 'org.kde.kglobalaccel',
                '--object-path', '/component/' + APP_ID.replace('.', '_')]

failed = []


def check(evidence, name, ok, detail=''):
    line = f'{name}={"PASS" if ok else "FAIL"} {detail}'.rstrip()
    print(line, flush=True)
    with open(evidence / 'summary.txt', 'a') as summary:
        summary.write(line + '\n')
    if not ok:
        failed.append(name)
    return ok


def run(*args, check_status=True, **kwargs):
    return subprocess.run(args, check=check_status, capture_output=True, text=True, timeout=120, **kwargs)


def sudo(*args):
    return run(*(args if os.geteuid() == 0 else ('sudo', *args)))


def sha256(path):
    with open(path, 'rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


def wait_for(predicate, timeout, what):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(1)
    raise TimeoutError(what)


# --- QSettings' INI files, as far as Speecher's keys need -------------------

def read_settings(path=SETTINGS):
    """Every key as QSettings names it (group/key), its value as a string,
    unquoted, with @ByteArray(...) unwrapped."""
    values, group = {}, ''
    for line in Path(path).read_text().splitlines():
        if line.startswith('[') and line.endswith(']'):
            group = '' if line == '[General]' else line[1:-1].replace('%2F', '/') + '/'
            continue
        if '=' not in line:
            continue
        key, value = line.split('=', 1)
        if value.startswith('"') and value.endswith('"'):
            value = re.sub(r'\\(.)', lambda m: {'n': '\n', 't': '\t'}.get(m[1], m[1]), value[1:-1])
        if value.startswith('@ByteArray(') and value.endswith(')'):
            value = value[len('@ByteArray('):-1]
        values[group + key.replace('\\', '/')] = value
    return values


def write_setting(key, value, path=SETTINGS):
    """Sets one plain value (no commas or quotes) while Speecher is not running."""
    group, name = key.rsplit('/', 1) if '/' in key else ('General', key)
    lines = Path(path).read_text().splitlines()
    header = f'[{group}]'
    if header not in lines:
        lines += ['', header]
    start = lines.index(header) + 1
    end = next((i for i in range(start, len(lines)) if lines[i].startswith('[')), len(lines))
    name = name.replace('/', '\\')
    for i in range(start, end):
        if lines[i].split('=', 1)[0] == name:
            lines[i] = f'{name}={value}'
            break
    else:
        lines.insert(end if end == len(lines) or lines[end - 1] else end - 1, f'{name}={value}')
    Path(path).write_text('\n'.join(lines) + '\n')


# --- The feed: github.com, served here --------------------------------------

def trust_store_command(ca):
    if Path('/etc/pki/ca-trust/source/anchors').is_dir():
        return [['cp', str(ca), '/etc/pki/ca-trust/source/anchors/speecher-e2e.pem'], ['update-ca-trust']]
    return [['cp', str(ca), '/usr/local/share/ca-certificates/speecher-e2e.crt'], ['update-ca-certificates']]


def serve_feed(work, evidence, new_image, version, build):
    certs = work / 'certs'
    certs.mkdir()
    run('openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2',
        '-keyout', certs / 'ca-key.pem', '-out', certs / 'ca.pem', '-subj', '/CN=Speecher update E2E CA')
    run('openssl', 'req', '-newkey', 'rsa:2048', '-nodes', '-keyout', certs / 'key.pem',
        '-out', certs / 'server.csr', '-subj', '/CN=github.com')
    (certs / 'server.ext').write_text('subjectAltName=DNS:github.com\nextendedKeyUsage=serverAuth\n')
    run('openssl', 'x509', '-req', '-days', '2', '-sha256', '-in', certs / 'server.csr',
        '-CA', certs / 'ca.pem', '-CAkey', certs / 'ca-key.pem', '-CAcreateserial',
        '-out', certs / 'server.pem', '-extfile', certs / 'server.ext')
    for command in trust_store_command(certs / 'ca.pem'):
        sudo(*command)
    hosts = Path('/etc/hosts').read_text()
    if 'github.com # speecher-e2e' not in hosts:
        sudo('sh', '-c', 'printf "127.0.0.1 github.com # speecher-e2e\\n" >> /etc/hosts')

    manifest = json.dumps({
        'version': version,
        'buildNumber': build,
        'linux-x86_64': {'appimage': 'https://github.com' + IMAGE_PATH, 'sha256': sha256(new_image)},
    }).encode()
    (evidence / 'update-manifest.json').write_bytes(manifest)
    log = open(evidence / 'https-requests.log', 'a')

    class Feed(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == MANIFEST_PATH:
                body = manifest
            elif self.path == IMAGE_PATH:
                body = Path(new_image).read_bytes()
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format, *args):
            log.write(f'{time.strftime("%H:%M:%S")} {format % args}\n')
            log.flush()

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 443), Feed)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certs / 'server.pem', certs / 'key.pem')
    server.socket = context.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    run('curl', '--fail', '--silent', '--output', '/dev/null', 'https://github.com' + MANIFEST_PATH)


def serve_stub_transcriber():
    """An OpenAI-compatible transcription endpoint that hears STUB_TRANSCRIPT."""
    class Stub(http.server.BaseHTTPRequestHandler):
        def do_POST(self):
            self.rfile.read(int(self.headers.get('Content-Length', 0)))
            body = json.dumps({'text': STUB_TRANSCRIPT}).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format, *args):
            pass

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Stub)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server.server_address[1]


# --- The installed app ------------------------------------------------------

def daemons():
    """Speecher's own processes (not the AppImage runtime around them)."""
    found = []
    for proc in Path('/proc').iterdir():
        try:
            exe = os.readlink(proc / 'exe')
        except OSError:
            continue
        if exe.endswith('/usr/bin/speecher') and proc.name.isdigit():
            cmdline = (proc / 'cmdline').read_bytes().split(b'\0')
            if not any(arg in (b'status', b'quit', b'start', b'stop', b'last', b'vocabulary') for arg in cmdline):
                found.append(int(proc.name))
    return found


def cli(*args):
    return run(str(LINK if LINK.exists() else INSTALLED), *args, check_status=False)


def launch(image, *args):
    env = dict(os.environ, QT_LINUX_ACCESSIBILITY_ALWAYS_ON='1')
    subprocess.Popen([str(image), *args], env=env, stdin=subprocess.DEVNULL,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    return wait_for(lambda: daemons(), 60, f'{image} did not start')[0]


def quit_speecher():
    cli('quit')
    try:
        wait_for(lambda: not daemons(), 30, 'speecher did not quit')
    except TimeoutError:
        for pid in daemons():
            os.kill(pid, 9)


def press(app_name, button_name, timeout=60):
    """Presses a push button by its accessible name, as a person clicks it."""
    import gi
    gi.require_version('Atspi', '2.0')
    from gi.repository import Atspi

    def find(node, depth=0):
        if depth > 40:
            return None
        if node.get_role() == Atspi.Role.PUSH_BUTTON and node.get_name() == button_name and \
                node.get_state_set().contains(Atspi.StateType.SHOWING):
            return node
        for index in range(node.get_child_count()):
            child = node.get_child_at_index(index)
            if child and (found := find(child, depth + 1)):
                return found
        return None

    def attempt():
        desktop = Atspi.get_desktop(0)
        for index in range(desktop.get_child_count()):
            app = desktop.get_child_at_index(index)
            if app and app.get_name() == app_name and (button := find(app)):
                return button
        return None

    button = wait_for(attempt, timeout, f'no "{button_name}" button in {app_name}')
    button.get_action_iface().do_action(0)


def install_desktop_integration(image, work):
    """The command link, menu entry and icon 0.2.0's LinuxDesktopIntegration
    writes, from the image's own files."""
    extracted = work / 'integration'
    extracted.mkdir()
    sources = (f'usr/share/applications/{APP_ID}.desktop', f'usr/share/icons/hicolor/scalable/apps/{APP_ID}.svg')
    for name in sources:
        run(str(image), '--appimage-extract', name, cwd=extracted)
    root = extracted / 'squashfs-root'
    desktop = (root / sources[0]).read_text()
    quoted = '"' + str(image).replace('\\', '\\\\').replace('"', '\\"') + '"'
    desktop = re.sub(r'^Exec=\S+', 'Exec=' + quoted, desktop, flags=re.M)
    for path in (LINK, DESKTOP, ICON):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.unlink(missing_ok=True)
    LINK.symlink_to(image)
    DESKTOP.write_text(desktop)
    shutil.copyfile(root / sources[1], ICON)


def shortcut_infos():
    return run(*KGLOBALACCEL, '--method', 'org.kde.kglobalaccel.Component.allShortcutInfos',
               check_status=False).stdout.strip()


def status():
    result = cli('status', '--json')
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError:
        return {'error': result.stdout + result.stderr}


# --- The run ----------------------------------------------------------------

def expected_state(evidence, label, values):
    """The 0.2.0 fixture's state, as the new app reads and saves it."""
    expected = {
        'app/setupCompleted': 'true',
        'shortcuts/activationMode': 'toggle',
        'audio/deviceId': 'alsa_input.fixture-mic',
        'audio/preRollMs': '400',
        'refinement/provider': 'anthropic',
        'refinement/defaultWritingProfile': 'work',
        'openai/model': 'gpt-6-luna',
        'anthropic/model': 'claude-opus-5-5',
        'anthropic/auth/mode': 'cliproxy',
        'output/method': 'automatic',
        'output/format': 'html',
        'updates/channel': 'stable',
        'cliproxy/baseUrl': 'http://127.0.0.1:8317',
    }
    wrong = {key: values.get(key) for key, want in expected.items() if values.get(key) != want}
    check(evidence, f'{label}.settings', not wrong, json.dumps(wrong) if wrong else f'{len(expected)} keys as expected')
    vocabulary = json.loads(values.get('stt/vocabularyEntries', '[]'))
    check(evidence, f'{label}.vocabulary', len(vocabulary) >= 262 and vocabulary[0].get('starred') is True,
          f'{len(vocabulary)} entries')
    check(evidence, f'{label}.old_window_keys_untouched', 'ui/appWindow/a/geometry' in values)
    keyring_has_proxy = 'cliproxy-api-key' in values.get('secrets/inKeyring', '')
    check(evidence, f'{label}.proxy_key_kept', keyring_has_proxy or values.get('cliproxy/apiKey') == 'fixture-proxy-key',
          'in the keyring' if keyring_has_proxy else 'in the settings file')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('old', type=Path)
    parser.add_argument('new', type=Path)
    parser.add_argument('evidence', type=Path)
    parser.add_argument('--keep-state', action='store_true')
    options = parser.parse_args()
    if os.environ.get('SPEECHER_DISPOSABLE_MACHINE') != '1':
        sys.exit('This edits /etc/hosts, the trust store and Speecher\'s state; '
                 'set SPEECHER_DISPOSABLE_MACHINE=1 on a throwaway machine.')
    evidence = options.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / 'summary.txt').write_text('')
    work = Path(tempfile.mkdtemp(prefix='speecher-update-e2e-'))

    # The new image's version, asked in a home of its own so its startup
    # touches nothing of the installed user's.
    scratch = work / 'home'
    scratch.mkdir()
    version_line = run(str(options.new.resolve()), '--version', env=dict(
        os.environ, HOME=str(scratch), XDG_CONFIG_HOME=str(scratch / 'config'),
        XDG_CACHE_HOME=str(scratch / 'cache'), XDG_DATA_HOME=str(scratch / 'data'))).stdout
    match = re.search(r'speecher (\S+) \(build (\d+)\)', version_line)
    version, build = match[1], int(match[2])
    new_sha = sha256(options.new)

    quit_speecher()
    if not options.keep_state:
        INSTALLED.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(options.old, INSTALLED)
        INSTALLED.chmod(0o755)
        SETTINGS.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(REPO / 'tests/migration/master-0.2.0.conf', SETTINGS)
    if not options.keep_state or not DESKTOP.exists():
        install_desktop_integration(INSTALLED, work)
    # Checked at startup, and installed without asking.
    write_setting('updates/lastCheckTime', '0')
    write_setting('updates/autoCheck', 'true')
    write_setting('updates/autoInstall', 'true')
    DATA.mkdir(parents=True, exist_ok=True)
    (DATA / 'sentinel.txt').write_text('kept\n')
    shutil.copyfile(SETTINGS, evidence / 'settings-before.conf')
    desktop_before = DESKTOP.read_text()
    old_sha = sha256(INSTALLED)
    check(evidence, 'old.installed', old_sha != new_sha, f'{INSTALLED} sha256 {old_sha}')

    serve_feed(work, evidence, options.new.resolve(), version, build)
    old_pid = launch(INSTALLED)
    shortcuts_before = wait_for(lambda: 'toggle' in shortcut_infos() and shortcut_infos(), 30,
                                'old app registered no global shortcut')
    (evidence / 'shortcuts-before.txt').write_text(shortcuts_before + '\n')
    check(evidence, 'old.running', bool(old_pid), f'pid {old_pid}')

    try:
        wait_for(lambda: sha256(INSTALLED) == new_sha, 300, 'the old app did not swap in the new image')
        check(evidence, 'old.downloaded_verified_swapped', True,
              f'{INSTALLED} now sha256 {new_sha}, mode {oct(INSTALLED.stat().st_mode & 0o777)}')
    except TimeoutError as error:
        check(evidence, 'old.downloaded_verified_swapped', False, str(error))
        return finish(evidence)
    check(evidence, 'old.executable_kept', os.access(INSTALLED, os.X_OK))

    press('speecher', 'Restart now')
    try:
        new_pid = wait_for(lambda: [pid for pid in daemons() if pid != old_pid], 90, 'no relaunch')[0]
    except TimeoutError as error:
        check(evidence, 'relaunched', False, str(error))
        return finish(evidence)
    wait_for(lambda: old_pid not in daemons(), 30, 'the old process stayed')
    time.sleep(5)
    environ = dict(kv.split('=', 1) for kv in Path(f'/proc/{new_pid}/environ').read_text().split('\0') if '=' in kv)
    check(evidence, 'relaunched', environ.get('APPIMAGE') == str(INSTALLED),
          f'pid {old_pid} -> {new_pid} from {environ.get("APPIMAGE")}')
    check(evidence, 'one_daemon', daemons() == [new_pid], f'{daemons()}')
    check(evidence, 'version', version in cli('--version').stdout, cli('--version').stdout.strip())
    state = status()
    check(evidence, 'status', state.get('state') == 'idle', json.dumps(state))

    values = read_settings()
    expected_state(evidence, 'first_launch', values)
    check(evidence, 'sentinel_kept', (DATA / 'sentinel.txt').read_text() == 'kept\n')
    check(evidence, 'desktop_entry_kept', DESKTOP.read_text() == desktop_before
          and f'Exec="{INSTALLED}"' in DESKTOP.read_text(), str(DESKTOP))
    check(evidence, 'command_link_kept', LINK.is_symlink() and LINK.resolve() == INSTALLED.resolve())
    check(evidence, 'icon_kept', ICON.is_file())
    shortcuts_after = shortcut_infos()
    (evidence / 'shortcuts-after.txt').write_text(shortcuts_after + '\n')
    keys = lambda infos: re.findall(r"\[(\d+)\], \[(\d+)\]", infos)
    check(evidence, 'shortcut_kept', keys(shortcuts_after) == keys(shortcuts_before) and keys(shortcuts_after),
          shortcuts_after)
    active = run(*KGLOBALACCEL, '--method', 'org.kde.kglobalaccel.Component.isActive', check_status=False).stdout
    check(evidence, 'shortcut_active', 'true' in active, active.strip())

    # One edit through the running app, then a restart from the menu entry.
    cli('vocabulary', 'add', 'migration e2e term')
    quit_speecher()
    shutil.copyfile(SETTINGS, evidence / 'settings-after-edit.conf')
    launch(INSTALLED)
    time.sleep(5)
    values = read_settings()
    expected_state(evidence, 'after_edit_and_restart', values)
    terms = [entry['term'] for entry in json.loads(values.get('stt/vocabularyEntries', '[]'))]
    check(evidence, 'edit_saved', 'migration e2e term' in terms)

    # The global shortcut toggles dictation, and dictation reaches a stub
    # speech service.
    quit_speecher()
    port = serve_stub_transcriber()
    for key, value in (('stt/provider', 'endpoint'), ('endpoint/speech/baseUrl', f'http://127.0.0.1:{port}/v1'),
                       ('endpoint/speech/model', 'stub'), ('refinement/provider', 'none'),
                       ('audio/vadEnabled', 'false'), ('audio/captureMode', 'on_demand')):
        write_setting(key, value)
    launch(INSTALLED)
    time.sleep(5)
    run(*KGLOBALACCEL, '--method', 'org.kde.kglobalaccel.Component.invokeShortcut', 'toggle-dictation',
        check_status=False)
    try:
        wait_for(lambda: status().get('state') in ('recording', 'listening'), 20, 'shortcut did not start dictation')
        check(evidence, 'shortcut_starts_dictation', True, json.dumps(status()))
    except TimeoutError as error:
        check(evidence, 'shortcut_starts_dictation', False, f'{error}: {json.dumps(status())}')
        cli('start')
    time.sleep(3)
    cli('stop')
    try:
        wait_for(lambda: STUB_TRANSCRIPT in cli('last').stdout, 60, 'no transcript')
        check(evidence, 'dictation_with_stub', True, cli('last').stdout.strip())
    except TimeoutError:
        check(evidence, 'dictation_with_stub', False, f'last={cli("last").stdout!r} status={json.dumps(status())}')
    return finish(evidence)


def finish(evidence):
    for path in (SETTINGS, HOME / '.cache/io.github.firemonster612/speecher/speecher.log'):
        if path.exists():
            shutil.copyfile(path, evidence / ('final-' + path.name))
    print(f'{len(failed)} failed: {failed}' if failed else 'all passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
