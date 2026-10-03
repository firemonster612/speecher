// AX driver for vocabulary_run.sh: osascript -l JavaScript vocabulary_ax.js
// <command> [args]. Scratch-branch-only. Scopes are "window" (the settings
// window, sheets included) and "sheet" (the window's open sheet).

const se = Application('System Events');
const proc = se.processes.byName('speecher');

function attr(element, name) {
    try { return element[name](); } catch (error) { return null; }
}

function settingsWindow() {
    let best = null;
    let bestArea = 0;
    for (const window of proc.windows()) {
        const size = attr(window, 'size');
        const area = size ? size[0] * size[1] : 0;
        if (area > bestArea) { best = window; bestArea = area; }
    }
    if (!best) throw new Error('no speecher window');
    return best;
}

function scope(name) {
    const window = settingsWindow();
    if (name !== 'sheet') return window;
    const sheets = window.sheets();
    if (sheets.length === 0) throw new Error('no sheet open');
    return sheets[0];
}

function describe(element) {
    return {
        role: attr(element, 'role'),
        subrole: attr(element, 'subrole'),
        name: attr(element, 'name'),
        description: attr(element, 'description'),
        value: attr(element, 'value'),
        enabled: attr(element, 'enabled'),
        position: attr(element, 'position'),
        size: attr(element, 'size'),
    };
}

function labelMatches(info, label) {
    const wanted = label.endsWith('*') ? label.slice(0, -1) : label;
    const exact = !label.endsWith('*');
    for (const text of [info.name, info.description]) {
        if (typeof text !== 'string') continue;
        if (exact ? text === wanted : text.startsWith(wanted)) return true;
    }
    return false;
}

// Role "input" is any text input: a one-line field or a multi-line area.
function roleMatches(actual, role) {
    return role === 'input' ? actual === 'AXTextField' || actual === 'AXTextArea' : actual === role;
}

function matches(scopeName, role, label) {
    const found = [];
    for (const element of scope(scopeName).entireContents()) {
        if (!roleMatches(attr(element, 'role'), role)) continue;
        const info = describe(element);
        if (label === '' || labelMatches(info, label)) found.push({ element, info });
    }
    return found;
}

function only(scopeName, role, label, index) {
    const found = matches(scopeName, role, label);
    const at = Number(index || 0);
    if (found.length <= at) throw new Error(`no ${role} "${label}" #${at} in ${scopeName}`);
    return found[at];
}

function centre(info) {
    return [Math.round(info.position[0] + info.size[0] / 2),
            Math.round(info.position[1] + info.size[1] / 2)];
}

function line(info) {
    return [info.role, info.subrole, JSON.stringify(info.name), JSON.stringify(info.description),
            JSON.stringify(info.value), info.enabled, JSON.stringify(info.position),
            JSON.stringify(info.size)].join(' | ');
}

function run(argv) {
    const [command, ...args] = argv;
    switch (command) {
    case 'front':
        proc.frontmost = true;
        return 'ok';
    case 'frame': {
        const info = describe(settingsWindow());
        return [...info.position, ...info.size].map(Math.round).join(',');
    }
    // hastext SCOPE TEXT: whether a static text reads TEXT, as a title does.
    case 'hastext':
        for (const element of scope(args[0]).entireContents()) {
            if (attr(element, 'role') !== 'AXStaticText') continue;
            const info = describe(element);
            if (info.value === args[1] || info.name === args[1]) return 'yes';
        }
        return 'no';
    case 'sheets':
        return String(settingsWindow().sheets().length);
    case 'dump':
        return scope(args[0]).entireContents().map(element => line(describe(element))).join('\n');
    // find SCOPE ROLE LABEL [INDEX]: the match's centre, "x y".
    case 'find':
        return centre(only(args[0], args[1], args[2], args[3]).info).join(' ');
    case 'value':
        return JSON.stringify(only(args[0], args[1], args[2], args[3]).info.value);
    case 'enabled':
        return String(only(args[0], args[1], args[2], args[3]).info.enabled);
    case 'press':
        only(args[0], args[1], args[2], args[3]).element.actions.byName('AXPress').perform();
        return 'ok';
    case 'focus':
        only(args[0], args[1], args[2], args[3]).element.focused = true;
        return 'ok';
    // has SCOPE TEXT: whether any element shows TEXT as its value, name or description.
    case 'has':
        for (const element of scope(args[0]).entireContents()) {
            const info = describe(element);
            if ([info.value, info.name, info.description].includes(args[1])) return 'yes';
        }
        return 'no';
    // rowof TERM: the centre of a read-only cell in the table row showing TERM
    // (the row's last static text, away from the editable term field).
    case 'rowof': {
        for (const table of scope('window').entireContents()) {
            if (attr(table, 'role') !== 'AXTable') continue;
            for (const row of table.rows()) {
                const cells = row.entireContents().map(describe);
                if (!cells.some(cell => cell.value === args[0] || cell.name === args[0])) continue;
                const texts = cells.filter(cell => cell.role === 'AXStaticText' && cell.size
                                           && cell.size[0] > 0);
                const target = texts.length ? texts[texts.length - 1] : describe(row);
                return centre(target).join(' ');
            }
        }
        throw new Error(`no table row shows ${args[0]}`);
    }
    // selectrow TERM: sets the row's AXSelected, the keyboard-free way to pick it.
    case 'selectrow': {
        for (const table of scope('window').entireContents()) {
            if (attr(table, 'role') !== 'AXTable') continue;
            for (const row of table.rows()) {
                const cells = row.entireContents().map(describe);
                if (cells.some(cell => cell.value === args[0] || cell.name === args[0])) {
                    row.selected = true;
                    return 'ok';
                }
            }
        }
        throw new Error(`no table row shows ${args[0]}`);
    }
    case 'click':
        se.click({ at: [Number(args[0]), Number(args[1])] });
        return 'ok';
    case 'type':
        for (const character of args[0]) {
            se.keystroke(character);
            delay(0.04);
        }
        return 'ok';
    default:
        throw new Error(`unknown command ${command}`);
    }
}
