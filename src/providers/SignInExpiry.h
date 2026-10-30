#pragma once

#include <QDateTime>
#include <QString>

// When each sign-in's token runs out, as the last read of its credentials
// found it. Reading credentials can block on files, a lock or the macOS
// Keychain, so the GUI thread asks here whether a renewal is due instead.

namespace speecher::SignInExpiry {

// signIn names one credential source; an invalid expiresAt means the token
// read has no expiry, or there was none to read.
void record(const QString &signIn, const QDateTime &expiresAt);
// Whether the token last read for signIn has run out by now. False until a
// read has found one.
bool lapsed(const QString &signIn);

} // namespace speecher::SignInExpiry
