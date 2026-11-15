package app.speecher.android.transfer

import app.speecher.android.settings.SpeecherSettings
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.RefinementRule
import app.speecher.protocol.Replacement
import app.speecher.protocol.Tone
import app.speecher.protocol.VocabularyWord
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import java.io.DataOutputStream
import java.net.InetAddress
import java.net.ServerSocket
import java.net.SocketTimeoutException
import java.util.concurrent.CountDownLatch
import javax.crypto.AEADBadTagException
import kotlin.concurrent.thread
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

// Robolectric for org.json and Conscrypt, the JSON parser and crypto provider Android has.
@RunWith(RobolectricTestRunner::class)
class ComputerImportTest {
    private val key = ByteArray(32) { it.toByte() }
    private val token = ByteArray(16) { it.toByte() }

    @Test
    fun `a Speecher code gives where the computer listens, the token, the key and its name`() {
        val link =
            parseImportLink(
                "speecher://import?v=2&a=192.168.1.20,10.0.0.5&p=53817&t=AAECAwQFBgcICQoLDA0ODw" +
                    "&k=AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8&n=enzo%27s%20thinkpad",
            )
        assertEquals(listOf("192.168.1.20", "10.0.0.5"), link.addresses.map { it.hostAddress })
        assertEquals(53817, link.port)
        assertArrayEquals(token, link.token)
        assertArrayEquals(key, link.key)
        assertEquals("enzo's thinkpad", link.computer)
    }

    @Test
    fun `any other text is not a Speecher code, and a code of another version says so`() {
        assertEquals(ImportFailure.NotSpeecherCode, failureOf("https://example.com/?v=2"))
        assertEquals(
            ImportFailure.NotSpeecherCode,
            failureOf("speecher://import?v=2&a=10.0.0.5&p=53817&t=AAEC&k=AAEC&n=pc"),
        )
        assertEquals(ImportFailure.OtherVersion, failureOf("speecher://import?v=1&x=1"))
    }

    @Test
    fun `a code must point at one to four private IPv4 addresses`() {
        val rest =
            "&p=53817&t=AAECAwQFBgcICQoLDA0ODw&k=AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8&n=pc"
        for (addresses in
            listOf(
                "enzo-thinkpad.local",
                "8.8.8.8",
                "172.32.0.1",
                "10.0.0.1,10.0.0.2,10.0.0.3,10.0.0.4,10.0.0.5",
            )) {
            assertEquals(
                addresses,
                ImportFailure.NotSpeecherCode,
                failureOf("speecher://import?v=2&a=$addresses$rest"),
            )
        }
        assertEquals(
            listOf("172.16.0.1", "172.31.255.254", "10.0.0.3", "192.168.0.4"),
            parseImportLink(
                    "speecher://import?v=2&a=172.16.0.1,172.31.255.254,10.0.0.3,192.168.0.4$rest",
                )
                .addresses
                .map { it.hostAddress },
        )
    }

    @Test
    fun `sealed settings open with the key from the code`() {
        assertEquals(
            """{"format":1,"computer":"test"}""",
            String(
                openSealed(
                    key,
                    hex(
                        "a0a1a2a3a4a5a6a7a8a9aaab77891e303f8ba3d98235c238de999296ed2ba7da31485a81" +
                            "c0bda8c5531f993f914144b39ebecba5459cac59a143",
                    ),
                ),
            ),
        )
    }

    @Test
    fun `sealed settings with one byte altered do not open`() {
        val sealed =
            hex(
                "a0a1a2a3a4a5a6a7a8a9aaab77891e303f8ba3d98235c238de999296ed2ba7da31485a81" +
                    "c0bda8c5531f993f914144b39ebecba5459cac59a143",
            )
        sealed[20] = (sealed[20].toInt() xor 1).toByte()
        assertThrows(AEADBadTagException::class.java) { openSealed(key, sealed) }
    }

