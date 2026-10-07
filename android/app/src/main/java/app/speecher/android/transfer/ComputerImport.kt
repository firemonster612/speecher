package app.speecher.android.transfer

import app.speecher.android.dictation.SpeecherSettings
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.CustomCleanupLevel
import app.speecher.protocol.CustomTone
import app.speecher.protocol.Replacement
import app.speecher.protocol.VocabularyWord
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import app.speecher.protocol.foldCase
import app.speecher.protocol.normalizedPhrase
import app.speecher.protocol.normalizedVocabulary
import app.speecher.protocol.withCleanupLevel
import app.speecher.protocol.withTone
import java.io.DataInputStream
import java.io.IOException
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.net.URI
import java.net.URLDecoder
import java.security.GeneralSecurityException
import java.util.Base64
import javax.crypto.AEADBadTagException
import javax.crypto.Cipher
import javax.crypto.spec.IvParameterSpec
import javax.crypto.spec.SecretKeySpec
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

// Copying settings from the desktop over the LAN, as docs/adr/0008-phone-transfer-over-the-lan.md
// specifies: the code, the exchange, the bundle and the merge.

/** Why an import stopped, as the import page says it. */
enum class ImportFailure(val title: String, val detail: String = "") {
    NotSpeecherCode("That isn't a Speecher code"),
    OtherVersion(
        "This code is from a different version of Speecher",
        "Update both apps and try again.",
    ),
    Unreachable(
        "Couldn't reach your computer",
        "Your phone and computer need to be on the same Wi-Fi network, and the code works only " +
            "while it is open on the computer.",
    ),
    Unreadable("Couldn't read the settings from your computer"),
    ScannerUnavailable(
        "Couldn't start the code scanner",
        "It needs Google Play services, which may still be downloading it. Try again in a minute.",
    ),
    NoLocalNetwork(
        "Speecher can't reach your network",
        "Allow Speecher to find nearby devices, then scan again.",
    ),
}

class ImportFailed(val failure: ImportFailure, cause: Throwable? = null) :
    Exception(failure.title, cause)

/**
 * What a Speecher code holds: the [addresses] and [port] the computer listens on, the [token] that
 * gets the settings once, the [key] they are sealed with, and the [computer]'s name.
 */
class ImportLink(
    val addresses: List<InetAddress>,
    val port: Int,
    val token: ByteArray,
    val key: ByteArray,
    val computer: String,
)

/**
 * The link a scanned code holds.
 *
 * @throws ImportFailed when [scanned] is not a Speecher code, or one of another version.
 */
fun parseImportLink(scanned: String): ImportLink =
    try {
        val uri = URI.create(scanned)
        require(uri.scheme == "speecher" && uri.host == "import")
        val query =
            uri.rawQuery.orEmpty().split('&').associate {
                it.substringBefore('=') to URLDecoder.decode(it.substringAfter('=', ""), "UTF-8")
            }
        val version = query.getValue("v")
        if (version != "1") throw ImportFailed(ImportFailure.OtherVersion)
        val base64 = Base64.getUrlDecoder()
        ImportLink(
            query.getValue("a").split(',').map(::privateAddress).also {
                require(it.size in 1..MAX_ADDRESSES)
            },
            query.getValue("p").toInt().also { require(it in 1..65535) },
            base64.decode(query.getValue("t")).also { require(it.size == TOKEN_BYTES) },
            base64.decode(query.getValue("k")).also { require(it.size == KEY_BYTES) },
            query.getValue("n"),
        )
    } catch (e: IllegalArgumentException) {
        throw ImportFailed(ImportFailure.NotSpeecherCode, e)
    } catch (e: NoSuchElementException) {
        throw ImportFailed(ImportFailure.NotSpeecherCode, e)
    }

/**
 * [text] as a private (RFC 1918) IPv4 address. Read numerically, so a crafted code can neither make
 * the phone look up a name nor send the token off the local network.
 */
private fun privateAddress(text: String): InetAddress {
    val octets =
        requireNotNull(IPV4.matchEntire(text)) { "Not an IPv4 address: $text" }
            .destructured
            .toList()
            .map(String::toInt)
    require(octets.all { it <= 255 }) { "Not an IPv4 address: $text" }
    return InetAddress.getByAddress(ByteArray(4) { octets[it].toByte() }).also {
        require(it.isSiteLocalAddress) { "Not a private address: $text" }
    }
}

private val IPV4 = Regex("""(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})""")

/**
 * The settings the computer serves for [link], trying each of its addresses in turn.
 *
 * @throws ImportFailed when no address answers, or what comes back does not open or parse.
 */
