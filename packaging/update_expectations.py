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
# Under these output methods Speecher never pasted: a pasting rule inserted
# only, or copied only.
PASTE_UNDER_METHOD = {'direct_insert': 'direct_insert', 'wl-copy': 'clipboard_only',
                      'clipboard': 'clipboard_only', 'qt-clipboard': 'clipboard_only', 'qt': 'clipboard_only'}


def migrated(before):
    """before as the new app should leave it."""
    after = dict(before)
    version = int(before.get('migration/refinementModelVersion') or 0)
    for key, old, new, below in REPLACED_MODELS:
        if version < below and after.get(key, '').strip() == old:
            after[key] = new
    paste = PASTE_UNDER_METHOD.get(before.get('output/method', ''))
    if paste:
        rules = json.loads(before.get('output/pasteRules') or '[]')
        for rule in rules:
            if rule.get('method') in ('standard_paste', 'terminal_paste'):
                rule['method'] = paste
        after['output/pasteRules'] = json.dumps(rules)
        after['output/method'] = 'automatic'
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