    @Test
    fun `a bundle reads as the computer's settings`() {
        // What the desktop's phoneTransferBundle makes of its test settings.
        val bundle =
            """{"computer":"desk","format":2,""" +
                """"replacements":[{"phrase":"my address","text":"Flat 3"}],""" +
                """"rules":[{"everyProfile":true,"id":"custom_rule_british",""" +
                """"instruction":"Use British spelling.","name":"British"},""" +
                """{"everyProfile":false,"id":"custom_rule_short","instruction":"",""" +
                """"name":"Short"}],""" +
                """"vocabulary":[{"context":"The container platform.","frequency":41,""" +
                """"keyTerm":true,"lastUsedMs":1790000000000,"priority":true,""" +
                """"profiles":["work"],"source":"manual","term":"Kubernetes"}],""" +
                """"writingProfiles":[{"cleanupLevel":"strong_polish","id":"work",""" +
                """"name":"","outputLanguage":"",""" +
                """"rules":{"custom_rule_short":true,"headings":false},""" +
                """"tone":"formal"}]}"""
        assertEquals(
            ComputerSettings(
                "desk",
                listOf(
                    VocabularyWord(
                        "Kubernetes",
                        "The container platform.",
                        setOf(WritingProfile.Work),
                        keyTerm = true,
                        priority = true,
                        source = "manual",
                        frequency = 41,
                        lastUsedMs = 1790000000000,
                    ),
                ),
                listOf(Replacement("my address", "Flat 3")),
                mapOf(
                    WritingProfile.Work to
                        WritingProfileSettings(
                            CleanupStrength.StrongPolish,
                            Tone.Formal,
                            mapOf("custom_rule_short" to true, "headings" to false),
                        ),
                ),
                listOf(
                    RefinementRule("custom_rule_british", "British", "Use British spelling.", true),
                    RefinementRule("custom_rule_short", "Short"),
                ),
            ),
            parseComputerSettings(bundle),
        )
    }

    @Test
    fun `importing keeps everything on the phone and takes the computer's version of shared items`() {
        val old = WritingProfile("custom_old")
        val standup = WritingProfile("custom_standup")
        val phone =
            SpeecherSettings(
                vocabulary =
                    listOf(
                        VocabularyWord("kubernetes", frequency = 50, lastUsedMs = 100),
                        VocabularyWord("Grafana", frequency = 3, source = "csv"),
                        VocabularyWord("Phone only"),
                    ),
                replacements =
                    listOf(Replacement("My Address", "Old flat"), Replacement("shrug", ":/")),
                writingProfiles =
                    WritingProfile.entries.associateWith { WritingProfileSettings() } +
                        (old to WritingProfileSettings(name = "Old")),
                refinementRules =
                    listOf(
                        RefinementRule("custom_rule_dry", "Dry", "Plain."),
                        RefinementRule("custom_rule_mine", "Mine"),
                    ),
            )
        val work = WritingProfileSettings(rules = mapOf("custom_rule_dry" to true))
        val computer =
            ComputerSettings(
                "enzo-thinkpad",
                listOf(
                    VocabularyWord(
                        "Kubernetes",
                        "The container platform.",
                        setOf(WritingProfile.Work),
                        frequency = 41,
                        lastUsedMs = 200,
                    ),
                    VocabularyWord("Grafana", frequency = 9),
                    VocabularyWord("Aoife  Byrne"),
                ),
                listOf(
                    Replacement("my address!", "Flat 3"),
                    Replacement("shrug", ":/"),
                    Replacement("email sign off", "Best, Enzo"),
                ),
                mapOf(
                    WritingProfile.Work to work,
                    WritingProfile.Email to WritingProfileSettings(),
                    standup to WritingProfileSettings(name = "Standup"),
                ),
                listOf(
                    RefinementRule("custom_rule_dry", "Dry", "Understated."),
                    RefinementRule("custom_rule_loose", "Loose", everyProfile = true),
                ),
            )

        assertEquals(
            phone.copy(
                vocabulary =
                    listOf(
                        VocabularyWord(
                            "Kubernetes",
                            "The container platform.",
                            setOf(WritingProfile.Work),
                            frequency = 50,
                            lastUsedMs = 200,
                        ),
                        VocabularyWord("Grafana", frequency = 9, source = "csv"),
                        VocabularyWord("Aoife Byrne"),
                        VocabularyWord("Phone only"),
                    ),
                replacements =
                    listOf(
                        Replacement("My Address", "Flat 3"),
                        Replacement("shrug", ":/"),
                        Replacement("email sign off", "Best, Enzo"),
                    ),
                writingProfiles =
                    phone.writingProfiles +
                        mapOf(
                            WritingProfile.Work to work,
                            standup to WritingProfileSettings(name = "Standup"),
                        ),
                refinementRules =
                    listOf(
                        RefinementRule("custom_rule_dry", "Dry", "Understated."),
                        RefinementRule("custom_rule_mine", "Mine"),
                        RefinementRule("custom_rule_loose", "Loose", everyProfile = true),
                    ),
            ),
            phone.withImported(computer),
        )
        // Grafana's use count alone and Email's identical settings are no update.
        assertEquals(
            ImportPreview(
                vocabulary = Changes(added = listOf("Aoife Byrne"), updated = listOf("Kubernetes")),
                replacements =
                    Changes(added = listOf("email sign off"), updated = listOf("My Address")),
                writingProfiles =
                    Changes(added = listOf(standup), updated = listOf(WritingProfile.Work)),
                rules = Changes(added = listOf("Loose"), updated = listOf("Dry")),
            ),
            importPreview(phone, computer),
        )
    }

