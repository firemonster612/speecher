# Release notes

Keep one Markdown file here for each Stable Release. Its filename must start
with a digit and match the version without the `v` prefix. For example, the
notes for tag `v0.1.1` live in `0.1.1.md`.

Start the notes at `##`; don't add a release title because each destination
supplies one. CI uses the file verbatim as the GitHub release body, embeds the
same Markdown as preformatted text in the Sparkle appcast, and bundles it for
the What's New page. If the file is missing, CI publishes a stub asking for it.
A file that exists but contains only headings fails the tag build.