suspend fun fetchSettings(link: ImportLink): ComputerSettings {
    val sealed = withContext(Dispatchers.IO) { fetchSealed(link) }
    return try {
        parseComputerSettings(String(openSealed(link.key, sealed), Charsets.UTF_8))
    } catch (e: GeneralSecurityException) {
        throw ImportFailed(ImportFailure.Unreadable, e)
    } catch (e: JSONException) {
        throw ImportFailed(ImportFailure.Unreadable, e)
    }
}

private suspend fun fetchSealed(link: ImportLink): ByteArray = coroutineScope {
    var failure: IOException? = null
    for (address in link.addresses) {
        // The computer serves its settings once, so a cancelled import must not reach it again.
        ensureActive()
        try {
            return@coroutineScope Socket().use { socket ->
                // Blocking socket calls ignore cancellation; closing the socket ends them.
                val closer =
                    launch(start = CoroutineStart.UNDISPATCHED) {
                        try {
                            awaitCancellation()
                        } finally {
                            socket.close()
                        }
                    }
                try {
                    socket.connect(InetSocketAddress(address, link.port), CONNECT_TIMEOUT_MS)
                    socket.soTimeout = READ_TIMEOUT_MS
                    ensureActive()
                    socket.getOutputStream().write(link.token)
                    val input = DataInputStream(socket.getInputStream())
                    val length = input.readInt()
                    if (length !in 0..MAX_SEALED_BYTES) {
                        throw IOException("A length of $length bytes")
                    }
                    ByteArray(length).also(input::readFully)
                } finally {
                    closer.cancel()
                }
            }
        } catch (e: IOException) {
            ensureActive()
            failure = e
        }
    }
    throw ImportFailed(ImportFailure.Unreachable, failure)
}

/** The plaintext of [sealed], a nonce, the ciphertext and its tag, as RFC 8439 seals them. */
fun openSealed(key: ByteArray, sealed: ByteArray): ByteArray {
    if (sealed.size < NONCE_BYTES + TAG_BYTES) throw AEADBadTagException("Too short to hold a tag")
    val cipher = Cipher.getInstance("ChaCha20-Poly1305")
    cipher.init(
        Cipher.DECRYPT_MODE,
        SecretKeySpec(key, "ChaCha20"),
        IvParameterSpec(sealed, 0, NONCE_BYTES),
    )
    return cipher.doFinal(sealed, NONCE_BYTES, sealed.size - NONCE_BYTES)
}

/** The dictation setup a computer sends: everything the ADR's bundle carries. */
data class ComputerSettings(
    val computer: String,
    val vocabulary: List<VocabularyWord>,
    val replacements: List<Replacement>,
    val writingProfiles: Map<WritingProfile, WritingProfileSettings>,
    val customTones: List<CustomTone>,
    val customCleanupLevels: List<CustomCleanupLevel>,
    val additionalInstructions: String,
)

/** @throws JSONException when [json] is not a bundle of format 1. */
fun parseComputerSettings(json: String): ComputerSettings {
    val bundle = JSONObject(json)
    val format = bundle.getInt("format")
    if (format != 1) throw JSONException("Bundle format $format, expected 1")
    return ComputerSettings(
        bundle.getString("computer"),
        bundle.objects("vocabulary").map {
            VocabularyWord(
                it.getString("term"),
                it.getString("context"),
                it.getJSONArray("profiles").let { ids ->
                    List(ids.length()) { index -> WritingProfile(ids.getString(index)) }.toSet()
                },
                it.getBoolean("keyTerm"),
                it.getBoolean("priority"),
                it.getString("source"),
                it.getInt("frequency"),
                it.getLong("lastUsedMs"),
            )
        },
        bundle.objects("replacements").map {
            Replacement(it.getString("phrase"), it.getString("text"))
        },
        bundle.objects("writingProfiles").associate {
            WritingProfile(it.getString("id")) to
                WritingProfileSettings(
                        instructions = it.getString("instructions"),
                        name = it.getString("name"),
                        outputLanguage = it.getString("outputLanguage"),
                    )
                    .withCleanupLevel(it.getString("cleanupLevel"))
                    .withTone(it.getString("tone"))
        },
        bundle.objects("customTones").map {
            CustomTone(it.getString("id"), it.getString("name"), it.getString("instruction"))
        },
        bundle.objects("customCleanupLevels").map {
            CustomCleanupLevel(
                it.getString("id"),
                it.getString("name"),
                CleanupStrength.entries.firstOrNull { base -> base.id == it.getString("base") }
                    ?: CleanupStrength.Balanced,
                it.getString("instructions"),
            )
        },
        bundle.getString("additionalInstructions"),
    )
}

private fun JSONObject.objects(key: String): List<JSONObject> =
    getJSONArray(key).let { items: JSONArray -> List(items.length()) { items.getJSONObject(it) } }

/**
 * These settings with [computer]'s merged in, as the ADR's table has it: nothing here is deleted,
 * an item both have takes the computer's version, and the computer's other items are added.
 */
