# Plasma 6 Wayland application matrix

Status: live check on `cachy`, 2026-07-30

All checks ran in the active Plasma 6 Wayland session. Test documents and
browser profiles contained only disposable text. The user's existing Helium
profile was not modified; Helium and Thunderbird used isolated temporary
profiles. Speecher's CLI `start`/`stop` pair was exercised while each target was
focused, and the non-activating popup preserved the target. Daemon `toggle`,
idempotent `start`/`stop`, format overrides, and background startup are also
covered by the automated session/IPC tests.

“Context” below reports only lengths and capabilities, not captured contents.
“Both” means distinct `text/plain;charset=utf-8` and `text/html` clipboard
offers were published through the native Wayland data-control helper.

| Application | Captured target | Category / profile | Bounded context | Paste rule / method | Clipboard | Strongest receipt | Restore | Correction observation |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Kate | `kate`, text control | Code editor / Work | title 40, caret 20, before 20 | global / standard paste | Both | Verified in Target | Restored after verification | Eligible |
| Konsole | `konsole`, terminal | Terminal / Work | title 19, caret 8, before 9, after 1 | terminal category / terminal paste | Both | Verified in Target | Restored after verification | Ineligible: no reliable direct span |
| Firefox | `firefox`, combo box | Browser / Other | title 15, control 35, caret 20, before 20 | global / standard paste | Both | Verified in Target | Restored after verification | Eligible |
| Helium | `helium`, entry | Browser / Other | title 20, control 22, caret 20, before 20 | global / standard paste | Both | Input Sent | Kept Speecher result because readback was unavailable | Ineligible: unverified |
| Thunderbird | `thunderbird`, entry | Email / Email | title 26, control 10, caret 28, before 28 | global / standard paste | Both | Verified in Target | Restored after verification | Eligible |
| LibreOffice Writer | `soffice.bin`, paragraph | Office / Work | title 31, caret 20, before 20 | global / standard paste | Both | Input Sent | Kept Speecher result because readback was unavailable | Ineligible: unverified |
| T3 Code | `t3code`, initial list item | Code editor / Work | title 15, control 36; no editable caret on initial surface | global / standard paste | Both | Input Sent | Kept Speecher result because readback was unavailable | Ineligible: no verified editable span |
| Qt password fixture | secure password-text control | General / Other | All text, selection, screenshot, and direct-insert context withheld | secure refusal / clipboard only | Both | Copied | Kept Speecher result | Refused |

The matrix deliberately preserves honest differences between applications.
Synthetic input is not promoted to “Verified in Target” unless bounded AT-SPI
readback finds the inserted text. When readback is unavailable, the transcription
remains on the clipboard and clipboard restoration does not remove the user's
only copy.

The password fixture additionally passed focused live checks proving that
Speecher does not capture screenshot context, attempt insertion, verify text, or
start correction observation for a secure target.

The native Wayland clipboard check independently verified distinct plain and
HTML byte streams and restoration of the original multi-MIME selection. The
full Qt test executable also passed on the same Plasma host.
