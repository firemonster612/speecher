// Drives the running Speecher's windows through System Events, as a person
// would with the mouse and keyboard, for the release gate
// (packaging/macos/release-gate.py). JavaScript for Automation:
//
//   osascript -l JavaScript release-gate-ui.js press LABEL
//   osascript -l JavaScript release-gate-ui.js press-after TEXT
//   osascript -l JavaScript release-gate-ui.js select LABEL
//   osascript -l JavaScript release-gate-ui.js type LABEL TEXT
//   osascript -l JavaScript release-gate-ui.js find LABEL
//   osascript -l JavaScript release-gate-ui.js dump
//
// press: AXPress on the element named LABEL that can be pressed (a button, a
// Qt Quick sidebar tab). press-after: AXPress on the first button after the
// text containing TEXT, for SwiftUI buttons that expose no title. select:
// selects the list row showing LABEL, such as a SwiftUI sidebar pane. type:
// focuses the text field labelled LABEL, types TEXT and presses Return. find:
// whether any element shows LABEL. dump: every element's role and labels, for
// working out what to match. Prints what it did; exits non-zero when nothing
// matched.

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

function pressable(element) {
    try {
        return element.actions.name().includes('AXPress');
    } catch (error) {
        return false;
    }
}

// Every element of the process's windows in document order, depth first.
// Asked level by level rather than through entireContents, which missed a
// banner SwiftUI had just shown.
function* elements(process) {
    function* under(element) {
        for (const child of element.uiElements()) {
            yield child;
            yield* under(child);
        }
    }
    for (const window of process.windows()) {
        yield* under(window);
    }
}

function find(process, matches) {
    for (const element of elements(process)) {
        if (matches(element)) {
            return element;
        }
    }
    return null;
}

function named(label) {
    return (element) => labels(element).includes(label);
}

function press(element) {
    element.actions.byName('AXPress').perform();
}

function run(argv) {
    const [action, label, text] = argv;
    const events = Application('System Events');
    const speecher = events.processes.byName('speecher');
    speecher.frontmost = true;
    delay(0.5);
    if (action === 'dump') {
        const lines = [];
        for (const element of elements(speecher)) {
            lines.push(`${role(element)}: ${labels(element).join(' | ')}`);
        }
        return lines.join('\n');
    }
    if (action === 'press-after') {
        let seen = false;
        const button = find(speecher, (element) => {
            seen = seen || labels(element).some((value) => value.includes(label));
            return seen && role(element) === 'AXButton';
        });
        if (!button) {
            throw new Error(`no button after a text containing "${label}"`);
        }
        press(button);
        return `pressed the first button after "${label}"`;
    }
    const element = find(speecher, {
        press: (candidate) => named(label)(candidate) && pressable(candidate),
        select: named(label),
        type: (candidate) => named(label)(candidate) && role(candidate) === 'AXTextField',
        find: named(label),
    }[action]);
    if (!element) {
        throw new Error(`${action}: no element labelled "${label}" in ${speecher.windows().length} Speecher windows`);
    }
    if (action === 'press') {
        press(element);
        return `pressed "${label}" (${role(element)})`;
    }
    if (action === 'select') {
        // A synthetic click on a SwiftUI sidebar row's text doesn't select it.
        let row = element;
        while (role(row) !== 'AXRow') {
            row = row.attributes.byName('AXParent').value();
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
    return `found "${label}" (${role(element)})`;
}
