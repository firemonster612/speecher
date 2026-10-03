// AX driver for vocabulary_run.sh: osascript -l JavaScript vocabulary_ax.js
// <command> [args]. Scratch-branch-only. Scopes are "window" (the settings
// window, sheets included) and "sheet" (the window's open sheet).
//
// Roles are AX roles, plus "input" (a one-line field or a multi-line area) and
// "unnamed" (a plain button with no title, description or help, as SwiftUI's
// accessory-bar buttons show up on the runner). An index counts matches in
// tree order; a negative one counts from the end.

const se = Application('System Events');
const proc = se.processes.byName('speecher');

function attr(element, name) {
    try { return element[name](); } catch (error) { return null; }
}

function axAttr(element, name) {
    try { return element.attributes.byName(name).value(); } catch (error) { return null; }
}

// Every element under ROOT, depth first, each addressed by index. System
// Events' entire contents names its elements by title, so siblings sharing one
// (the sheet's profile checkboxes are all "Profiles") resolve to the first.
function walk(root) {
    const found = [];
    const visit = element => {
        let count = 0;
        try { count = element.uiElements.length; } catch (error) { return; }
        for (let index = 0; index < count; index++) {
            const child = element.uiElements[index];
            found.push(child);
            visit(child);
        }
    };
    visit(root);
    return found;
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
        title: axAttr(element, 'AXTitle'),
        description: axAttr(element, 'AXDescription'),
        help: axAttr(element, 'AXHelp'),
        value: attr(element, 'value'),
        enabled: attr(element, 'enabled'),
        position: attr(element, 'position'),
        size: attr(element, 'size'),
    };
}

function labels(info) {
    return [info.title, info.description, info.help].filter(text => typeof text === 'string' && text !== '');
}

function labelMatches(info, label) {
    const wanted = label.endsWith('*') ? label.slice(0, -1) : label;
    const exact = !label.endsWith('*');
    return labels(info).some(text => (exact ? text === wanted : text.startsWith(wanted)));
}

function roleMatches(info, role) {
    if (role === 'input') return info.role === 'AXTextField' || info.role === 'AXTextArea';
    if (role === 'unnamed') return info.role === 'AXButton' && !info.subrole && labels(info).length === 0;
    return info.role === role;
}

function matches(scopeName, role, label) {
    const found = [];
    for (const element of walk(scope(scopeName))) {
        const kind = attr(element, 'role');
        if (role !== 'input' && role !== 'unnamed' && kind !== role) continue;
        if (role === 'unnamed' && kind !== 'AXButton') continue;
        const info = describe(element);
        if (!roleMatches(info, role)) continue;
        if (label === '' || labelMatches(info, label)) found.push({ element, info });
    }
    return found;
}

function only(scopeName, role, label, index) {
    const found = matches(scopeName, role, label);
    let at = Number(index || 0);
    if (at < 0) at += found.length;
    if (at < 0 || found.length <= at) throw new Error(`no ${role} "${label}" #${index || 0} in ${scopeName}`);
    return found[at];
}

function centre(info) {
    return [Math.round(info.position[0] + info.size[0] / 2),
            Math.round(info.position[1] + info.size[1] / 2)];
}

function line(info) {
    return [info.role, info.subrole, JSON.stringify(info.title), JSON.stringify(info.description),
            JSON.stringify(info.help), JSON.stringify(info.value), info.enabled,
            JSON.stringify(info.position), JSON.stringify(info.size)].join(' | ');
}

// The table's row (an outline row on the runner) that shows TEXT, as
// { row, cells }.
function rowShowing(text) {
    for (const table of walk(scope('window'))) {
        const role = attr(table, 'role');
        if (role !== 'AXOutline' && role !== 'AXTable') continue;
        if (axAttr(table, 'AXDescription') === 'Sidebar') continue;
        for (const row of table.rows()) {
            const cells = walk(row).map(describe);
            if (cells.some(cell => cell.value === text)) {
                const clip = describe(axAttr(table, 'AXParent') || table);
                return { row, cells, right: clip.position[0] + clip.size[0] };
            }
        }
    }
    throw new Error(`no table row shows ${text}`);
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
    case 'sheets':
        return String(settingsWindow().sheets().length);
    case 'dump':
        return walk(scope(args[0])).map(element => line(describe(element))).join('\n');
    // find SCOPE ROLE LABEL [INDEX]: the match's centre, "x y".
    case 'find':
        return centre(only(args[0], args[1], args[2], args[3]).info).join(' ');
    // frameof SCOPE ROLE LABEL [INDEX]: the match's frame, "x,y,w,h".
    case 'frameof': {
        const info = only(args[0], args[1], args[2], args[3]).info;
        return [...info.position, ...info.size].map(Math.round).join(',');
    }
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
    // has SCOPE TEXT: whether any element holds TEXT as its value or a label.
    case 'has':
        for (const element of walk(scope(args[0]))) {
            const info = describe(element);
            if (info.value === args[1] || labels(info).includes(args[1])) return 'yes';
        }
        return 'no';
    // hastext SCOPE TEXT: whether a static text reads TEXT.
    case 'hastext':
        for (const element of walk(scope(args[0]))) {
            if (attr(element, 'role') !== 'AXStaticText') continue;
            if (attr(element, 'value') === args[1]) return 'yes';
        }
        return 'no';
    // bounds SCOPE: the scope's frame, "x,y,w,h".
    case 'bounds': {
        const info = describe(scope(args[0]));
        return [...info.position, ...info.size].map(Math.round).join(',');
    }
    // row TERM: the texts the row showing TERM holds, joined with " | ".
    case 'row':
        return rowShowing(args[0]).cells
            .filter(cell => typeof cell.value === 'string' && cell.value !== '')
            .map(cell => cell.value).join(' | ');
    // pills TERM: the pills in the cell after TERM's, top to bottom, joined
    // with ","; or, when a pill is elsewhere in the row, wider than that cell,
    // or out of one left-aligned stack, where they are.
    case 'pills': {
        const { cells } = rowShowing(args[0]);
        const within = (inner, outer) => inner.position[0] >= outer.position[0]
            && inner.position[0] + inner.size[0] <= outer.position[0] + outer.size[0];
        const term = cells.find(cell => cell.value === args[0]);
        const columns = cells.filter(cell => cell.role === 'AXCell');
        const speech = columns[columns.findIndex(column => within(term, column)) + 1];
        const pills = cells
            .filter(cell => cell.role === 'AXStaticText'
                    && (cell.value === 'Key term' || cell.value === 'Priority'))
            .sort((a, b) => a.position[1] - b.position[1]);
        const stacked = pills.every((pill, index) => pill.position[0] === pills[0].position[0]
            && (index === 0 || pill.position[1] >= pills[index - 1].position[1] + pills[index - 1].size[1]));
        if (!speech || !stacked || !pills.every(pill => within(pill, speech))) {
            return `misplaced: ${pills.map(line).join('; ')} in ${speech ? line(speech) : 'no cell'}`;
        }
        return pills.map(pill => pill.value).join(',');
    }
    // rowof TERM: the centre of the row's last static text still in view (a
    // read-only cell, away from the editable term field).
    case 'rowof': {
        const { row, cells, right } = rowShowing(args[0]);
        const texts = cells.filter(cell => cell.role === 'AXStaticText' && cell.size
                                   && cell.size[0] > 0 && centre(cell)[0] < right - 4);
        return centre(texts.length ? texts[texts.length - 1] : describe(row)).join(' ');
    }
    case 'selectrow':
        rowShowing(args[0]).row.selected = true;
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
