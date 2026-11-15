# Copy settings to the phone over the LAN

Status: accepted, 2026-10-07. Amended 2026-11-15 by
[0011](0011-refinement-rules.md): format 2 carries rules.

## Context

People set up vocabulary, snippets and Writing Profiles on the desktop and
then have to type them again on the Android app. A QR code is the obvious
hand-off, but a QR code holds at most 2,953 bytes, and a code that dense is
hard to scan off a laptop screen. Measured bundles of a typical setup (120
terms, 20 snippets) are 10 to 17 KB of JSON and 2 to 7 KB deflated; a heavy
one (400 terms, 60 snippets) is 28 to 54 KB and 4 to 20 KB deflated.
Compression alone does not fit, and a sequence of codes needs a continuous
camera scanner and a camera permission on the phone.

## Decision

**The QR code carries an address and a key, not the settings.** The desktop
shows a code while its "Copy settings to your phone" dialog is open. The
phone scans it with Google's code scanner, which needs no camera permission,
connects to the desktop over TCP, and receives the settings encrypted with
the key from the code.

- The desktop listens only while the dialog is open, on a port the system
  picks, and serves the bundle once. Closing the dialog closes the port.
- The code is the only secret. A neighbour on the same Wi-Fi sees ciphertext,
  and cannot use up the one serving without the token.
- Only the user's own dictation setup moves: vocabulary, replacements and
  snippets, Writing Profiles and the person's refinement rules. Never
  accounts, tokens or keys.
- The phone merges: nothing on it is deleted, and where both have an item the
  computer's version replaces the phone's.
- Only desktop to phone. The other direction has no use yet.

The desktop vendors two single-file libraries through FetchContent: Nayuki's
QR Code generator (MIT) to draw the code and Monocypher (CC0) for
ChaCha20-Poly1305, which Qt lacks. Android uses `javax.crypto`'s
ChaCha20-Poly1305, built in from API 28.

## Wire format

Version 2. Both sides treat anything else as an error, so a phone whose app
reads another version says so. Version 1 carried custom
tones, custom cleanup levels and Additional Instructions, which rules
replaced.

**The code** is a URI:

```
speecher://import?v=2&a=192.168.1.20,10.0.0.5&p=53817&t=<token>&k=<key>&n=<name>
```

- `a`: the desktop's private IPv4 addresses on interfaces that are up, comma
  separated, at most four: Wi-Fi first, then wired, then container and VM
  bridges, which can carry the real LAN address. The phone tries each in
  turn.
- `p`: the TCP port.
- `t`: 16 random bytes, base64url without padding.
- `k`: 32 random bytes, the ChaCha20-Poly1305 key, base64url without padding.
- `n`: the computer's host name, percent-encoded, shown while fetching.

**The exchange**, over one TCP connection:

1. The phone sends the 16 token bytes.
2. The desktop compares them in constant time. On a mismatch, or when it
   has already served the bundle, it closes the connection.
3. Otherwise it sends a 4-byte big-endian length, then that many bytes:
   a 12-byte nonce, the ciphertext, and the 16-byte Poly1305 tag (RFC 8439,
   no associated data). It then closes the connection and stops listening.

The desktop drops a connection that has not sent its token within 10 seconds,
and keeps at most 8 connections open at once, closing any beyond that as soon
as it arrives. The phone gives each address 3 seconds to connect and the read
10 seconds. A length over 4 MiB is an error.

Once the token matches, the code is spent even if the send fails. If the
phone disconnects before the desktop has written the whole bundle, or the
write has not finished within 30 seconds, the desktop drops the connection and
the dialog says the phone lost the connection and to open it again for a new
code. The desktop never reopens the port for the same code.

**The bundle** is the decrypted UTF-8 JSON object. Every key is present.

```json
{
  "format": 2,
  "computer": "desk",
  "vocabulary": [
    {"term": "Kubernetes", "context": "The container platform.", "profiles": ["work"],
     "keyTerm": true, "priority": true, "source": "manual", "frequency": 41,
     "lastUsedMs": 1790000000000}
  ],
  "replacements": [{"phrase": "my address", "text": "Flat 3"}],
  "writingProfiles": [
    {"id": "work", "name": "", "cleanupLevel": "strong_polish", "tone": "formal",
     "outputLanguage": "", "rules": {"custom_rule_short": true, "headings": false}}
  ],
  "rules": [
    {"id": "custom_rule_british", "name": "British", "instruction": "Use British spelling.",
     "everyProfile": true},
    {"id": "custom_rule_short", "name": "Short", "instruction": "", "everyProfile": false}
  ]
}
```

`priority` is the desktop's `starred`. `cleanupLevel` and `tone` hold a
built-in id, as the desktop stores them. A profile's `rules` holds only the
rules it switched away from their default, by id, built-in rules and the
person's alike. `writingProfiles` lists every profile, built-in and custom;
`name` is empty for a built-in. `rules` lists the person's rules in their
order.

**Merging on the phone**, item by item:

| Item | Same item when | The computer's version |
| --- | --- | --- |
| Vocabulary Entry | terms match ignoring case and spacing | replaces term, context, profiles, key term and priority; frequency and last use keep the larger |
| Replacement | spoken phrases match as `normalizedPhrase` | replaces the text |
| Writing Profile | ids match | replaces every setting |
| Rule | ids match | replaces it whole |

## Consequences

- Both devices must be on the same network. Guest and corporate Wi-Fi that
  isolate clients block the transfer, and the phone says so.
- A network that gives the computer only a public or carrier-grade NAT
  (100.64.0.0/10) address shows no code, because the code carries only
  private addresses.
- Windows asks for firewall permission the first time the dialog opens.
- The QR code is drawn black on white in every theme, the one place the
  desktop does not use palette roles, because scanners read dark-on-light
  codes most reliably.
- Phones without Google Play services cannot scan the code.
- On Android 17 and later the app needs the local network permission
  (`ACCESS_LOCAL_NETWORK`, shown as Nearby devices) before any socket to the
  computer's private address opens, so it asks before the first scan. Without
  it the import page says so and opens the app's settings.
- Learned corrections and app-specific paste rules have no counterpart on the
  phone, and the desktop's application recognition rules name desktop apps.
  All three stay on the desktop, and the dialog lists them.