    @Test
    fun `fetching sends the token and opens the length-prefixed settings the computer sends back`() {
        val sealed =
            hex(
                "b0b1b2b3b4b5b6b7b8b9babbec5f47c78f85e8f3614ca4aa8d29bc5d5eb526c3f9c707e62a7078" +
                    "07d976a2486530b9c6538e2fc7db68e6609ee78ab346c4f9cb34e411034084dc36c580cc0cc889" +
                    "c58a5c4edc1a241525ac07a299ef42f109ce784bc3d097b4d1ae40d4d44b9897dc6a5a2b731239" +
                    "0ccc38e5cc47bbb64b79ebd84a70fe215f80341fabd01f52afb415a01253027b2f76298522510f" +
                    "da269c51c954fa11052a67208a5b409efff4a9cb6d42882e2c7cc39289ac79b5fafd91d54cdb00" +
                    "926e0e9330dbb77e72f29dca54930f0414c3f65a08b53b004455d12cb998dc2c5e9e5a306f770f" +
                    "a2b7",
            )
        ServerSocket(0, 1, InetAddress.getLoopbackAddress()).use { server ->
            val desktop = thread {
                server.accept().use { phone ->
                    val received = phone.getInputStream().readNBytes(16)
                    if (received.contentEquals(token))
                        DataOutputStream(phone.getOutputStream()).apply {
                            writeInt(sealed.size)
                            write(sealed)
                        }
                }
            }
            val settings = runBlocking {
                fetchSettings(
                    ImportLink(listOf(server.inetAddress), server.localPort, token, key, "pc"),
                )
            }
            desktop.join()
            assertEquals(
                ComputerSettings(
                    "enzo-thinkpad",
                    emptyList(),
                    emptyList(),
                    emptyMap(),
                    listOf(
                        RefinementRule(
                            "custom_rule_british",
                            "British",
                            "Use British spelling.",
                            true,
                        ),
                    ),
                ),
                settings,
            )
        }
    }

    @Test
    fun `cancelling while the computer holds the connection stops before the next address`() {
        val first = ServerSocket(0, 1, InetAddress.getByName("127.0.0.1"))
        val second = ServerSocket(first.localPort, 1, InetAddress.getByName("127.0.0.2"))
        first.use {
            second.use {
                val tokenSent = CountDownLatch(1)
                thread {
                    first.accept().use { phone ->
                        phone.getInputStream().readNBytes(16)
                        tokenSent.countDown()
                        phone.getInputStream().read()
                    }
                }
                val link =
                    ImportLink(
                        listOf(first.inetAddress, second.inetAddress),
                        first.localPort,
                        token,
                        key,
                        "pc",
                    )
                runBlocking {
                    val fetch = launch(Dispatchers.IO) { fetchSettings(link) }
                    tokenSent.await()
                    // Well under the read timeout, so only closing the socket ends it in time.
                    withTimeout(2_000) { fetch.cancelAndJoin() }
                }
                second.soTimeout = 100
                assertThrows(SocketTimeoutException::class.java) { second.accept() }
            }
        }
    }

    private fun failureOf(scanned: String): ImportFailure? =
        try {
            parseImportLink(scanned)
            null
        } catch (failed: ImportFailed) {
            failed.failure
        }

    private fun hex(text: String) =
        ByteArray(text.length / 2) { text.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
}
