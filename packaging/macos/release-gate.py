#!/usr/bin/env python3
"""The release gate in docs/macos.md, unattended, on a disposable Mac such as a
GitHub Actions macOS runner (.github/workflows/mac-release-gate.yml):

  SPEECHER_DISPOSABLE_MACHINE=1 packaging/macos/release-gate.py \\
      SHIPPED.dmg CANDIDATE.dmg CANDIDATE-SIGNATURE.txt EVIDENCE_DIR

It compares the two bundles' signatures (the gate's step 2), installs the
shipped app and gives it what a user would (settings, the TCC grants recorded
against the shipped app's designated requirement, its login item, an OpenAI
key it saves in the Keychain itself), updates it to the candidate with
verify-dmg-update.py, pressing Install and restart through the UI, and then
checks what the new app kept. Each check is a PASS or FAIL line in
EVIDENCE_DIR/summary.txt; what a machine without a person can't check is a
NOT_VERIFIED line, never a pass. The exit status is 1 if any check failed.
"""

import argparse
import contextlib
import importlib.util
import json
import os
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location('verify_dmg_update', HERE / 'verify-dmg-update.py')
update = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(update)

BUNDLE_ID = 'io.github.firemonster612.speecher'
LOG = Path.home() / 'Library/Caches/io.github.firemonster612/speecher/speecher.log'
USER_TCC = Path.home() / 'Library/Application Support/com.apple.TCC/TCC.db'
SYSTEM_TCC = Path('/Library/Application Support/com.apple.TCC/TCC.db')
# Each grant a user may have given the shipped app, in the database macOS keeps
# it in. Automation is recorded per target app: Music, for pausing media.
GRANTS = {
    'kTCCServiceMicrophone': USER_TCC,
    'kTCCServiceAppleEvents': USER_TCC,
    'kTCCServiceAccessibility': SYSTEM_TCC,
    'kTCCServiceListenEvent': SYSTEM_TCC,
    'kTCCServiceScreenCapture': SYSTEM_TCC,
}
AUTOMATION_TARGET = 'com.apple.Music'
DUMMY_OPENAI_KEY = 'sk-release-gate-dummy-0000000000000000'
# QtKeychain's generic password for the OpenAI key (src/core/SecretStore.cpp).
KEYCHAIN_SERVICE, KEYCHAIN_ACCOUNT = 'speecher', 'openai-api-key'
# Long enough for a Keychain read the app times out itself; a prompt nobody
# answers would hold it far longer.
ANSWER_TIMEOUT = 60
NOT_COVERED = {
    'microphone_audio': 'the runner has no microphone; only the TCC record is checked',
    'shortcut_and_paste': 'dictating with the shortcut into TextEdit needs a person and audio',
    'music_pause_without_prompt': 'needs a dictation while Music plays; only the Automation record is checked',
    'screenshot_refinement_without_prompt': 'needs a refined dictation; only the Screen Recording record is checked',
    'claude_and_codex_sign_in': 'needs real Claude Code and Codex logins in the Keychain',
    'login_starts_speecher': 'needs a log out and back in',
    'launchservices_start_takes_input': 'needs a person to click and type in a window LaunchServices opened',
    'accessibility_off_before_update': 'step 7 needs a second update with the grant removed',
}


class Summary:
    def __init__(self, path):
        self.path = path
        self.failed = False
        path.write_text('')

    def record(self, name, verdict, detail=''):
        line = f'{name}={verdict} {detail}'.rstrip()
        print(line, flush=True)
        with open(self.path, 'a') as summary:
            summary.write(line + '\n')
        self.failed |= verdict == 'FAIL'

    def check(self, name, ok, detail=''):
        self.record(name, 'PASS' if ok else 'FAIL', detail)
        return ok


def run(*args):
    """A command's result, whatever its exit status."""
    return update.run(*args, check_status=False)


