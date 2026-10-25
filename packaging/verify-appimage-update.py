#!/usr/bin/env python3
"""Updates a released Speecher AppImage to a new one through the old app's own
updater, on a disposable Linux desktop, and checks what an installed user keeps.

  SPEECHER_DISPOSABLE_MACHINE=1 packaging/verify-appimage-update.py \\
      OLD.AppImage NEW.AppImage EVIDENCE_DIR [--keep-state] [--extract-and-run]
      [--keywatch-helper]

Run it as the desktop user, inside the desktop session (KDE Plasma for the
KGlobalAccel checks) with an unlocked keyring, with sudo or as root. It
changes the machine: /etc/hosts sends github.com and api.openai.com to a local
HTTPS server, a throwaway CA joins the system trust store, and the user's
Speecher state becomes tests/migration/master-0.2.0.conf with the command
link, menu entry and icon 0.2.0's integration writes, and an OpenAI key and a
CLI Proxy key made up for the run. With --keep-state the AppImage, settings,
keyring and integration already there are used as they are, so a person can
seed them through the old app first. Then the OpenAI key is proved only when
SPEECHER_E2E_OPENAI_KEY names the one they saved, and the proxy key only when
the settings file holds one; their auth modes are left as they are otherwise.

The released app has no feed override, so it fetches the real manifest URL,
which the local server answers with the new AppImage and its SHA-256. The old
app saves the OpenAI key in the keyring itself, then downloads, verifies and
swaps the image on its own; the script presses the old window's own "Restart
now" over AT-SPI. The new app's settings are compared with the old ones as the
update should leave them (packaging/update_expectations.py), and the new
binary's requests to stub OpenAI and proxy servers show it reads both keys,
after the update and again after an edit and a restart. --extract-and-run
runs every AppImage invocation extracted, as APPIMAGE_EXTRACT_AND_RUN does
where FUSE is missing. --keywatch-helper installs 0.2.0's key-watch helper
first and binds F13 through it, then checks the new app takes the key from
that same helper. Each check is a PASS or FAIL line in EVIDENCE_DIR/summary.txt;
the exit status is 1 if any failed.
"""

import argparse
import fcntl
import getpass
import hashlib
import http.server
import json
import math
import os
import re
import secrets
import shutil
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import update_expectations  # noqa: E402

APP_ID = 'io.github.firemonster612.speecher'
REPO = Path(__file__).resolve().parent.parent
HOME = Path.home()
INSTALLED = HOME / 'Applications/Speecher-x86_64.AppImage'
SETTINGS = HOME / '.config/io.github.firemonster612/speecher.conf'
DATA = HOME / '.local/share/io.github.firemonster612/speecher'
LINK = HOME / '.local/bin/speecher'
DESKTOP = HOME / f'.local/share/applications/{APP_ID}.desktop'
ICON = HOME / f'.local/share/icons/hicolor/scalable/apps/{APP_ID}.svg'
HELPER = Path('/usr/local/lib/speecher/speecher-keywatchd')
MANIFEST_PATH = '/firemonster612/speecher/releases/latest/download/update-manifest.json'
IMAGE_PATH = '/firemonster612/speecher/releases/download/e2e/Speecher-x86_64.AppImage'
STUB_TRANSCRIPT = 'speecher migration stub transcript'
REFINED = 'speecher migration refined'
KGLOBALACCEL = ['gdbus', 'call', '--session', '--dest', 'org.kde.kglobalaccel',
                '--object-path', '/component/' + APP_ID.replace('.', '_')]
KEY_A, KEY_Z, KEY_F13 = 30, 44, 183

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
    """Sets one plain value (no commas or quotes) while Speecher is not
    running, where QSettings keeps it: the first part of the key names the
    group."""
    group, name = key.split('/', 1) if '/' in key else ('General', key)
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


def save_redacted(source, target):
    """A copy of a settings file for the evidence, without its keys."""
    text = re.sub(r'^(\S*apiKey)=.*$', r'\1=<redacted>', Path(source).read_text(), flags=re.M)
    Path(target).write_text(text)


