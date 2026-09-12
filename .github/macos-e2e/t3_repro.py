import json
import os
from pathlib import Path
import sys
import subprocess
import time
from urllib.request import urlopen
from playwright.sync_api import sync_playwright

out = Path(sys.argv[1])
for attempt in range(60):
    try:
        with urlopen('http://127.0.0.1:9222/json/version', timeout=1) as response:
            json.load(response)
        break
    except OSError:
        time.sleep(1)
else:
    raise RuntimeError('T3 Code did not expose its test connection')

with sync_playwright() as driver:
    browser = driver.chromium.connect_over_cdp('http://127.0.0.1:9222')
    context = browser.contexts[0]
    page = context.pages[0] if context.pages else context.wait_for_event('page', timeout=60000)
    page.wait_for_timeout(10000)
    def capture(name):
        page.screenshot(path=str(out / f'{name}.png'))
        (out / f'{name}.txt').write_text(page.locator('body').inner_text())
        (out / f'{name}.html').write_text(page.content())
    capture('initial')
    for index, label in enumerate(['Continue', 'Continue', 'Do not import projects', 'Start coding', 'Add project']):
        button = page.get_by_role('button', name=label, exact=True)
        if button.count() and button.first.is_visible() and button.first.is_enabled():
            button.first.click()
            page.wait_for_timeout(1500)
            capture(f'{index}-' + label.replace(' ', '-').lower())
    page.get_by_text('Local folder', exact=True).click()
    time.sleep(2)
    subprocess.run(['osascript', '-e', '''tell application "System Events"
        keystroke "g" using {command down, shift down}
        delay 1
        keystroke "/tmp/speecher-paste-project"
        key code 36
        delay 1
        key code 36
    end tell'''], check=True, timeout=15)
    page.wait_for_timeout(3000)
    capture('project-added')
    button = page.get_by_role('button', name='New thread', exact=True)
    if button.count() and button.first.is_visible():
        button.first.click()
        page.wait_for_timeout(2000)
    capture('composer')
    editor = page.get_by_test_id('composer-editor')
    editor.click()
    app = os.environ['APP_BIN']
    subprocess.run([app, 'start'], check=True, timeout=15)
    page.wait_for_timeout(2500)
    subprocess.run([app, 'stop'], check=True, timeout=15)
    for attempt in range(100):
        status = subprocess.run([app, 'status'], capture_output=True, text=True, timeout=5)
        if status.stdout.strip().endswith('idle'):
            break
        page.wait_for_timeout(200)
    capture('after-dictation')
    actual = editor.inner_text().strip()
    copied = subprocess.run(['pbpaste'], capture_output=True, text=True, check=True).stdout
    (out / 'delivery.json').write_text(json.dumps({'editor': actual, 'clipboard': copied}, indent=2))
    assert actual == 'The quick brown fox.', f'Text did not reach T3 Code: {actual!r}; clipboard: {copied!r}'
