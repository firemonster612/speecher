package app.speecher.android.auth

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import androidx.core.content.edit
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SignInRequired
import app.speecher.android.dictation.oauth
import app.speecher.protocol.OAuthHttpException
import app.speecher.protocol.OAuthProvider
import app.speecher.protocol.OAuthTokens
import app.speecher.protocol.refreshTokens
import java.security.KeyStore
import java.util.Base64
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec
import okhttp3.OkHttpClient
import org.json.JSONObject

class TokenStore(context: Context) {
    private val preferences = context.getSharedPreferences("accounts", Context.MODE_PRIVATE)
    private val key: SecretKey by lazy {
        val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (store.getKey("speecher-oauth", null) as? SecretKey)
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

    fun signedIn(): Set<Provider> =
        Provider.entries.filterTo(mutableSetOf()) { load(it.oauth) != null }

    fun save(provider: OAuthProvider, tokens: OAuthTokens) {
        val plain =
            JSONObject()
                .put("access", tokens.accessToken)
                .put("refresh", tokens.refreshToken)
                .put("id", tokens.idToken)
                .put("expiry", tokens.expiresAtMillis)
                .put("scope", tokens.scope)
                .toString()
                .toByteArray(Charsets.UTF_8)
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, key)
        val encrypted = cipher.doFinal(plain)
        val value = Base64.getEncoder().encodeToString(cipher.iv + encrypted)
        preferences.edit(commit = true) { putString(provider.name, value) }
    }

    fun load(provider: OAuthProvider): OAuthTokens? {
        val stored = preferences.getString(provider.name, null) ?: return null
        return try {
            val bytes = Base64.getDecoder().decode(stored)
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(128, bytes.copyOfRange(0, 12)))
            val data =
                JSONObject(
                    String(cipher.doFinal(bytes.copyOfRange(12, bytes.size)), Charsets.UTF_8)
                )
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

    fun signOut(provider: OAuthProvider) {
        preferences.edit(commit = true) { remove(provider.name) }
    }

    /** Call on a worker thread before a provider request. */
    @Synchronized
    fun validTokens(provider: OAuthProvider, http: OkHttpClient): OAuthTokens? {
        val stored = load(provider) ?: return null
        if (stored.expiresAtMillis - 60_000 > System.currentTimeMillis()) return stored
        val refreshed =
            try {
                refreshTokens(http, provider, stored)
            } catch (error: OAuthHttpException) {
                if (error.status == 400 || error.status == 401) throw SignInRequired()
                throw error
            }
        save(provider, refreshed)
        return refreshed
    }
}
