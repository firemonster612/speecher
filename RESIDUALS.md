# Known residuals

- An auto-detected CLI Proxy API auth directory can switch to a higher-priority candidate during a session if account files appear there. Keep the explicit `cliproxy/oauthDir` override set when the directory must remain stable.