fun SpeecherSettings.withImported(computer: ComputerSettings): SpeecherSettings =
    copy(
            vocabulary =
                normalizedVocabulary(
                    merged(vocabulary, normalizedVocabulary(computer.vocabulary), ::termKey) {
                        phone,
                        incoming ->
                        incoming.copy(
                            source = phone.source,
                            frequency = maxOf(phone.frequency, incoming.frequency),
                            lastUsedMs = maxOf(phone.lastUsedMs, incoming.lastUsedMs),
                        )
                    }
                ),
            replacements =
                merged(replacements, computer.replacements, ::phraseKey) { phone, incoming ->
                    phone.copy(text = incoming.text)
                },
            writingProfiles = writingProfiles + computer.writingProfiles,
            customTones = merged(customTones, computer.customTones, CustomTone::id) { _, it -> it },
            customCleanupLevels =
                merged(customCleanupLevels, computer.customCleanupLevels, CustomCleanupLevel::id) {
                    _,
                    it ->
                    it
                },
            additionalInstructions =
                computer.additionalInstructions.ifEmpty { additionalInstructions },
        )
        .withCustomChoices()
        .withWritingProfiles()

/** [phone] with each item [computer] also has [replaced], then the computer's other items. */
private fun <T> merged(
    phone: List<T>,
    computer: List<T>,
    key: (T) -> Any,
    replaced: (phone: T, computer: T) -> T,
): List<T> {
    val incoming = computer.associateBy(key)
    val present = phone.map(key).toSet()
    return phone.map { item -> incoming[key(item)]?.let { replaced(item, it) } ?: item } +
        computer.filter { key(it) !in present }
}

private fun termKey(word: VocabularyWord) = foldCase(word.term)

private fun phraseKey(rule: Replacement) = normalizedPhrase(rule.phrase)

/** The items an import adds and those it changes, by name. */
data class Changes<T>(val added: List<T> = emptyList(), val updated: List<T> = emptyList()) {
    val isEmpty: Boolean
        get() = added.isEmpty() && updated.isEmpty()
}

/**
 * What importing changes. [additionalInstructions] are the computer's when they replace different
 * ones, else null.
 */
data class ImportPreview(
    val vocabulary: Changes<String>,
    val replacements: Changes<String>,
    val writingProfiles: Changes<WritingProfile>,
    val tonesAndLevels: Changes<String>,
    val additionalInstructions: String?,
) {
    val changesNothing: Boolean
        get() =
            vocabulary.isEmpty &&
                replacements.isEmpty &&
                writingProfiles.isEmpty &&
                tonesAndLevels.isEmpty &&
                additionalInstructions == null
}

/**
 * What importing [computer] into [phone] adds and changes. A word whose use counts alone change is
 * not updated, since the counts are not a setting.
 */
fun importPreview(phone: SpeecherSettings, computer: ComputerSettings): ImportPreview {
    val merged = phone.withImported(computer)
    val tones = changes(phone.customTones, merged.customTones, CustomTone::id, CustomTone::name)
    val levels =
        changes(
            phone.customCleanupLevels,
            merged.customCleanupLevels,
            CustomCleanupLevel::id,
            CustomCleanupLevel::name,
        )
    return ImportPreview(
        changes(phone.vocabulary, merged.vocabulary, ::termKey, VocabularyWord::term) {
            it.copy(source = "", frequency = 0, lastUsedMs = 0)
        },
        changes(phone.replacements, merged.replacements, ::phraseKey, Replacement::phrase),
        changes(
            phone.writingProfiles.toList(),
            merged.writingProfiles.toList(),
            { it.first },
            { it.first },
        ),
        Changes(tones.added + levels.added, tones.updated + levels.updated),
        merged.additionalInstructions.takeIf { it != phone.additionalInstructions },
    )
}

/**
 * The [merged] items not in [phone] by [key], and those whose [setting] differs from the phone's,
 * by [name].
 */
private fun <T, N> changes(
    phone: List<T>,
    merged: List<T>,
    key: (T) -> Any,
    name: (T) -> N,
    setting: (T) -> Any? = { it },
): Changes<N> {
    val before = phone.associateBy(key)
    val (kept, added) = merged.partition { key(it) in before }
    return Changes(
        added.map(name),
        kept.filter { setting(before.getValue(key(it))) != setting(it) }.map(name),
    )
}

private const val MAX_ADDRESSES = 4
private const val TOKEN_BYTES = 16
private const val KEY_BYTES = 32
private const val NONCE_BYTES = 12
private const val TAG_BYTES = 16
private const val CONNECT_TIMEOUT_MS = 3_000
private const val READ_TIMEOUT_MS = 10_000
private const val MAX_SEALED_BYTES = 4 * 1024 * 1024
