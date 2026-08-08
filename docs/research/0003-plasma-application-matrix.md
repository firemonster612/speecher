# Plasma application compatibility check

Status: live check, 2026-07-29

Matrix ticket: [Choose the first Plasma application compatibility matrix](https://github.com/firemonster612/speecher/issues/6)

The checks below ran in a Plasma 6 Wayland session on `cachy`. Test applications used temporary documents or profiles. The existing Helium session was left alone; a separate temporary Helium profile was used for the check.

| Application | Target seen through AT-SPI | Category and profile | Nearby text | Direct insertion |
| --- | --- | --- | --- | --- |
| Kate | `kate`, focused role `list item` | Code editor, Technical | Not exposed by the focused object | No |
| Konsole | `konsole`, focused role `terminal` | Terminal, Technical | Exposed around the cursor | No |
| Firefox | `firefox`, focused browser control | Browser, configured default profile | Not exposed in this check | No |
| Helium | `helium`, focused role `entry` | Browser, configured default profile | Exposed after the caret | No |
| LibreOffice Writer | `soffice.bin`, focused role `page tab list` | Office, configured default profile | Not exposed by the focused object | No |
| Thunderbird | Not installed on the test host | Email, Email | Not live-tested | Not live-tested |
| T3 Code | Not installed on the test host | Code editor, Technical | Not live-tested | Not live-tested |
| Qt password fixture | Secure password-text target | Secure deny case | Withheld | Refused |

The Kate result exposed an important fallback case: an application can be known even when its focused control does not implement the AT-SPI text interfaces. Speecher now keeps that application identity so exact-app Paste Rules and profile inference still work. It does not invent caret or text context, and it does not offer direct insertion for that control.

The Qt fixture also confirmed the privacy boundary in a live session. Speecher identified the control as secure, returned no nearby text, and refused direct insertion.

Plain text, sanitized HTML clipboard data, Paste Rule precedence, delivery receipts, clipboard restoration, and correction-learning gates are covered by the automated suite. Their behavior is shared across applications. A result is only called `Verified in Target` when bounded AT-SPI readback finds the inserted text; otherwise Speecher reports `Input sent` or `Copied`. Rich-editor autoformatting still belongs to the target application and is not treated as verified Speecher formatting.

AT-SPI exposure depends on the application and sometimes on how it was launched. The browser checks used accessibility-enabled temporary profiles. On a session where an application does not expose an accessibility tree, Speecher falls back to clipboard delivery instead of guessing.
