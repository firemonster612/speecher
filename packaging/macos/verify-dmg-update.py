#!/usr/bin/env python3
"""Updates an installed Speecher from an older DMG to a new one through the
old app's own Sparkle updater, on a disposable Mac, and checks what an
installed user keeps.

  SPEECHER_DISPOSABLE_MACHINE=1 packaging/macos/verify-dmg-update.py \\
      OLD.dmg NEW.dmg NEW-SIGNATURE.txt EVIDENCE_DIR [--accept quit|restart-button]
      [--keep-state]

NEW-SIGNATURE.txt is sign_update's output for NEW.dmg, or the
sparkle-signature.txt release.yml uploads beside it, made with the private
key whose public half OLD's Info.plist carries as SUPublicEDKey: the release
key for a shipped build. The script installs OLD at /Applications/speecher.app,
replaces both Speecher defaults domains with tests/migration/master-0.2.0.conf
(Launch at login on, updates installed in the background), and serves an
appcast offering NEW from a local HTTPS server whose throwaway CA it adds to
the System keychain. The old app finds it through SPEECHER_APPCAST_URL, which
releases since 0.2.0 read, at its next scheduled check. With --keep-state the
installed app, its settings, grants and keychain items stay as a person set
them up (the release gate in docs/macos.md); its Check now brings the update
sooner.

Sparkle downloads and stages NEW on its own. --accept quit (the default) then
quits the old app, which Sparkle installs on, and opens the app again as the
next login would; --accept restart-button leaves the old window's "Restart
now" to a person or a computer-use driver within five minutes. Each check is
a PASS or FAIL line in EVIDENCE_DIR/summary.txt; the exit status is 1 if any
failed. Old and new signatures and designated requirements go to the
evidence; with ad-hoc fixtures they show what the release gate must compare
for real (docs/macos.md).
"""

import argparse
import http.server
import os
import plistlib
import re
import shutil
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
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


