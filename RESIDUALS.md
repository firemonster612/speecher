# Known residuals

- An auto-detected CLI Proxy API auth directory can switch to a higher-priority candidate during a session if account files appear there. Keep the explicit `cliproxy/oauthDir` override set when the directory must remain stable.
- The Vocabulary page's capacity summary counts the shared 100-term/500-token limit. Claude Voice additionally filters the sent keyterms to Latin-1 and a 1,024-byte header, so a list of long or non-Latin-1 terms can carry fewer terms to Claude than the summary states. Codex and refinement are unaffected.
