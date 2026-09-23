package app.speecher.protocol

import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertNotNull
import org.junit.jupiter.api.Assertions.assertThrows
import org.junit.jupiter.api.Test

class TlsFingerprintTest {
    @Test
    fun `OpenSSL wire policy matches the captured Python OpenSSL 3_6 ClientHello`() {
        val (ciphers, extensions) = capture(OpenSslFingerprint)
        // Literal reference from ssl.create_default_context with ALPN http/1.1.
        // Certificate compression (27) is intentionally absent: BC cannot decode it.
        assertEquals(
            "130213031301c02cc030009fcca9cca8ccaac02bc02f009ec024c028006bc023c0270067c00ac0140039c009c0130033009d009c003d003c0035002f",
            ciphers,
        )
        assertEquals(
            listOf(65281, 0, 11, 10, 35, 16, 22, 23, 13, 43, 45, 51),
            extensions.keys.toList(),
        )
        assertEquals("001011ec001d0017001e0018001901000101", extensions[10])
        assertEquals(
            "003409050906090404030503060308070808081a081b081c0809080a080b080408050806040105010601030303010302040205020602",
            extensions[13],
        )
        assertEquals("000908687474702f312e31", extensions[16])
        assertEquals("0403040303", extensions[43])
        assertEquals("04e811ec04c0", extensions.getValue(51).take(12))
        assertEquals(2516, extensions.getValue(51).length)
    }

    @Test
    fun `Chrome stub is selectable and emits its own groups and key share`() {
        val (_, extensions) = capture(TlsFingerprints.named("chrome"))
        assertEquals("0006001d00170018", extensions[10])
        assertEquals("0024001d0020", extensions.getValue(51).take(12))
    }

    private fun capture(profile: TlsFingerprint): Pair<String, Map<Int, String>> {
        var hello: ByteArray? = null
        // BC emits its actual ClientHello before it encounters the empty server input.
        assertThrows(java.io.IOException::class.java) {
            connectTls(
                byteArrayOf().inputStream(),
                ByteArrayOutputStream(),
                "chatgpt.com",
                profile,
            ) {
                hello = it
            }
        }
        assertNotNull(hello)
        val input = DataInputStream(hello!!.inputStream())
        assertEquals(0x0303, input.readUnsignedShort())
        input.skipNBytes(32)
        input.skipNBytes(input.readUnsignedByte().toLong())
        val ciphers = ByteArray(input.readUnsignedShort()).also(input::readFully).hex()
        input.skipNBytes(input.readUnsignedByte().toLong())
        assertEquals(input.readUnsignedShort(), input.available())
        val extensions = linkedMapOf<Int, String>()
        while (input.available() > 0) {
            val type = input.readUnsignedShort()
            extensions[type] = ByteArray(input.readUnsignedShort()).also(input::readFully).hex()
        }
        return ciphers to extensions
    }

    private fun ByteArray.hex() = joinToString("") { "%02x".format(it) }
}