def seed_settings(work):
    """master-0.2.0.conf into the app's defaults domain, as QSettings writes
    it on macOS, by a small Qt program, with updates checked every 5 minutes."""
    source = work / 'seed.cpp'
    source.write_text(r'''
#include <QtCore/QCoreApplication>
#include <QtCore/QSettings>
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QSettings fixture(QString::fromLocal8Bit(argv[1]), QSettings::IniFormat);
    QSettings settings(QStringLiteral("io.github.firemonster612"), QStringLiteral("speecher"));
    settings.clear();
    for (const QString &key : fixture.allKeys()) settings.setValue(key, fixture.value(key));
    settings.setValue(QStringLiteral("updates/autoInstall"), true);
    // The old app first checks one interval after it starts; 5 is the least.
    settings.setValue(QStringLiteral("updates/checkIntervalMinutes"), 5);
    settings.sync();
    return settings.status() == QSettings::NoError ? 0 : 1;
}
''')
    qt = Path(os.environ['QT_ROOT_DIR'])
    run('c++', '-std=c++20', source, f'-F{qt}/lib', '-framework', 'QtCore',
        f'-Wl,-rpath,{qt}/lib', '-o', work / 'seed')
    run(work / 'seed', REPO / 'tests/migration/master-0.2.0.conf')


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
    entries = [block for block in dump.split('\n\n') if 'firemonster612' in block or 'speecher.app' in block]
    return '\n\n'.join(entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('old', type=Path)
    parser.add_argument('new', type=Path)
    parser.add_argument('signature', type=Path)
    parser.add_argument('evidence', type=Path)
    parser.add_argument('--accept', choices=('quit', 'restart-button'), default='quit')
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
        # A user past Sparkle's first-launch question lets it check and
        # download in the background.
        run('defaults', 'write', SPARKLE_DOMAIN, 'SUEnableAutomaticChecks', '-bool', 'true')
    SUPPORT.mkdir(parents=True, exist_ok=True)
    (SUPPORT / 'sentinel.txt').write_text('kept\n')
    run('defaults', 'export', APP_DOMAIN, evidence / 'defaults-before.plist')

    new = serve_appcast(work, evidence, options.new.resolve(), options.signature)
    feed = 'https://localhost:8443/appcast-stable.xml'
    # LaunchServices relaunches must keep the test feed.
    run('launchctl', 'setenv', 'SPEECHER_APPCAST_URL', feed)
    run('open', '-a', APP, '--env', f'SPEECHER_APPCAST_URL={feed}')
    old_pid = wait_for(speecher_pids, 60, 'the old app did not start')[0]
    check(evidence, 'old.running', True, f'pid {old_pid}')
    time.sleep(5)
    (evidence / 'login-item-before.txt').write_text(login_item() + '\n')

    try:
        wait_for(lambda: 'GET /speecher.dmg' in (evidence / 'https-requests.log').read_text(), 600,
                 'Sparkle did not download the offered DMG')
        check(evidence, 'old.downloaded', True)
    except TimeoutError as error:
        check(evidence, 'old.downloaded', False, str(error))
        return finish(evidence)
    # Sparkle validates and stages it before asking to relaunch.
    time.sleep(20)
    if options.accept == 'quit':
        cli('quit')
        wait_for(lambda: old_pid not in speecher_pids(), 60, 'the old app did not quit')
        wait_for(lambda: bundle_version()['CFBundleVersion'] == new['CFBundleVersion'], 180,
                 'Sparkle did not install on quit')
        run('open', '-a', APP, '--env', f'SPEECHER_APPCAST_URL={feed}')
    try:
        new_pid = wait_for(lambda: [pid for pid in speecher_pids() if pid != old_pid], 300, 'no new process')[0]
        time.sleep(10)
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
        current = plistlib.loads(run('defaults', 'export', APP_DOMAIN, '-').stdout.encode())
        expected = {
            'app.setupCompleted': True, 'app.launchAtLogin': True, 'shortcuts.activationMode': 'toggle',
            'refinement.provider': 'anthropic', 'openai.model': 'gpt-6-luna',
            'anthropic.model': 'claude-opus-5-5', 'output.method': 'automatic', 'updates.channel': 'stable',
        }
        normalize = lambda value: {'true': True, 'false': False}.get(value, value) if isinstance(value, str) else value
        wrong = {key: current.get(key) for key, want in expected.items() if normalize(current.get(key)) != want}
        check(evidence, f'{label}.settings', not wrong, str(wrong) if wrong else f'{len(expected)} keys as expected')
        check(evidence, f'{label}.old_window_keys_untouched', 'ui.appWindow.a.geometry' in current)
        return current

    state('first_launch')
    check(evidence, 'sentinel_kept', (SUPPORT / 'sentinel.txt').read_text() == 'kept\n')
    login = login_item()
    (evidence / 'login-item-after.txt').write_text(login + '\n')
    check(evidence, 'login_item', 'speecher' in login.lower() and 'enabled' in login.lower(), 'see login-item-after.txt')

    cli('vocabulary', 'add', 'migration e2e term')
    cli('quit')
    wait_for(lambda: not speecher_pids(), 60, 'the new app did not quit')
    run('open', '-a', APP)
    wait_for(speecher_pids, 60, 'the new app did not reopen')
    time.sleep(10)
    after = state('after_edit_and_restart')
    vocabulary = after.get('stt.vocabularyEntries', b'')
    check(evidence, 'edit_saved', b'migration e2e term' in (vocabulary if isinstance(vocabulary, bytes) else vocabulary.encode()))
    return finish(evidence)


def finish(evidence):
    log = Path.home() / 'Library/Caches/io.github.firemonster612/speecher/speecher.log'
    if log.exists():
        shutil.copyfile(log, evidence / 'speecher.log')
    print(f'{len(failed)} failed: {failed}' if failed else 'all passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
