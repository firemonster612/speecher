package app.speecher.android.auth

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import androidx.core.content.edit
import app.speecher.android.provider.Provider
import app.speecher.android.provider.oauth
import app.speecher.protocol.OAuthHttpException
import app.speecher.protocol.OAuthProvider
import app.speecher.protocol.OAuthTokens
import app.speecher.protocol.ProviderFailure
import app.speecher.protocol.ProviderFailureKind
import app.speecher.protocol.failureKindForHttpStatus
import app.speecher.protocol.refreshTokens
import java.security.KeyStore
import java.util.Base64
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec
import okhttp3.OkHttpClient
import org.json.JSONObject

/** A key the person typed for a server or an API, by the slot it is saved under. */
enum class ApiKey {
    SpeechEndpoint
}

/**
 * Sign-ins and [ApiKey]s, encrypted with [secretKey]: an Android Keystore key that never leaves the
 * keystore, which tests replace.
 */
class TokenStore(context: Context, secretKey: Lazy<SecretKey> = lazy(::keystoreKey)) {
    private val preferences = context.getSharedPreferences("accounts", Context.MODE_PRIVATE)
    private val key: SecretKey by secretKey

    /**
     * The accounts with a saved sign-in. Read without decrypting, so the chip tap that resolves the
     * providers waits on no Keystore; a sign-in the Keystore can no longer decrypt still counts,
     * and the dictation that uses it asks for a sign-in.
     */
    fun signedIn(): Set<Provider> =
        Provider.entries.filterTo(mutableSetOf()) { preferences.contains(it.oauth.name) }

    fun save(provider: OAuthProvider, tokens: OAuthTokens) {
        val value =
            encrypt(
                JSONObject()
                    .put("access", tokens.accessToken)
                    .put("refresh", tokens.refreshToken)
                    .put("id", tokens.idToken)
                    .put("expiry", tokens.expiresAtMillis)
                    .put("scope", tokens.scope)
                    .toString()
            )
        synchronized(signIns) {
            preferences.edit(commit = true) {
                putString(provider.name, value)
                remove(sessionEndedKey(provider))
            }
        }
    }

    fun load(provider: OAuthProvider): OAuthTokens? {
        val stored = preferences.getString(provider.name, null) ?: return null
        return try {
            val data = JSONObject(decrypt(stored))
            OAuthTokens(
                data.getString("access"),
                data.getString("refresh"),
                data.getString("id"),
                data.getLong("expiry"),
                data.optString("scope"),
            )
        } catch (_: Exception) {
            null
        }
    }

    /** The key saved in [slot], or empty when there is none or it can no longer be read. */
    fun apiKey(slot: ApiKey): String =
        preferences.getString(slot.preference, null)?.let {
            runCatching { decrypt(it) }.getOrNull()
        } ?: ""

    /**
     * Saves [key] in [slot] encrypted, or clears the slot when it is empty. Throws when it can't.
     */
    fun saveApiKey(slot: ApiKey, key: String) {
        preferences.edit(commit = true) {
            if (key.isEmpty()) remove(slot.preference) else putString(slot.preference, encrypt(key))
        }
    }

    private val ApiKey.preference: String
        get() = "api-key-$name"

    /** [plain] encrypted with the store's key, for a secret kept outside it. */
    internal fun encrypt(plain: String): String {
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, key)
        val encrypted = cipher.doFinal(plain.toByteArray(Charsets.UTF_8))
        return Base64.getEncoder().encodeToString(cipher.iv + encrypted)
    }

    /** What [encrypt] encrypted; throws when the key can't read [stored]. */
    internal fun decrypt(stored: String): String {
        val bytes = Base64.getDecoder().decode(stored)
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(128, bytes.copyOfRange(0, 12)))
        return String(cipher.doFinal(bytes.copyOfRange(12, bytes.size)), Charsets.UTF_8)
    }

    fun signOut(provider: OAuthProvider) =
        synchronized(signIns) {
            preferences.edit(commit = true) {
                remove(provider.name)
                remove(sessionEndedKey(provider))
            }
        }

    /**
     * Providers that rejected their saved sign-in during a dictation, until it is replaced or a
     * later dictation is accepted.
     */
    fun sessionEnded(): Set<Provider> =
        Provider.entries.filterTo(mutableSetOf()) {
            preferences.getBoolean(sessionEndedKey(it.oauth), false)
        }

    /**
     * Marks [provider]'s session ended because it turned down the [rejected] sign-in, unless
     * another sign-in replaced that one since.
     */
    fun endSession(provider: OAuthProvider, rejected: OAuthTokens) =
        synchronized(signIns) {
            if (load(provider) != rejected) return@synchronized
            preferences.edit(commit = true) { putBoolean(sessionEndedKey(provider), true) }
        }

    fun clearSessionEnded(provider: OAuthProvider) {
        val key = sessionEndedKey(provider)
        if (preferences.contains(key)) preferences.edit(commit = true) { remove(key) }
    }

    private fun sessionEndedKey(provider: OAuthProvider) = "${provider.name}-session-ended"

    /**
     * Call on a worker thread before a provider request. A refresh the token endpoint turns down
     * fails as a rejected sign-in; one that fails for no reason of its own still leaves no usable
     * sign-in, as on the desktop, and a turned-down refresh ends the session. One refresh per
     * provider runs at a time across every store, so a rotating refresh token is spent once; a
     * sign-out or sign-in during it wins over its result.
     */
    fun validTokens(provider: OAuthProvider, http: OkHttpClient): OAuthTokens? {
        synchronized(refreshes.getValue(provider)) {
            val stored = load(provider) ?: return null
            if (stored.expiresAtMillis - 60_000 > System.currentTimeMillis()) return stored
            val refreshed =
                try {
                    refreshTokens(http, provider, stored)
                } catch (error: OAuthHttpException) {
                    val kind =
                        if (error.status == 400 || error.status == 401)
                            ProviderFailureKind.Authentication
                        else
                            failureKindForHttpStatus(error.status).takeUnless {
                                it == ProviderFailureKind.Other
                            } ?: ProviderFailureKind.Unavailable
                    if (kind == ProviderFailureKind.Authentication) endSession(provider, stored)
                    throw ProviderFailure(kind, "Sign-in refresh failed", error.status, error)
                }
            synchronized(signIns) {
                val now = load(provider)
                if (now != stored) return now
                save(provider, refreshed)
            }
            return refreshed
        }
    }

    private companion object {
        /** Held through each provider's refresh, its request included. */
        val refreshes = OAuthProvider.entries.associateWith { Any() }
        /**
         * Held for each sign-in write, and by a refresh from checking its sign-in is still the one
         * saved until it saves the result. Never held through a request, so a sign-out never waits.
         */
        val signIns = Any()
    }
}

private fun keystoreKey(): SecretKey {
    val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
    return (store.getKey("speecher-oauth", null) as? SecretKey)
        ?: KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
            .apply {
                init(
                    KeyGenParameterSpec.Builder(
                            "speecher-oauth",
                            KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
                        )
                        .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                        .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                        .build()
                )
            }
            .generateKey()
}
