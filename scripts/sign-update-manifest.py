#!/usr/bin/env python3
"""Signs the Linux and Windows update manifest with the Sparkle EdDSA key.

  SPARKLE_ED_PRIVATE_KEY=... SPARKLE_ED_PUBLIC_KEY=... \\
      scripts/sign-update-manifest.py MANIFEST SIGNED_MANIFEST

SIGNED_MANIFEST is the base64 Ed25519 signature, a newline, then MANIFEST's
bytes unchanged. The signature covers PREFIX followed by those bytes, so it can
never pass for a Sparkle signature over a DMG, nor a DMG's for it
(docs/adr/0011-signed-update-manifest.md). ManifestUpdater checks it.

The private key is the one `generate_keys -x` exports: base64 of a 32-byte
seed, or of a legacy 96-byte key (the 64-byte expanded key, then the public
key). Common Ed25519 libraries take only the seed, so this signs with the
RFC 8032 arithmetic directly, and refuses a key whose public half is not
SPARKLE_ED_PUBLIC_KEY.
"""

import base64
import hashlib
import os
import sys

# Must match manifestSignaturePrefix in src/app/ManifestUpdater.cpp.
PREFIX = b"Speecher update manifest v1\n"

# Ed25519 over edwards25519, as RFC 8032 section 6 writes it.
P = 2**255 - 19
Q = 2**252 + 27742317777372353535851937790883648493
D = -121665 * pow(121666, -1, P) % P


def point_add(a, b):
    e = (a[1] - a[0]) * (b[1] - b[0]) % P
    h = (a[1] + a[0]) * (b[1] + b[0]) % P
    c = 2 * a[3] * b[3] * D % P
    d = 2 * a[2] * b[2] % P
    e, f, g, h = h - e, d - c, d + c, h + e
    return (e * f, g * h, f * g, e * h)


def point_mul(scalar, point):
    result = (0, 1, 1, 0)
    while scalar:
        if scalar & 1:
            result = point_add(result, point)
        point = point_add(point, point)
        scalar >>= 1
    return result


def point_compress(point):
    z_inverse = pow(point[2], -1, P)
    x, y = point[0] * z_inverse % P, point[1] * z_inverse % P
    return (y | (x & 1) << 255).to_bytes(32, "little")


def base_point():
    y = 4 * pow(5, -1, P) % P
    x2 = (y * y - 1) * pow(D * y * y + 1, -1, P) % P
    x = pow(x2, (P + 3) // 8, P)
    if (x * x - x2) % P:
        x = x * pow(2, (P - 1) // 4, P) % P
    if x & 1:
        x = P - x
    return (x, y, 1, x * y % P)


def expanded_key(secret):
    if len(secret) == 32:
        return hashlib.sha512(secret).digest()
    if len(secret) == 96:
        return secret[:64]
    sys.exit(f"SPARKLE_ED_PRIVATE_KEY decodes to {len(secret)} bytes; expected a 32-byte seed or a 96-byte legacy key.")


def sign(secret, message):
    """The Ed25519 signature of message, and the public key it verifies with."""
    expanded = expanded_key(secret)
    scalar = int.from_bytes(expanded[:32], "little") & (2**254 - 8) | 2**254
    public = point_compress(point_mul(scalar, base_point()))
    nonce = int.from_bytes(hashlib.sha512(expanded[32:] + message).digest(), "little") % Q
    commitment = point_compress(point_mul(nonce, base_point()))
    challenge = int.from_bytes(hashlib.sha512(commitment + public + message).digest(), "little") % Q
    return commitment + ((nonce + challenge * scalar) % Q).to_bytes(32, "little"), public


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    private_key = os.environ.get("SPARKLE_ED_PRIVATE_KEY", "").strip()
    public_key = os.environ.get("SPARKLE_ED_PUBLIC_KEY", "").strip()
    if not private_key or not public_key:
        sys.exit("SPARKLE_ED_PRIVATE_KEY and SPARKLE_ED_PUBLIC_KEY are required to sign the update manifest.")
    manifest = open(sys.argv[1], "rb").read()
    signature, derived = sign(base64.b64decode(private_key, validate=True), PREFIX + manifest)
    if derived != base64.b64decode(public_key, validate=True):
        sys.exit("SPARKLE_ED_PRIVATE_KEY does not belong to SPARKLE_ED_PUBLIC_KEY.")
    with open(sys.argv[2], "wb") as output:
        output.write(base64.b64encode(signature) + b"\n" + manifest)


if __name__ == "__main__":
    main()
