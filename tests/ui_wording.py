#!/usr/bin/env python3
"""Drift checks for user-visible wording (docs/adr/0007-presentation-lives-in-core.md).

  ui_wording.py lint        Core presentation strings follow the house wording rules.
  ui_wording.py front-ends  No front end hard-codes a string core already owns.

A heuristic, not a C++ parser: it reads QStringLiteral/QLatin1String/tr
literals with a regex and treats one as user-visible when it contains a space
or starts with a capital letter. The front-end check flags any string literal
(Qt, Swift, C++/WinRT L"…") equal to a core string of two or more words.

`// ui-lint: allow <rule>` on the literal's line, or the line above, allows one
real exception. Known drift not fixed yet is listed in ui_wording_allowlist.txt.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Core presentation code, by directory and pattern, so new modules are covered.
CORE_SOURCES = [
    "src/core/settings/*",
    "src/**/*Presentation.*",
    "src/dictation/DictationTypes.*",
    "src/app/UpdateBanner.*",
    "src/**/SetupSteps.*",
    "src/core/Insights*",
]
FRONT_END_SOURCES = ["src/frontend/qt", "src/frontend/mac", "src/frontend/win", "src/ui"]
FRONT_END_SUFFIXES = {".cpp", ".h", ".mm", ".swift", ".xaml"}

CORE_LITERAL = re.compile(r'\b(?:QStringLiteral|QLatin1String|tr)\(\s*((?:"(?:[^"\\\n]|\\.)*"\s*)+)\)')
ANY_LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
PIECE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
MARKER = re.compile(r"ui-lint: allow ([\w-]+)")

US_SPELLING = re.compile(
    r"\b(\w*(?:behaviour|colour|favour|honour|labour|neighbour)\w*"
    r"|licence[sd]?|defence|centre[sd]?|grey\w*|catalogue[sd]?|whilst|amongst"
    r"|\w*(?:organis|recognis|customis|initialis|normalis|optimis|prioritis|summaris|synchronis"
    r"|authoris|minimis|maximis|finalis|utilis|apologis|personalis|realis|visualis)(?:e|es|ed|ing|ation)"
    r"|analys(?:e|es|ed|ing)|cancell(?:ed|ing)|label+ed|labell\w+|travell\w+|modell\w+)\b",
    re.IGNORECASE,
)

# Names that keep their capitals besides the CONTEXT.md glossary terms.
PROPER_NAMES = [
    "Speecher", "Claude", "Claude Voice", "ChatGPT", "Codex", "OpenAI", "Anthropic", "Gemini",
    "KDE", "Plasma", "KWin", "GNOME", "Wayland", "X11", "Linux", "Windows", "macOS", "Mac",
    "Apple", "Microsoft", "Sparkle", "AppImage", "GitHub", "Ollama", "LM Studio", "llama-server",
    "Whisper", "Parakeet", "Moonshine", "Vulkan", "CUDA", "Metal", "Stable", "Nightly", "Home",
    "General", "Accounts", "Dictation", "Local models", "Transcribe", "Refinement", "Vocabulary",
    "Output", "Finder", "Explorer", "Settings", "System Settings", "Accessibility", "Terminal",
    "Work", "Email", "Personal", "AI coding", "Other", "None", "Light", "Medium", "High",
    "Low", "Standard", "What's New", "Speecher Setup Assistant", "CLI Proxy API", "Claude Code", "Opus", "Sonnet", "Haiku",
    # Windows and vendor API names.
    "UI Automation", "Chat Completions", "Messages",
]
KEY_NAMES = ["Ctrl", "Control", "Shift", "Alt", "Option", "Cmd", "Command", "Meta", "Super",
             "Fn", "Esc", "Escape", "Enter", "Return", "Tab", "Space", "Backspace", "Delete"]


def read_context():
    text = (ROOT / "CONTEXT.md").read_text(encoding="utf-8")
    terms = re.findall(r"^\*\*(.+?)\*\*:", text, re.MULTILINE)
    avoid = [term.strip() for line in re.findall(r"^_Avoid_:(.+)$", text, re.MULTILINE)
             for term in line.split(",")]
    return terms, [term for term in avoid if term]


def source_files(patterns):
    files = set()
    for pattern in patterns:
        files.update(path for path in ROOT.glob(pattern) if path.suffix in {".cpp", ".h"})
    return sorted(files)


def unescape(text):
    return text.replace('\\"', '"').replace("\\n", "\n").replace("\\\\", "\\")


def literals(path, pattern):
    """Matches of pattern outside comments, as (match, line, rules allowed by a marker)."""
    source = path.read_text(encoding="utf-8")
    lines = source.splitlines()
    for match in pattern.finditer(source):
        line = source.count("\n", 0, match.start()) + 1
        before = source[source.rfind("\n", 0, match.start()) + 1:match.start()]
        if re.search(r"(^|\s)//", before) or before.lstrip().startswith(("*", "/*")):
            continue
        marked = {rule for text in lines[max(0, line - 2):line] for rule in MARKER.findall(text)}
        yield match, line, marked


def core_strings():
    """Every user-visible core literal as (path, line, text, rules allowed by a marker)."""
    found = []
    for path in source_files(CORE_SOURCES):
        for match, line, marked in literals(path, CORE_LITERAL):
            text = unescape("".join(PIECE.findall(match.group(1))))
            if not re.search(r"[A-Za-z]", text) or not (" " in text or text[:1].isupper()):
                continue
            # URLs and regular expressions.
            if re.match(r"[a-z]+://|\^", text):
                continue
            found.append((path.relative_to(ROOT).as_posix(), line, text, marked))
    return found


def phrase_pattern(phrases):
    ordered = sorted(set(phrases), key=len, reverse=True)
    return re.compile(r"(?<![\w-])(?:" + "|".join(re.escape(p) for p in ordered) + r")(?![\w-])")


def is_title_case(text, exempt):
    # Captions only: short, and not a sentence.
    if text.rstrip().endswith((".", "!", "?", ":")) or len(text.split()) > 7:
        return False
    # Exempt names become a lowercase word, then the first word may be capitalised.
    later = re.findall(r"[A-Za-z][\w'’-]*", exempt.sub("x", text))[1:]
    # Acronyms, camel-case names (API, AppImage) and key letters keep their capitals.
    return any(len(w) > 1 and w[0].isupper() and not re.search(r"[A-Z]", w[1:]) for w in later)


def violations(text, avoid, exempt):
    found = []
    # "..." inside a token, as in a compare range "a...b", is not an ellipsis.
    if re.search(r"\.\.\.(?!\S)|(?<!\S)\.\.\.", text):
        found.append(("ellipsis", 'use "…" (U+2026), not "..."'))
    for spelling in US_SPELLING.findall(text):
        found.append(("us-spelling", f'"{spelling}" is not US spelling'))
    if any(is_title_case(part, exempt) for part in text.split(";;")):
        found.append(("title-case", "captions are sentence case"))
    for term in avoid.findall(text):
        found.append(("avoid-term", f'"{term}" is a CONTEXT.md Avoid term'))
    return found


def read_allowlist():
    """Maps (rule, path, text) to the comment above its entry, which says what clears it."""
    entries = {}
    note = ""
    for raw in (ROOT / "tests" / "ui_wording_allowlist.txt").read_text(encoding="utf-8").splitlines():
        if raw.startswith("#"):
            note = raw.lstrip("# ")
        elif raw.strip():
            rule, path, text = raw.split(" ", 2)
            entries[(rule, path, unescape(text))] = note
    return entries


def lint():
    terms, avoid_terms = read_context()
    exempt = phrase_pattern(terms + PROPER_NAMES + KEY_NAMES)
    avoid = re.compile(phrase_pattern(avoid_terms).pattern, re.IGNORECASE)
    for path, line, text, marked in core_strings():
        for rule, reason in violations(text, avoid, exempt):
            if rule not in marked:
                yield path, line, rule, text, reason


def front_ends():
    owned = {text for _, _, text, marked in core_strings()
             if len(text.split()) > 1 and "core-string" not in marked}
    for directory in FRONT_END_SOURCES:
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in FRONT_END_SUFFIXES:
                continue
            for match, line, marked in literals(path, ANY_LITERAL):
                text = unescape(match.group(1))
                if text in owned and "core-string" not in marked:
                    yield (path.relative_to(ROOT).as_posix(), line, "core-string", text,
                           "core already words this; read it from core")


def report(findings, rules):
    """Prints findings against the allowlist and returns how many fail."""
    allowlist = {entry: note for entry, note in read_allowlist().items() if entry[0] in rules}
    used = set()
    failures = 0
    for path, line, rule, text, reason in findings:
        key = (rule, path, text)
        if key in allowlist:
            used.add(key)
            print(f"allowed  {path}:{line}: {rule}: {text!r} ({allowlist[key]})")
        else:
            failures += 1
            print(f"FAIL     {path}:{line}: {rule}: {reason}: {text!r}")
    # A warning, not a failure: the pull request that fixes the wording can
    # land before or after the one that deletes its entry.
    for rule, path, text in sorted(allowlist.keys() - used):
        print(f"stale    {path}: {rule}: {text!r} no longer occurs; delete it from the allowlist")
    return failures


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    checks = {
        "lint": (lint, {"ellipsis", "us-spelling", "title-case", "avoid-term"}),
        "front-ends": (front_ends, {"core-string"}),
    }
    if len(sys.argv) != 2 or sys.argv[1] not in checks:
        sys.exit(f"usage: {sys.argv[0]} {' | '.join(checks)}")
    check, rules = checks[sys.argv[1]]
    count = report(check(), rules)
    print(f"{count} failure(s)")
    sys.exit(1 if count else 0)
