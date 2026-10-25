#!/usr/bin/env python3
"""Updates an installed Speecher from an older DMG to a new one through the
old app's own Sparkle updater, on a disposable Mac, and checks what an
installed user keeps.

  SPEECHER_DISPOSABLE_MACHINE=1 packaging/macos/verify-dmg-update.py \\
      OLD.dmg NEW.dmg NEW-SIGNATURE.txt EVIDENCE_DIR [--keep-state]

NEW-SIGNATURE.txt is sign_update's output for NEW.dmg, or the
sparkle-signature.txt release.yml uploads beside it, made with the private
key whose public half OLD's Info.plist carries as SUPublicEDKey: the release
key for a shipped build. The script installs OLD at /Applications/speecher.app,
replaces both Speecher defaults domains with tests/migration/master-0.2.0.conf
(Launch at login on, updates checked every 5 minutes), and serves an
appcast offering NEW from a local HTTPS server whose throwaway CA it adds to
the System keychain. The old app finds it through SPEECHER_APPCAST_URL, which
releases since 0.2.0 read, at its next scheduled check. With --keep-state the
installed app, its settings, grants and keychain items stay as a person set
them up (the release gate in docs/macos.md); its Check for updates brings it
sooner.

The old app then shows "Install and restart" in its window, and a person or a
computer-use driver presses it within ten minutes: 0.2.0 turns Sparkle's own
scheduler off and runs its checks itself, so Sparkle never downloads in the
background, whatever "Download and install updates automatically" says, and
there is no install on quit to wait for. Sparkle then downloads NEW, checks
its signature, installs it and opens the new app. The new app's settings are
compared with the old ones as the update should leave them
(packaging/update_expectations.py). Each check is a PASS or FAIL line in
EVIDENCE_DIR/summary.txt; the exit status is 1 if any failed. Old and new
signatures and designated requirements go to the evidence; with ad-hoc
fixtures they show what the release gate must compare for real
(docs/macos.md).
"""

import argparse
import atexit
import http.server
import json
import os
import plistlib
import re
import shutil
import signal
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO / 'packaging'))
import update_expectations  # noqa: E402

APP = Path('/Applications/speecher.app')
EXECUTABLE = APP / 'Contents/MacOS/speecher'
APP_DOMAIN = 'com.io-github-firemonster612.speecher'
SPARKLE_DOMAIN = 'io.github.firemonster612.speecher'
SUPPORT = Path.home() / 'Library/Application Support/io.github.firemonster612/speecher'

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
    return subprocess.run([str(arg) for arg in args], check=check_status, capture_output=True,
                          text=True, timeout=180, **kwargs)


def wait_for(predicate, timeout, what):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(1)
    raise TimeoutError(what)


def bundle_version():
    return plistlib.loads((APP / 'Contents/Info.plist').read_bytes())


def speecher_pids():
    result = run('pgrep', '-f', f'^{EXECUTABLE}', check_status=False)
    return [int(pid) for pid in result.stdout.split()]


def cli(*args):
    return run(EXECUTABLE, *args, check_status=False)


def record_signature(evidence, label):
    details = run('codesign', '-dv', '--verbose=4', APP, check_status=False).stderr
    requirement = run('codesign', '-d', '-r-', APP, check_status=False)
    (evidence / f'{label}-codesign.txt').write_text(details + '\n' + requirement.stdout + requirement.stderr)
    return re.search(r'designated => .*', requirement.stdout + requirement.stderr)


# --- Settings: QSettings' INI fixture in, the defaults domain out ------------

def ini_unescape(text):
    """A value as QSettings' INI reader unescapes it: C escapes, \\x hex and
    octal runs."""
    simple = {'a': '\a', 'b': '\b', 'f': '\f', 'n': '\n', 'r': '\r', 't': '\t', 'v': '\v'}

    def one(match):
        escape = match[1]
        if escape[0] == 'x':
            return chr(int(escape[1:], 16))
        if escape[0] in '01234567':
            return chr(int(escape, 8))
        return simple.get(escape, escape)

    return re.sub(r'\\(x[0-9a-fA-F]+|[0-7]+|.)', one, text)