def ui(evidence, *args):
    """release-gate-ui.js with args, logged to ui.log; whether it matched."""
    result = run('osascript', '-l', 'JavaScript', HERE / 'release-gate-ui.js', *args)
    with open(evidence / 'ui.log', 'a') as log:
        log.write(f'{time.strftime("%H:%M:%S")} {args[:2]} -> exit {result.returncode}\n'
                  f'{result.stdout}{result.stderr}\n')
    return result.returncode == 0


def screenshot(evidence, label):
    run('screencapture', '-x', evidence / f'screen-{label}.png')


@contextlib.contextmanager
def mounted(dmg):
    with tempfile.TemporaryDirectory() as mount:
        update.run('hdiutil', 'attach', dmg, '-readonly', '-nobrowse', '-mountpoint', mount)
        try:
            yield Path(mount) / 'speecher.app'
        finally:
            run('hdiutil', 'detach', mount, '-quiet')


def accessibility_lines(cdhash):
    """trusted= values the app logged at its starts with this cdhash."""
    text = LOG.read_text(errors='replace') if LOG.exists() else ''
    return re.findall(rf'macOS accessibility identity .* trusted=(\d) .*cdhash={cdhash}\b', text)


# --- Step 2: the two bundles' signatures --------------------------------------

def signature(app, evidence, label):
    """codesign's fields and designated requirement for app, kept as evidence."""
    details = run('codesign', '-dv', '--verbose=4', app).stderr
    requirement = run('codesign', '-d', '-r-', app)
    designated = re.search(r'designated => (.*)', requirement.stdout + requirement.stderr)
    (evidence / f'{label}-codesign.txt').write_text(details)
    (evidence / f'{label}-requirement.txt').write_text(requirement.stdout + requirement.stderr)
    fields = dict(line.split('=', 1) for line in details.splitlines() if '=' in line)
    fields['requirement'] = designated[1] if designated else ''
    fields['adhoc'] = 'adhoc' in details
    return fields


def compare_bundles(summary, evidence, shipped_dmg, candidate_dmg):
    with mounted(shipped_dmg) as shipped_app, mounted(candidate_dmg) as candidate_app:
        shipped = signature(shipped_app, evidence, 'shipped')
        candidate = signature(candidate_app, evidence, 'candidate')
        satisfied = run('codesign', '--verify', '-R=' + shipped['requirement'], candidate_app)
        shipped_info = plistlib.loads((shipped_app / 'Contents/Info.plist').read_bytes())
        candidate_info = plistlib.loads((candidate_app / 'Contents/Info.plist').read_bytes())
    (evidence / 'requirement-check.txt').write_text(
        f'codesign --verify -R="{shipped["requirement"]}" candidate/speecher.app\n'
        f'exit {satisfied.returncode}\n{satisfied.stdout}{satisfied.stderr}')

    summary.check('signing.identifier', shipped.get('Identifier') == candidate.get('Identifier') == BUNDLE_ID,
                  f'shipped {shipped.get("Identifier")}, candidate {candidate.get("Identifier")}')
    summary.check('signing.team', shipped.get('TeamIdentifier') == candidate.get('TeamIdentifier'),
                  f'shipped {shipped.get("TeamIdentifier")}, candidate {candidate.get("TeamIdentifier")}')
    summary.check('signing.not_adhoc', not shipped['adhoc'] and not candidate['adhoc'],
                  f'shipped adhoc={shipped["adhoc"]}, candidate adhoc={candidate["adhoc"]}')
    summary.check('signing.shipped_requirement', bool(shipped['requirement']) and satisfied.returncode == 0,
                  f'candidate satisfies: {shipped["requirement"] or "no requirement"}')
    for key in ('SUPublicEDKey', 'CFBundleIdentifier', 'CFBundleExecutable'):
        summary.check(f'plist.{key}', shipped_info.get(key) == candidate_info.get(key),
                      f'shipped {shipped_info.get(key)}, candidate {candidate_info.get(key)}')
    summary.check('plist.CFBundleVersion',
                  int(candidate_info['CFBundleVersion']) > int(shipped_info['CFBundleVersion']),
                  f'shipped {shipped_info["CFBundleVersion"]}, candidate {candidate_info["CFBundleVersion"]}')
    return shipped, candidate


