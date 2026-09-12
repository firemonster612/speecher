import json
from pathlib import Path
import sys
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