def ini_value(raw):
    """One INI value as QSettings hands it on: a string, a list for unquoted
    commas, bytes for @ByteArray(...)."""
    quoted = raw.startswith('"') and raw.endswith('"')
    if not quoted and ', ' in raw:
        return [ini_unescape(part) for part in raw.split(', ')]
    value = ini_unescape(raw[1:-1] if quoted else raw)
    if value.startswith('@ByteArray(') and value.endswith(')'):
        return value[len('@ByteArray('):-1].encode('latin-1')
    return value


def seed_settings(work):
    """master-0.2.0.conf into the app's defaults domain, typed as QSettings
    stores what it reads from that INI file on macOS (groups joined by dots,
    strings, lists, byte arrays as data), with updates installed in the
    background and checked every 5 minutes."""
    settings, group = {}, ''
    for line in (REPO / 'tests/migration/master-0.2.0.conf').read_text().splitlines():
        if line.startswith('[') and line.endswith(']'):
            group = '' if line == '[General]' else line[1:-1] + '.'
        elif '=' in line:
            key, raw = line.split('=', 1)
            settings[group + key.replace('\\', '.')] = ini_value(raw)
    settings['updates.autoInstall'] = True
    # The old app first checks one interval after it starts; 5 is the least.
    settings['updates.checkIntervalMinutes'] = 5
    plist = work / 'seed.plist'
    plist.write_bytes(plistlib.dumps(settings, fmt=plistlib.FMT_BINARY))
    run('defaults', 'import', APP_DOMAIN, plist)


def read_settings():
    """The app's defaults domain as group/key strings, the way the update
    expectations read settings."""
    domain = plistlib.loads(run('defaults', 'export', APP_DOMAIN, '-').stdout.encode())

    def text(value):
        if isinstance(value, bool):
            return 'true' if value else 'false'
        if isinstance(value, bytes):
            return value.decode('latin-1')
        if isinstance(value, list):
            return ', '.join(map(text, value))
        return str(value)

    return {key.replace('.', '/'): text(value) for key, value in domain.items()}


# --- The feed ----------------------------------------------------------------

def serve_appcast(work, evidence, new_dmg, signature):
    certs = work / 'certs'
    certs.mkdir()
    run('openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2',
        '-keyout', certs / 'ca-key.pem', '-out', certs / 'ca.pem', '-subj', '/CN=Speecher update E2E CA')
    run('openssl', 'req', '-newkey', 'rsa:2048', '-nodes', '-keyout', certs / 'key.pem',
        '-out', certs / 'server.csr', '-subj', '/CN=localhost')
    (certs / 'server.ext').write_text('subjectAltName=DNS:localhost,IP:127.0.0.1\nextendedKeyUsage=serverAuth\n')
    run('openssl', 'x509', '-req', '-days', '2', '-sha256', '-in', certs / 'server.csr',
        '-CA', certs / 'ca.pem', '-CAkey', certs / 'ca-key.pem', '-CAcreateserial',
        '-out', certs / 'server.pem', '-extfile', certs / 'server.ext')
    run('sudo', 'security', 'add-trusted-cert', '-d', '-r', 'trustRoot',
        '-k', '/Library/Keychains/System.keychain', certs / 'ca.pem')

    root = work / 'feed'
    root.mkdir()
    shutil.copyfile(new_dmg, root / 'speecher.dmg')
    with tempfile.TemporaryDirectory() as mount:
        run('hdiutil', 'attach', new_dmg, '-readonly', '-nobrowse', '-mountpoint', mount)
        info = plistlib.loads((Path(mount) / 'speecher.app/Contents/Info.plist').read_bytes())
        run('hdiutil', 'detach', mount, '-quiet')
    text = Path(signature).read_text()
    # sign_update's output, or the sparkle-signature.txt release.yml keeps.
    ed_signature = re.search(r'edSignature="?([^"\s]+)"?', text)[1]
    length = re.search(r'length="?(\d+)', text)[1]
    (work / 'signature.txt').write_text(f'sparkle:edSignature={ed_signature}\nlength={length}\n')
    run(sys.executable, REPO / 'scripts/make-appcast.py', '--channel', 'stable',
        '--version', info['CFBundleShortVersionString'], '--build-number', info['CFBundleVersion'],
        '--dmg-url', 'https://localhost:8443/speecher.dmg', '--signature', work / 'signature.txt',
        '--output', root / 'appcast-stable.xml')
    shutil.copyfile(root / 'appcast-stable.xml', evidence / 'appcast-stable.xml')
    log = open(evidence / 'https-requests.log', 'a')

    class Feed(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(root), **kwargs)

        def log_message(self, format, *args):
            log.write(f'{time.strftime("%H:%M:%S")} {format % args}\n')
            log.flush()

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 8443), Feed)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certs / 'server.pem', certs / 'key.pem')
    server.socket = context.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return info