# --- The servers: github.com, api.openai.com and the proxy, served here -----

class Credentials:
    """The key a request to each stub must carry, and which stubs have seen
    theirs. Only the outcome is kept, never a key."""

    def __init__(self, keys):
        self.keys = keys  # stub name -> key
        self.seen = set()

    def note(self, name, headers):
        if name not in self.keys:
            return False
        sent = ' '.join(headers.get(header, '') for header in ('Authorization', 'x-api-key'))
        ok = self.keys[name] in sent
        if ok:
            self.seen.add(name)
        return ok


def trust_store_command(ca):
    if Path('/etc/pki/ca-trust/source/anchors').is_dir():
        return [['cp', str(ca), '/etc/pki/ca-trust/source/anchors/speecher-e2e.pem'], ['update-ca-trust']]
    return [['cp', str(ca), '/usr/local/share/ca-certificates/speecher-e2e.crt'], ['update-ca-certificates']]


def serve_https(work, evidence, new_image, version, build, credentials):
    """github.com's release feed, and api.openai.com's Responses endpoint,
    which answers only a request with the OpenAI key."""
    certs = work / 'certs'
    certs.mkdir()
    run('openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2',
        '-keyout', certs / 'ca-key.pem', '-out', certs / 'ca.pem', '-subj', '/CN=Speecher update E2E CA')
    run('openssl', 'req', '-newkey', 'rsa:2048', '-nodes', '-keyout', certs / 'key.pem',
        '-out', certs / 'server.csr', '-subj', '/CN=github.com')
    (certs / 'server.ext').write_text('subjectAltName=DNS:github.com,DNS:api.openai.com\nextendedKeyUsage=serverAuth\n')
    run('openssl', 'x509', '-req', '-days', '2', '-sha256', '-in', certs / 'server.csr',
        '-CA', certs / 'ca.pem', '-CAkey', certs / 'ca-key.pem', '-CAcreateserial',
        '-out', certs / 'server.pem', '-extfile', certs / 'server.ext')
    for command in trust_store_command(certs / 'ca.pem'):
        sudo(*command)
    hosts = Path('/etc/hosts').read_text()
    for host in ('github.com', 'api.openai.com'):
        if f'{host} # speecher-e2e' not in hosts:
            sudo('sh', '-c', f'printf "127.0.0.1 {host} # speecher-e2e\\n" >> /etc/hosts')

    manifest = json.dumps({
        'version': version,
        'buildNumber': build,
        'linux-x86_64': {'appimage': 'https://github.com' + IMAGE_PATH, 'sha256': sha256(new_image)},
    }).encode()
    (evidence / 'update-manifest.json').write_bytes(manifest)
    refined = b''.join(f'event: {name}\ndata: {json.dumps(dict(data, type=name))}\n\n'.encode() for name, data in (
        ('response.output_text.delta', {'delta': REFINED}), ('response.completed', {'response': {}})))
    log = open(evidence / 'https-requests.log', 'a')

    class Server(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == MANIFEST_PATH:
                self.reply(manifest)
            elif self.path == IMAGE_PATH:
                self.reply(Path(new_image).read_bytes())
            else:
                self.send_error(404)

        def do_POST(self):
            self.rfile.read(int(self.headers.get('Content-Length', 0)))
            if self.path == '/v1/responses' and credentials.note('openai', self.headers):
                self.reply(refined, 'text/event-stream')
            else:
                self.send_error(401)

        def reply(self, body, content_type='application/octet-stream'):
            self.send_response(200)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format, *args):
            log.write(f'{time.strftime("%H:%M:%S")} {format % args}\n')
            log.flush()

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 443), Server)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certs / 'server.pem', certs / 'key.pem')
    server.socket = context.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    run('curl', '--fail', '--silent', '--output', '/dev/null', 'https://github.com' + MANIFEST_PATH)


