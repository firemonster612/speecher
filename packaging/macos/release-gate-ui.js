// Drives the running Speecher's windows through System Events, as a person
// would with the mouse and keyboard, for the release gate
// (packaging/macos/release-gate.py). JavaScript for Automation:
//
//   osascript -l JavaScript release-gate-ui.js press LABEL
//   osascript -l JavaScript release-gate-ui.js click LABEL
//   osascript -l JavaScript release-gate-ui.js type LABEL TEXT
//   osascript -l JavaScript release-gate-ui.js dump
//
// press: AXPress on the button named LABEL. click: a mouse click at the centre
// of the element showing LABEL (a sidebar row's text has no press action).
// type: focuses the text field labelled LABEL, types TEXT and presses Return.
// dump: every element's role and labels, for working out what to match.
// Prints what it did; exits non-zero when nothing matched.

function labels(element) {
    const found = [];
    for (const read of [() => element.name(), () => element.description(), () => element.value(),
                        () => element.attributes.byName('AXPlaceholderValue').value()]) {
        try {
            const value = read();
            if (value !== null && value !== undefined && value !== '') {
                found.push(String(value));
            }
        } catch (error) {
            // Not every element has every attribute.
        }
    }
    return found;
}

function role(element) {
    try {
        return element.role();
    } catch (error) {
        return '';
    }
}

function find(process, label, roles) {
    for (const window of process.windows()) {
        for (const element of window.entireContents()) {
            if ((!roles || roles.includes(role(element))) && labels(element).includes(label)) {
                return element;
            }
        }
    }
    return null;
}

function run(argv) {
    const [action, label, text] = argv;
    const events = Application('System Events');
    const speecher = events.processes.byName('speecher');
    speecher.frontmost = true;
    delay(0.5);
    if (action === 'dump') {
        const lines = [];
        speecher.windows().forEach((window, index) => {
            lines.push(`window ${index}: ${labels(window).join(' | ')}`);
            for (const element of window.entireContents()) {
                lines.push(`  ${role(element)}: ${labels(element).join(' | ')}`);
            }
        });
        return lines.join('\n');
    }
    const element = find(speecher, label,
                         action === 'press' ? ['AXButton'] : action === 'type' ? ['AXTextField'] : null);
    if (!element) {
        throw new Error(`no element labelled "${label}" in ${speecher.windows().length} Speecher windows`);
    }
    if (action === 'press') {
        element.actions.byName('AXPress').perform();
        return `pressed "${label}"`;
    }
    if (action === 'click') {
        const [x, y] = element.position();
        const [width, height] = element.size();
        speecher.click({at: [x + width / 2, y + height / 2]});
        return `clicked "${label}" at ${x + width / 2},${y + height / 2}`;
    }
    if (action === 'type') {
        element.focused = true;
        delay(0.3);
        events.keystroke(text);
        events.keyCode(36);
        return `typed ${text.length} characters into "${label}"`;
    }
    throw new Error(`unknown action ${action}`);
}
