// Drives the running Speecher's windows through System Events, as a person
// would with the mouse and keyboard, for the release gate
// (packaging/macos/release-gate.py). JavaScript for Automation:
//
//   osascript -l JavaScript release-gate-ui.js press LABEL
//   osascript -l JavaScript release-gate-ui.js press-after TEXT
//   osascript -l JavaScript release-gate-ui.js select LABEL
//   osascript -l JavaScript release-gate-ui.js type LABEL TEXT
//   osascript -l JavaScript release-gate-ui.js field-after LABEL TEXT
//   osascript -l JavaScript release-gate-ui.js dump
//
// press: AXPress on the element named LABEL that can be pressed (a button, a
// Qt Quick sidebar tab). press-after: AXPress on the first button after the
// text containing TEXT, for SwiftUI buttons that expose no title. select:
// selects the list row showing LABEL, such as a SwiftUI sidebar pane. type:
// focuses the text field labelled LABEL, types TEXT and presses Return.
// field-after: whether the first text field after the element named LABEL
// shows TEXT. dump: every element's role and labels, for working out what to
// match. Prints what it did; exits non-zero when nothing matched.

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

// The first element with the role wanted at or after the first one matching
// seen, in document order.
function after(process, seen, wanted) {
    let passed = false;
    return find(process, (element) => {
        passed = passed || seen(element);
        return passed && role(element) === wanted;
    });
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
        const button = after(speecher, (element) => labels(element).some((value) => value.includes(label)),
                             'AXButton');
        if (!button) {
            throw new Error(`no button after a text containing "${label}"`);
        }
        press(button);
        return `pressed the first button after "${label}"`;
    }
    if (action === 'field-after') {
        const field = after(speecher, named(label), 'AXTextField');
        if (!field || !labels(field).includes(text)) {
            const shown = field ? labels(field).join(' | ') : 'no text field';
            throw new Error(`after "${label}": ${shown}, not "${text}"`);
        }
        return `the first text field after "${label}" shows "${text}"`;
    }
    const element = find(speecher, {
        press: (candidate) => named(label)(candidate) && pressable(candidate),
        select: named(label),
        type: (candidate) => named(label)(candidate) && role(candidate) === 'AXTextField',
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
    element.focused = true;
    delay(0.3);
    events.keystroke(text);
    events.keyCode(36);
    return `typed ${text.length} characters into "${label}"`;
}