# --- Step 3: the shipped app as a user set it up -------------------------------

def sql_text(value):
    return "'" + value.replace("'", "''") + "'"


def tcc_rows(database):
    query = f'select service, auth_value, hex(csreq) from access where client = {sql_text(BUNDLE_ID)}'
    rows = run('sudo', 'sqlite3', '-separator', '|', database, query).stdout.splitlines()
    return {service: (int(value), csreq) for service, value, csreq in (row.split('|') for row in rows)}


def grant(evidence, requirement):
    """Records each grant against the shipped app's designated requirement, as
    tccd does when a user allows it; returns the compiled requirement in hex."""
    compiled = evidence / 'shipped-requirement.csreq'
    update.run('csreq', '-r=' + requirement, '-b', compiled)
    csreq = compiled.read_bytes().hex().upper()
    for service, database in GRANTS.items():
        target = AUTOMATION_TARGET if service == 'kTCCServiceAppleEvents' else 'UNUSED'
        update.run('sudo', 'sqlite3', database,
                   'insert or replace into access (service, client, client_type, auth_value, auth_reason, '
                   'auth_version, csreq, indirect_object_identifier_type, indirect_object_identifier, flags, '
                   f'last_modified) values ({sql_text(service)}, {sql_text(BUNDLE_ID)}, 0, 2, 3, 1, '
                   f"X'{csreq}', 0, {sql_text(target)}, 0, {int(time.time())})")
    # tccd caches decisions; restart both the system and the user instance.
    run('sudo', 'killall', 'tccd')
    return csreq


def set_up_shipped_app(summary, evidence, shipped_dmg, shipped):
    run('pkill', '-x', 'speecher')
    for domain in (update.APP_DOMAIN, update.SPARKLE_DOMAIN):
        run('defaults', 'delete', domain)
    shutil.rmtree(update.APP, ignore_errors=True)
    with mounted(shipped_dmg) as app:
        update.run('ditto', app, update.APP)
    work = Path(tempfile.mkdtemp(prefix='speecher-release-gate-'))
    # verify-dmg-update.py's settings: master-0.2.0.conf, with Launch at login
    # on, the OpenAI key read from Speecher's own store, and updates checked
    # every 5 minutes.
    update.seed_settings(work)
    update.run('defaults', 'write', update.SPARKLE_DOMAIN, 'SUEnableAutomaticChecks', '-bool', 'true')
    csreq = grant(evidence, shipped['requirement'])
    (evidence / 'tcc-before.json').write_text(json.dumps({str(db): tcc_rows(db) for db in set(GRANTS.values())}))

    update.run('open', '-a', update.APP)
    try:
        update.wait_for(lambda: update.cli('status').returncode == 0, 60, 'the shipped app did not answer')
        trusted = update.wait_for(lambda: accessibility_lines(shipped['CDHash']), 30, 'no accessibility line')
    except TimeoutError as error:
        trusted = []
        summary.record('setup.shipped_app_running', 'FAIL', str(error))
    summary.check('setup.shipped_accessibility', trusted and set(trusted) == {'1'},
                  f'speecher.log trusted={trusted} for the shipped cdhash {shipped["CDHash"]}')

    # Typed into the old app's own field, so the old app writes the Keychain
    # item with its own access list, as it did for its users.
    update.cli('settings')
    time.sleep(3)
    saved = ui(evidence, 'select', 'Accounts')
    time.sleep(2)
    saved = saved and ui(evidence, 'type', 'Enter OpenAI API key', DUMMY_OPENAI_KEY)
    time.sleep(3)
    # Attributes only: without -w, reading them asks for no permission.
    item = run('security', 'find-generic-password', '-s', KEYCHAIN_SERVICE, '-a', KEYCHAIN_ACCOUNT)
    (evidence / 'keychain-item-before.txt').write_text(item.stdout + item.stderr)
    if not saved:
        ui(evidence, 'dump')
    summary.check('setup.openai_key_saved_by_shipped_app', saved and item.returncode == 0,
                  'Accounts > OpenAI API key typed; Keychain item speecher/openai-api-key '
                  + ('present' if item.returncode == 0 else 'missing'))
    screenshot(evidence, 'shipped-setup')

    login = update.login_item()
    (evidence / 'login-item-setup.txt').write_text(login + '\n')
    summary.check('setup.login_item', update.login_item_enabled(login), 'see login-item-setup.txt')
    update.cli('quit')
    return csreq


