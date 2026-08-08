# Domain docs

These rules explain how engineering skills should read this repository's domain documentation.

## Before exploring

Read the following when they exist:

- `CONTEXT.md` at the repository root
- Relevant ADRs under `docs/adr/`

If a file or directory doesn't exist, proceed without calling attention to it. The `/domain-modeling` skill creates domain documents when the project resolves terms or architectural decisions.

## File structure

This is a single-context repository:

```text
/
├── CONTEXT.md
├── docs/
│   └── adr/
└── src/
```

## Use the glossary's vocabulary

When naming a domain concept in an issue, proposal, hypothesis, or test, use the term defined in `CONTEXT.md`. Don't switch to synonyms that the glossary explicitly avoids.

If a needed concept isn't in the glossary, reconsider whether it belongs to the project's language. If it does, record the gap for `/domain-modeling`.

## Flag ADR conflicts

If proposed work contradicts an existing ADR, say so explicitly instead of silently overriding the decision.