def login_item():
    """What Background Task Management lists for Speecher."""
    dump = run('sudo', 'sfltool', 'dumpbtm', check_status=False).stdout
    entries = [block for block in re.split(r'\n\s*\n', dump) if 'firemonster612' in block or 'speecher.app' in block]
    return '\n\n'.join(entries)


def login_item_enabled(entries):
    # "Disposition: [enabled, allowed, ...]"; a disabled one reads [disabled, ...].
    return bool(re.search(r'Disposition:\s*\[enabled\b', entries))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('old', type=Path)
    parser.add_argument('new', type=Path)
    parser.add_argument('signature', type=Path)
    parser.add_argument('evidence', type=Path)
    parser.add_argument('--keep-state', action='store_true')
    options = parser.parse_args()
    if os.environ.get('SPEECHER_DISPOSABLE_MACHINE') != '1':
        sys.exit('This trusts a throwaway CA and replaces Speecher\'s app and settings; '
                 'set SPEECHER_DISPOSABLE_MACHINE=1 on a throwaway Mac.')
    evidence = options.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / 'summary.txt').write_text('')
    work = Path(tempfile.mkdtemp(prefix='speecher-update-e2e-'))

    run('pkill', '-x', 'speecher', check_status=False)
    if not options.keep_state:
        for domain in (APP_DOMAIN, SPARKLE_DOMAIN):
            run('defaults', 'delete', domain, check_status=False)
        shutil.rmtree(APP, ignore_errors=True)
        with tempfile.TemporaryDirectory() as mount:
            run('hdiutil', 'attach', options.old, '-readonly', '-nobrowse', '-mountpoint', mount)
            run('ditto', Path(mount) / 'speecher.app', APP)
            run('hdiutil', 'detach', mount, '-quiet')
    old = bundle_version()
    old_requirement = record_signature(evidence, 'old')
    check(evidence, 'old.installed', True, f'{old["CFBundleShortVersionString"]} ({old["CFBundleVersion"]}); '
          f'{old_requirement[0] if old_requirement else "no designated requirement"}')
    if not options.keep_state:
        seed_settings(work)
        # A user past Sparkle's first-launch question about checking.
        run('defaults', 'write', SPARKLE_DOMAIN, 'SUEnableAutomaticChecks', '-bool', 'true')
    SUPPORT.mkdir(parents=True, exist_ok=True)
    (SUPPORT / 'sentinel.txt').write_text('kept\n')
    run('defaults', 'export', APP_DOMAIN, evidence / 'defaults-before.plist')
    before = read_settings()

    new = serve_appcast(work, evidence, options.new.resolve(), options.signature)
    feed = 'https://localhost:8443/appcast-stable.xml'
    # LaunchServices relaunches must keep the test feed.
    run('launchctl', 'setenv', 'SPEECHER_APPCAST_URL', feed)
    # Gone when the script ends, passed, failed or stopped, so no later launch
    # checks a feed nobody serves. A terminal closing or CI cancelling the step
    # sends SIGHUP or SIGTERM, which skip atexit unless they exit.
    atexit.register(run, 'launchctl', 'unsetenv', 'SPEECHER_APPCAST_URL', check_status=False)
    for stop in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(stop, lambda *_: sys.exit(1))
    run('open', '-a', APP, '--env', f'SPEECHER_APPCAST_URL={feed}')
    old_pid = wait_for(speecher_pids, 60, 'the old app did not start')[0]
    check(evidence, 'old.running', True, f'pid {old_pid}')
    time.sleep(5)
    (evidence / 'login-item-before.txt').write_text(login_item() + '\n')

    requests = evidence / 'https-requests.log'
    try:
        wait_for(lambda: 'GET /appcast-stable.xml' in requests.read_text(), 900, 'the old app never checked the feed')
        print('Press "Install and restart" in the old Speecher window.', flush=True)
        wait_for(lambda: 'GET /speecher.dmg' in requests.read_text(), 600, 'no download: nobody pressed Install and restart')
        check(evidence, 'old.downloaded', True)
    except TimeoutError as error:
        check(evidence, 'old.downloaded', False, str(error))
        return finish(evidence)
    try:
        new_pid = wait_for(lambda: [pid for pid in speecher_pids() if pid != old_pid], 600, 'no new process')[0]
        wait_for(lambda: '"state"' in cli('status', '--json').stdout, 60, 'the new app did not answer')
    except TimeoutError as error:
        check(evidence, 'relaunched', False, str(error))
        return finish(evidence)
    installed = bundle_version()
    check(evidence, 'installed', installed['CFBundleVersion'] == new['CFBundleVersion'],
          f'{installed["CFBundleShortVersionString"]} ({installed["CFBundleVersion"]}) at {APP}')
    check(evidence, 'relaunched', True, f'pid {old_pid} -> {new_pid}')
    check(evidence, 'one_process', speecher_pids() == [new_pid], f'{speecher_pids()}')
    new_requirement = record_signature(evidence, 'new')
    check(evidence, 'signatures_recorded', bool(new_requirement), new_requirement[0] if new_requirement else '')
    status = cli('status', '--json').stdout.strip()
    check(evidence, 'cli', '"state"' in status, status)
    (evidence / 'defaults-after.plist').write_text(run('defaults', 'export', APP_DOMAIN, '-').stdout)

    def state(label):
        current = read_settings()
        wrong = update_expectations.differences(before, current)
        check(evidence, f'{label}.settings', not wrong,
              json.dumps(wrong) if wrong else f'{len(before)} saved keys as the update should leave them')
        terms = lambda entries: {entry['term']: entry.get('starred', False) for entry in entries}
        old_terms = terms(update_expectations.vocabulary(before))
        new_terms = terms(update_expectations.vocabulary(current))
        check(evidence, f'{label}.vocabulary', old_terms.items() <= new_terms.items(),
              f'{len(new_terms)} entries, {len(old_terms)} before')
        return current

    state('first_launch')
    check(evidence, 'sentinel_kept', (SUPPORT / 'sentinel.txt').read_text() == 'kept\n')
    login = login_item()
    (evidence / 'login-item-after.txt').write_text(login + '\n')
    check(evidence, 'login_item', login_item_enabled(login), 'see login-item-after.txt')

    # One edit through the running app, then a restart as at the next login.
    # The CLI's vocabulary add saves only the vocabulary; the whole snapshot a
    # Quick UI edit saves is tests/test_migration.cpp's to check.
    cli('vocabulary', 'add', 'migration e2e term')
    cli('quit')
    wait_for(lambda: not speecher_pids(), 60, 'the new app did not quit')
    run('open', '-a', APP)
    wait_for(speecher_pids, 60, 'the new app did not reopen')
    wait_for(lambda: '"state"' in cli('status', '--json').stdout, 60, 'the new app did not answer')
    after = state('after_edit_and_restart')
    terms = [entry['term'] for entry in update_expectations.vocabulary(after)]
    check(evidence, 'edit_saved', 'migration e2e term' in terms)
    return finish(evidence)


def finish(evidence):
    log = Path.home() / 'Library/Caches/io.github.firemonster612/speecher/speecher.log'
    if log.exists():
        shutil.copyfile(log, evidence / 'speecher.log')
    print(f'{len(failed)} failed: {failed}' if failed else 'all passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