# --- Step 4: the update, with Install and restart pressed ----------------------

def run_update(summary, evidence, shipped_dmg, candidate_dmg, signature_file):
    out = evidence / 'update'
    requests = out / 'https-requests.log'
    with open(evidence / 'update-output.txt', 'w') as output:
        script = subprocess.Popen(
            [sys.executable, HERE / 'verify-dmg-update.py', shipped_dmg, candidate_dmg, signature_file, out,
             '--keep-state'],
            stdout=output, stderr=subprocess.STDOUT, env=dict(os.environ, SPEECHER_DISPOSABLE_MACHINE='1'))
        # When each button was last pressed; a press that a minute later still
        # shows nothing served has missed, so it is pressed again.
        pressed = {'check': None, 'install': None}
        dumped = False
        while script.poll() is None:
            time.sleep(10)
            started = (out / 'summary.txt').exists() and 'old.running=' in (out / 'summary.txt').read_text()
            served = requests.read_text() if requests.exists() else ''
            if not started or 'GET /speecher.dmg' in served:
                continue
            button = 'install' if 'GET /appcast-stable.xml' in served else 'check'
            if pressed[button] is not None and time.monotonic() - pressed[button] < 60:
                continue
            # Before the old app listens, 0.2.0's `speecher settings` starts an
            # app of its own in the foreground instead of asking it.
            if update.cli('status').returncode != 0:
                continue
            update.cli('settings')
            time.sleep(3)
            if button == 'check':
                # 0.2.0's own timer checks only when App Nap lets it; a person
                # presses Check for updates (General > Updates) instead.
                ui(evidence, 'select', 'General')
                time.sleep(2)
                if ui(evidence, 'press', 'Check for updates'):
                    pressed['check'] = time.monotonic()
                continue
            screenshot(evidence, 'offer')
            # 0.2.0's banner buttons have no accessible title; Install and
            # restart is the first after the offer (UpdateBannerContent).
            if ui(evidence, 'press-after', ' is available'):
                pressed['install'] = time.monotonic()
            elif not dumped:
                dumped = ui(evidence, 'dump')
        status = script.wait()
    for line in (out / 'summary.txt').read_text().splitlines() if (out / 'summary.txt').exists() else []:
        name, _, rest = line.partition('=')
        verdict, _, detail = rest.partition(' ')
        summary.record(f'update.{name}', verdict, detail)
    summary.check('update.script', status == 0, f'verify-dmg-update.py exit {status}; see update-output.txt')
    screenshot(evidence, 'after-update')


# --- Step 5: what the new app kept --------------------------------------------

