#pragma once

#ifdef SPEECHER_WITH_QKEYCHAIN
#if __has_include(<qt6keychain/keychain.h>)
#include <qt6keychain/keychain.h>
#else
#include <keychain.h>
#endif

namespace speecher {

inline bool keyringDeletionSucceeded(QKeychain::Error error)
{
    return error == QKeychain::NoError
        || error == QKeychain::EntryNotFound
        || error == QKeychain::NoBackendAvailable;
}

} // namespace speecher
#endif