def serve_proxy(port, credentials):
    """A CLI Proxy API server that only notes whether a request carries the
    proxy key."""
    class Proxy(http.server.BaseHTTPRequestHandler):
        def do_POST(self):
            self.rfile.read(int(self.headers.get('Content-Length', 0)))
            self.send_error(502 if credentials.note('proxy', self.headers) else 401)

        do_GET = do_POST

        def log_message(self, format, *args):
            pass

    server = http.server.ThreadingHTTPServer(('127.0.0.1', port), Proxy)
    threading.Thread(target=server.serve_forever, daemon=True).start()


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


def write_tone(path):
    """A second of warbling tone, for a speech server to be asked about."""
    with wave.open(str(path), 'wb') as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(16000)
        audio.writeframes(b''.join(struct.pack('<h', int(8000 * math.sin(i * (0.12 + 0.04 * math.sin(i / 800)))))
                                   for i in range(16000)))


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
            if not any(arg in (b'status', b'quit', b'start', b'stop', b'cancel', b'last', b'vocabulary',
                               b'transcribe') for arg in cmdline):
                found.append(int(proc.name))
    return found


def cli(*args):
    return run(str(LINK if LINK.exists() else INSTALLED), *args, check_status=False)


def launch(image, old=False):
    """Starts the installed app and waits for it; the new app also answers
    status --json, which 0.2.0's CLI lacks."""
    env = dict(os.environ, QT_LINUX_ACCESSIBILITY_ALWAYS_ON='1')
    subprocess.Popen([str(image)], env=env, stdin=subprocess.DEVNULL,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    pid = wait_for(lambda: daemons(), 60, f'{image} did not start')[0]
    if not old:
        wait_for(lambda: 'state' in status(), 60, f'{image} did not answer')
    return pid


def quit_speecher():
    # Nothing to quit on a machine Speecher was never installed on.
    if not daemons():
        return
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


def extract(image, work, *names):
    """The image's files, or only names, in a new folder under work."""
    root = Path(tempfile.mkdtemp(prefix='extracted-', dir=work))
    if not names:
        run(str(image), '--appimage-extract', cwd=root)
    # The runtime takes one name a call.
    for name in names:
        run(str(image), '--appimage-extract', name, cwd=root)
    return root / 'squashfs-root'


def install_desktop_integration(image, work):
    """The command link, menu entry and icon 0.2.0's LinuxDesktopIntegration
    writes, from the image's own files."""
    sources = (f'usr/share/applications/{APP_ID}.desktop', f'usr/share/icons/hicolor/scalable/apps/{APP_ID}.svg')
    root = extract(image, work, *sources)
    desktop = (root / sources[0]).read_text()
    quoted = '"' + str(image).replace('\\', '\\\\').replace('"', '\\"') + '"'
    desktop = re.sub(r'^Exec=\S+', 'Exec=' + quoted, desktop, flags=re.M)
    for path in (LINK, DESKTOP, ICON):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.unlink(missing_ok=True)
    LINK.symlink_to(image)
    DESKTOP.write_text(desktop)
    shutil.copyfile(root / sources[1], ICON)


def install_old_keywatch_helper(image, work):
    """The helper as the old image's own installer, which the app runs
    through pkexec, puts it on the system."""
    root = extract(image, work)
    setup = [str(root / 'usr/libexec/speecher/speecher-keywatch-setup'), '--install', '--user', getpass.getuser()]
    return run(*([] if os.geteuid() == 0 else ['sudo']), *setup, check_status=False)


class Keyboard:
    """A uinput keyboard that taps F13. The key-watch helper reads only
    devices with the letter keys, so it has A to Z as well."""

    UI_SET_EVBIT, UI_SET_KEYBIT, UI_DEV_CREATE = 0x40045564, 0x40045565, 0x5501
    EV_SYN, EV_KEY = 0, 1

    def __init__(self):
        self.device = os.open('/dev/uinput', os.O_WRONLY | os.O_NONBLOCK)
        fcntl.ioctl(self.device, self.UI_SET_EVBIT, self.EV_KEY)
        for key in (*range(KEY_A, KEY_Z + 1), KEY_F13):
            fcntl.ioctl(self.device, self.UI_SET_KEYBIT, key)
        # struct uinput_user_dev: name, input_id (bus USB), ff_effects_max, four abs arrays.
        os.write(self.device, struct.pack('80sHHHHI', b'speecher-e2e-keyboard', 3, 1, 1, 1, 0) + bytes(4 * 64 * 4))
        fcntl.ioctl(self.device, self.UI_DEV_CREATE)

    def tap(self, key):
        for value in (1, 0):
            os.write(self.device, struct.pack('llHHi', 0, 0, self.EV_KEY, key, value)
                     + struct.pack('llHHi', 0, 0, self.EV_SYN, 0, 0))
            time.sleep(0.1)


def helper_journal():
    return run('journalctl', '-u', 'speecher-keywatchd', '--no-pager', '-o', 'cat', check_status=False).stdout


def helper_watching(pid):
    """Whether the key-watch helper took this process's request to watch F13,
    as the helper logs it."""
    return f'watching for uid {os.getuid()} pid {pid} key F13' in helper_journal()


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

def expected_state(evidence, label, before, written):
    """Checks the settings against before as the update should leave it, with
    what the script wrote since. Returns them."""
    values = read_settings()
    wrong = update_expectations.differences(before, values, written)
    check(evidence, f'{label}.settings', not wrong,
          json.dumps(wrong) if wrong else f'{len(before)} saved keys as the update should leave them')
    terms = lambda entries: {entry['term']: entry.get('starred', False) for entry in entries}
    old, new = terms(update_expectations.vocabulary(before)), terms(update_expectations.vocabulary(values))
    check(evidence, f'{label}.vocabulary', old.items() <= new.items(), f'{len(new)} entries, {len(old)} before')
    return values


def prove_keys(evidence, label, credentials, audio):
    """The new binary, with no window, refines a transcript through each stub,
    which takes the request only with the key the old app saved."""
    for refine, name in (('openai', 'openai'), ('anthropic', 'proxy')):
        if name not in credentials.keys:
            continue
        credentials.seen.discard(name)
        result = run(str(INSTALLED), 'transcribe', '--model', 'endpoint', '--refine', refine,
                     '--output', 'none', '--stdout', str(audio), check_status=False)
        check(evidence, f'{label}.{name}_key_read', name in credentials.seen,
              f'transcribe exit {result.returncode}: {result.stdout.strip()[-200:]}')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('old', type=Path)
    parser.add_argument('new', type=Path)
    parser.add_argument('evidence', type=Path)
    parser.add_argument('--keep-state', action='store_true')
    parser.add_argument('--extract-and-run', action='store_true')
    parser.add_argument('--keywatch-helper', action='store_true')
    options = parser.parse_args()
    if os.environ.get('SPEECHER_DISPOSABLE_MACHINE') != '1':
        sys.exit('This edits /etc/hosts, the trust store and Speecher\'s state; '
                 'set SPEECHER_DISPOSABLE_MACHINE=1 on a throwaway machine.')
    evidence = options.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / 'summary.txt').write_text('')
    work = Path(tempfile.mkdtemp(prefix='speecher-update-e2e-'))
    if options.extract_and_run:
        # Inherited by every AppImage this runs, and by the apps' relaunches.
        os.environ['APPIMAGE_EXTRACT_AND_RUN'] = '1'

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
    if options.keep_state:
        keys = {}
        if os.environ.get('SPEECHER_E2E_OPENAI_KEY'):
            keys['openai'] = os.environ['SPEECHER_E2E_OPENAI_KEY']
        if read_settings().get('cliproxy/apiKey'):
            keys['proxy'] = read_settings()['cliproxy/apiKey']
    else:
        INSTALLED.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(options.old, INSTALLED)
        INSTALLED.chmod(0o755)
        SETTINGS.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(REPO / 'tests/migration/master-0.2.0.conf', SETTINGS)
        # 0.2.0 moves the OpenAI key into the keyring as it starts; the proxy
        # key it keeps in its file, for the new app to move.
        keys = {'openai': 'sk-e2e-' + secrets.token_hex(16), 'proxy': 'e2e-proxy-' + secrets.token_hex(12)}
        write_setting('openai/apiKey', keys['openai'])
        write_setting('cliproxy/apiKey', keys['proxy'])
    if not options.keep_state or not DESKTOP.exists():
        install_desktop_integration(INSTALLED, work)
    # Checked at startup, and installed without asking.
    write_setting('updates/lastCheckTime', '0')
    write_setting('updates/autoCheck', 'true')
    write_setting('updates/autoInstall', 'true')
    keyboard = None
    if options.keywatch_helper:
        setup = install_old_keywatch_helper(INSTALLED, work)
        if not check(evidence, 'old.helper_installed', setup.returncode == 0, (setup.stdout + setup.stderr).strip()):
            return finish(evidence)
        write_setting('shortcuts/toggleDictation', 'key:F13')
        keyboard = Keyboard()
    DATA.mkdir(parents=True, exist_ok=True)
    (DATA / 'sentinel.txt').write_text('kept\n')
    before = read_settings()
    save_redacted(SETTINGS, evidence / 'settings-before.conf')
    desktop_before = DESKTOP.read_text()
    helper_before = sha256(HELPER) if keyboard else None
    old_sha = sha256(INSTALLED)
    check(evidence, 'old.installed', old_sha != new_sha, f'{INSTALLED} sha256 {old_sha}')

    credentials = Credentials(keys)
    serve_https(work, evidence, options.new.resolve(), version, build, credentials)
    proxy = re.match(r'http://(127\.0\.0\.1|localhost):(\d+)', before.get('cliproxy/baseUrl', ''))
    if 'proxy' in keys and proxy:
        serve_proxy(int(proxy[2]), credentials)
    else:
        keys.pop('proxy', None)
    old_pid = launch(INSTALLED, old=True)
    check(evidence, 'old.running', True, f'pid {old_pid} {os.readlink(f"/proc/{old_pid}/exe")}')
    if not options.keep_state:
        try:
            wait_for(lambda: 'openai/apiKey' not in read_settings(), 30, 'the OpenAI key stayed in the settings file')
            check(evidence, 'old.saved_key_in_keyring', True, 'the old app moved the OpenAI key out of its file')
        except TimeoutError as error:
            check(evidence, 'old.saved_key_in_keyring', False, str(error))
    shortcuts_before = None
    if keyboard:
        wait_for(lambda: helper_watching(old_pid), 30, 'the helper never watched F13 for the old app')
        untapped = cli('status').stdout.strip()
        keyboard.tap(KEY_F13)
        # Any change shows the key reached the old app, whose fixture
        # microphone may not exist here.
        try:
            wait_for(lambda: cli('status').stdout.strip() != untapped, 20, 'F13 did not reach the old app')
            check(evidence, 'old.helper_shortcut', True, f'{untapped!r} -> {cli("status").stdout.strip()!r}')
        except TimeoutError as error:
            check(evidence, 'old.helper_shortcut', False, f'{error}: {cli("status").stdout.strip()}')
        cli('stop')
    else:
        shortcuts_before = wait_for(lambda: 'toggle' in shortcut_infos() and shortcut_infos(), 30,
                                    'old app registered no global shortcut')
        (evidence / 'shortcuts-before.txt').write_text(shortcuts_before + '\n')

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
        wait_for(lambda: old_pid not in daemons(), 30, 'the old process stayed')
        wait_for(lambda: 'state' in status(), 60, 'the new app did not answer')
    except TimeoutError as error:
        check(evidence, 'relaunched', False, str(error))
        return finish(evidence)
    environ = dict(kv.split('=', 1) for kv in Path(f'/proc/{new_pid}/environ').read_text().split('\0') if '=' in kv)
    check(evidence, 'relaunched', environ.get('APPIMAGE') == str(INSTALLED),
          f'pid {old_pid} -> {new_pid} {os.readlink(f"/proc/{new_pid}/exe")} from {environ.get("APPIMAGE")}')
    check(evidence, 'one_daemon', daemons() == [new_pid], f'{daemons()}')
    check(evidence, 'version', version in cli('--version').stdout, cli('--version').stdout.strip())
    state = status()
    check(evidence, 'status', state.get('state') == 'idle', json.dumps(state))

    written = {}
    expected_state(evidence, 'first_launch', before, written)
    check(evidence, 'sentinel_kept', (DATA / 'sentinel.txt').read_text() == 'kept\n')
    check(evidence, 'desktop_entry_kept', DESKTOP.read_text() == desktop_before
          and f'Exec="{INSTALLED}"' in DESKTOP.read_text(), str(DESKTOP))
    check(evidence, 'command_link_kept', LINK.is_symlink() and LINK.resolve() == INSTALLED.resolve())
    check(evidence, 'icon_kept', ICON.is_file())
    if shortcuts_before:
        shortcuts_after = shortcut_infos()
        (evidence / 'shortcuts-after.txt').write_text(shortcuts_after + '\n')
        bound = lambda infos: re.findall(r"\[(\d+)\], \[(\d+)\]", infos)
        check(evidence, 'shortcut_kept', bound(shortcuts_after) == bound(shortcuts_before) and bound(shortcuts_after),
              shortcuts_after)
        active = run(*KGLOBALACCEL, '--method', 'org.kde.kglobalaccel.Component.isActive', check_status=False).stdout
        check(evidence, 'shortcut_active', 'true' in active, active.strip())

    # The keys, read by the new binary on its own. Transcribing a file needs
    # a speech server, which a stub is.
    quit_speecher()
    audio = work / 'tone.wav'
    write_tone(audio)
    written.update({'endpoint/speech/baseUrl': f'http://127.0.0.1:{serve_stub_transcriber()}/v1',
                    'endpoint/speech/model': 'stub'})
    # Each key through the sign-in that uses it, chosen only for a key this
    # run proves.
    if 'openai' in keys:
        written['openai/auth/mode'] = 'settings'
    if 'proxy' in keys:
        written['anthropic/auth/mode'] = 'cliproxy'
    for key, value in written.items():
        write_setting(key, value)
    prove_keys(evidence, 'after_update', credentials, audio)

    # One edit through the running app, then a restart from the menu entry.
    # The CLI's vocabulary add saves only the vocabulary; the whole snapshot a
    # Quick UI edit saves is tests/test_migration.cpp's to check.
    launch(INSTALLED)
    cli('vocabulary', 'add', 'migration e2e term')
    quit_speecher()
    save_redacted(SETTINGS, evidence / 'settings-after-edit.conf')
    launch(INSTALLED)
    values = expected_state(evidence, 'after_edit_and_restart', before, written)
    terms = [entry['term'] for entry in update_expectations.vocabulary(values)]
    check(evidence, 'edit_saved', 'migration e2e term' in terms)
    quit_speecher()
    prove_keys(evidence, 'after_edit_and_restart', credentials, audio)

    # The global shortcut toggles dictation, and dictation reaches the stub
    # speech service.
    for key, value in (('stt/provider', 'endpoint'), ('refinement/provider', 'none'),
                       ('audio/vadEnabled', 'false'), ('audio/captureMode', 'on_demand')):
        write_setting(key, value)
    pid = launch(INSTALLED)
    if keyboard:
        check(evidence, 'helper_untouched', sha256(HELPER) == helper_before, f'{HELPER} sha256 {sha256(HELPER)}')
        try:
            wait_for(lambda: helper_watching(pid), 30, 'the helper never watched F13 for the new app')
            check(evidence, 'helper_watches_for_new_app', True)
        except TimeoutError as error:
            check(evidence, 'helper_watches_for_new_app', False, str(error))
        (evidence / 'keywatchd.log').write_text(helper_journal())
        keyboard.tap(KEY_F13)
    else:
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
    if SETTINGS.exists():
        save_redacted(SETTINGS, evidence / 'final-speecher.conf')
    log = HOME / '.cache/io.github.firemonster612/speecher/speecher.log'
    if log.exists():
        shutil.copyfile(log, evidence / 'final-speecher.log')
    print(f'{len(failed)} failed: {failed}' if failed else 'all passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