def check_after_update(summary, evidence, candidate, csreq):
    trusted = accessibility_lines(candidate['CDHash'])
    summary.check('after.accessibility_trusted', trusted and set(trusted) == {'1'},
                  f'speecher.log trusted={trusted} for the candidate cdhash {candidate["CDHash"]}')

    with tempfile.NamedTemporaryFile(suffix='.csreq') as compiled:
        compiled.write(bytes.fromhex(csreq))
        compiled.flush()
        requirement = run('csreq', '-r', compiled.name, '-t').stdout.strip()
    satisfied = run('codesign', '--verify', '-R=' + requirement, update.APP).returncode == 0
    rows = {service: tcc_rows(database).get(service) for service, database in GRANTS.items()}
    (evidence / 'tcc-after.json').write_text(json.dumps(rows))
    for service, row in rows.items():
        summary.check(f'after.tcc.{service}', row == (2, csreq) and satisfied,
                      f'record {"allowed" if row and row[0] == 2 else row}, '
                      f'{"unchanged" if row and row[1] == csreq else "changed"}; installed app '
                      f'{"satisfies" if satisfied else "does not satisfy"} its requirement')

    if not update.speecher_pids():
        update.run('open', '-a', update.APP)
        with contextlib.suppress(TimeoutError):
            update.wait_for(update.speecher_pids, 60, 'the new app did not start')
    # Through the running app, which reads the key the old app saved. A
    # Keychain prompt nobody answers shows up as no answer.
    try:
        answer = subprocess.run([str(update.EXECUTABLE), 'providers', '--json'], capture_output=True,
                                text=True, timeout=ANSWER_TIMEOUT).stdout.strip()
    except subprocess.TimeoutExpired:
        answer = None
    prompt = run('osascript', '-e', 'tell application "System Events" to if exists process "SecurityAgent" '
                 'then return count windows of process "SecurityAgent"').stdout.strip()
    screenshot(evidence, 'key-check')
    (evidence / 'providers-after.json').write_text(f'{answer}\nSecurityAgent windows: {prompt}\n')
    openai = next((entry for entry in json.loads(answer or '[]')
                   if entry['id'] == 'openai' and entry['role'] == 'refinement'), None)
    no_prompt = prompt in ('', '0')
    summary.check('after.openai_key_without_prompt', answer is not None and bool(openai)
                  and openai['signedIn'] is True and no_prompt,
                  'no answer in 60 s: a Keychain prompt is likely waiting' if answer is None
                  else f'openai {openai}; SecurityAgent windows {prompt or 0}')

    login = update.login_item()
    (evidence / 'login-item-after-gate.txt').write_text(login + '\n')
    apps = [block for block in re.split(r'\n\s*\n', login) if re.search(r'Type:\s*app', block)]
    summary.check('after.login_item_once', len(apps) == 1 and update.login_item_enabled(login),
                  f'{len(apps)} app entries; see login-item-after-gate.txt')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('shipped', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('signature', type=Path)
    parser.add_argument('evidence', type=Path)
    options = parser.parse_args()
    if os.environ.get('SPEECHER_DISPOSABLE_MACHINE') != '1':
        sys.exit('This replaces Speecher, its settings and TCC records and trusts a throwaway CA; '
                 'set SPEECHER_DISPOSABLE_MACHINE=1 on a throwaway Mac.')
    evidence = options.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    summary = Summary(evidence / 'summary.txt')
    shipped_dmg, candidate_dmg = options.shipped.resolve(), options.candidate.resolve()

    shipped, candidate = compare_bundles(summary, evidence, shipped_dmg, candidate_dmg)
    csreq = set_up_shipped_app(summary, evidence, shipped_dmg, shipped)
    run_update(summary, evidence, shipped_dmg, candidate_dmg, options.signature.resolve())
    check_after_update(summary, evidence, candidate, csreq)
    for name, reason in NOT_COVERED.items():
        summary.record(f'manual.{name}', 'NOT_VERIFIED', reason)
    if LOG.exists():
        shutil.copyfile(LOG, evidence / 'speecher.log')
    summary.record('gate', 'FAIL' if summary.failed else 'PASS')
    return 1 if summary.failed else 0


if __name__ == '__main__':
    sys.exit(main())
