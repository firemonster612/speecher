"""What an installed user's settings should read after an update to the Qt
Quick build: the settings saved before it, changed only where the new app
changes them on purpose (migrateSavedSettings in src/core/SettingsStore.cpp,
and secrets moving to the keyring). The update scripts compare against this,
so a person's own setup works as well as the test fixture.

Settings are flat dicts of QSettings keys (group/key) to strings, as the
scripts read them; JSON values compare as JSON.
"""

import json
import re
import sys

# Values the new app moves or rewrites on purpose, or that change by running:
# secrets go to the keyring, update bookkeeping and migration versions move on.
CHANGED_BY_RUNNING = re.compile(
    r'^(.*/apiKey|secrets/.*|migration/.*|updates/(lastCheckTime|lastRunVersion|lastRunBuildNumber))$')
# Held to their own checks: the edit each script makes adds a term.
CHECKED_APART = {'stt/vocabularyEntries', 'stt/customVocabulary'}
# The old front end's window layout, which the old app saves as it runs up to
# the update and the new app never reads: only kept, not compared.
OLD_WINDOW = re.compile(r'^ui/appWindow/')

REPLACED_MODELS = [
    # (key, old, new, before refinementModelVersion)
    ('openai/model', 'gpt-5.6-luna', 'gpt-6-luna', 1),
    ('anthropic/model', 'claude-sonnet-5', 'claude-opus-5-5', 1),
    ('anthropic/model', 'claude-sonnet-5', 'claude-sonnet-5-5', 2),
]
# The stored output methods that never used a delivery step
# (migrateDeliverySteps).
KEYBOARD_ONLY = {'ydotool', 'mac-paste', 'win-paste'}
INSERT_ONLY = {'direct_insert'}
COPY_ONLY = {'wl-copy', 'clipboard', 'qt-clipboard', 'qt'}
# macOS presses ⌘V in terminals too, so Terminal paste rules mean nothing there.
TERMINAL_KEYS_DIFFER = sys.platform != 'darwin'


def _truthy(value):
    return str(value).strip().lower() in ('true', '1')


def _delivery_steps(before, after):
    """The delivery steps the new app turns the old output method, global
    paste rule and clipboard restore switch into."""
    method = before.get('output/method', '')
    rules = json.loads(before.get('output/pasteRules') or '[]')
    stored_global = [rule for rule in rules if rule.get('scope') == 'global']
    turned_on = [rule for rule in stored_global if rule.get('enabled', True)]
    default = turned_on[0]['method'] if turned_on else 'clipboard_only' if stored_global else 'standard_paste'
    insert = method not in KEYBOARD_ONLY | COPY_ONLY and default != 'clipboard_only'
    press = method not in INSERT_ONLY | COPY_ONLY and default not in ('direct_insert', 'clipboard_only')
    keep = not _truthy(before.get('output/restoreClipboardAfterTyping')) or not (insert or press)
    after.pop('output/method', None)
    after.pop('output/restoreClipboardAfterTyping', None)
    for key, on in (('insertThroughAccessibility', insert), ('pressPasteKeys', press), ('keepOnClipboard', keep)):
        after['output/' + key] = 'true' if on else 'false'
    rules = [rule for rule in rules if rule.get('scope') != 'global']
    if not TERMINAL_KEYS_DIFFER:
        rules = [rule for rule in rules if not (rule.get('scope') == 'category' and rule.get('method') == 'terminal_paste')]
        for rule in rules:
            if rule.get('method') == 'terminal_paste':
                rule['method'] = 'standard_paste'
    if 'output/pasteRules' in before:
        after['output/pasteRules'] = json.dumps(rules)


def migrated(before):
    """before as the new app should leave it."""
    after = dict(before)
    version = int(before.get('migration/refinementModelVersion') or 0)
    for key, old, new, below in REPLACED_MODELS:
        if version < below and after.get(key, '').strip() == old:
            after[key] = new
    if 'output/method' in before or 'output/restoreClipboardAfterTyping' in before:
        _delivery_steps(before, after)
    return after


def _comparable(value):
    if value is None:
        return None
    try:
        return json.loads(value)
    except (TypeError, ValueError):
        return str(value).strip()


def differences(before, now, changed_by_script=None):
    """Each key of before whose value now isn't what the update should leave,
    with what the script itself wrote since (changed_by_script) taken as the
    intent. Keys the new app only adds aren't compared."""
    expected = migrated(before)
    expected.update(changed_by_script or {})
    wrong = {}
    for key, want in expected.items():
        if CHANGED_BY_RUNNING.match(key) or key in CHECKED_APART:
            continue
        if OLD_WINDOW.match(key):
            if key not in now:
                wrong[key] = {'expected': 'kept', 'now': None}
            continue
        if _comparable(now.get(key)) != _comparable(want):
            wrong[key] = {'expected': want, 'now': now.get(key)}
    return wrong


def vocabulary(values):
    return json.loads(values.get('stt/vocabularyEntries') or '[]')
