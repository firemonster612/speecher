// Drives the running Speecher's windows through System Events, as a person
// would with the mouse and keyboard, for the release gate
// (packaging/macos/release-gate.py). JavaScript for Automation:
//
//   osascript -l JavaScript release-gate-ui.js press LABEL
//   osascript -l JavaScript release-gate-ui.js press-after TEXT
//   osascript -l JavaScript release-gate-ui.js select LABEL
//   osascript -l JavaScript release-gate-ui.js type LABEL TEXT
//   osascript -l JavaScript release-gate-ui.js dump
//
// press: AXPress on the button named LABEL. press-after: AXPress on the
// first button after the text containing TEXT, beside it in the same group,
// for SwiftUI buttons that expose no title. select: selects the list row
// showing LABEL, such as a settings sidebar pane.
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

// Every element under element, depth first. Asked level by level rather
// than through entireContents, which missed a banner SwiftUI had just shown.
function* descendants(element) {
    for (const child of element.uiElements()) {
        yield child;
        yield* descendants(child);
    }
}

function find(process, label, roles, matches = (found) => found.includes(label)) {
    for (const window of process.windows()) {
        for (const element of descendants(window)) {
            if ((!roles || roles.includes(role(element))) && matches(labels(element))) {
                return element;
            }
        }
    }
    return null;
}

function parent(element) {
    return element.attributes.byName('AXParent').value();
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
            for (const element of descendants(window)) {
                lines.push(`  ${role(element)}: ${labels(element).join(' | ')}`);
            }
        });
        return lines.join('\n');
    }
    if (action === 'press-after') {
        const text = find(speecher, label, ['AXStaticText'], (found) => found.some((value) => value.includes(label)));
        const siblings = text ? parent(text).uiElements() : [];
        const index = siblings.findIndex((sibling) => labels(sibling).some((value) => value.includes(label)));
        const button = siblings.slice(index + 1).find((sibling) => role(sibling) === 'AXButton');
        if (!button) {
            throw new Error(`no button after a text containing "${label}"`);
        }
        button.actions.byName('AXPress').perform();
        return `pressed the first button after "${labels(siblings[index])[0]}"`;
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
    if (action === 'select') {
        // A synthetic click on a SwiftUI sidebar row's text doesn't select it.
        let row = element;
        while (role(row) !== 'AXRow') {
            row = parent(row);
        }
        row.selected = true;
        return `selected the row showing "${label}"`;
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
