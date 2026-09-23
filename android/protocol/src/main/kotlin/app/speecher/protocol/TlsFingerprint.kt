package app.speecher.protocol

import java.io.ByteArrayOutputStream
import java.io.OutputStream
import java.net.IDN
import java.util.Hashtable
import java.util.Vector
import org.bouncycastle.tls.ClientHello
import org.bouncycastle.tls.ProtocolName
import org.bouncycastle.tls.ProtocolVersion
import org.bouncycastle.tls.ServerName
import org.bouncycastle.tls.SignatureAndHashAlgorithm
import org.bouncycastle.tls.TlsExtensionsUtils
import org.bouncycastle.tls.TlsUtils

// Add a named TlsFingerprint and register it in TlsFingerprints.named. Switch with
// -Dspeecher.tlsFingerprint=<name> or SPEECHER_TLS_FINGERPRINT=<name>; default: openssl.
// This file owns ClientHello policy and encoding, including extension order. BC supplies
// fresh random/session/key-share bytes; never rewrite bytes after BC hashes the handshake.
data class TlsFingerprint(
    val name: String,
    val versions: List<ProtocolVersion>,
    val cipherSuites: List<Int>,
    val extensionOrder: List<Int>,
    val groups: List<Int>,
    val signatureSchemes: List<Int>,
    val keyShareGroups: List<Int>,
    val pointFormats: List<Short> = listOf(0, 1, 2),
    val alpn: List<String> = listOf("http/1.1"),
    val extraExtensions: Map<Int, ByteArray> = emptyMap(),
    val compatibilityMode: Boolean = true,
    val padTo: Int = 512,
) {
    internal fun extensions(host: String): Hashtable<Int, ByteArray> =
        Hashtable<Int, ByteArray>().apply {
            // RFC 6066 forbids SNI for IP literals; DNS names are sent as their A-label (ASCII)
            // form.
            val sni = host.removeSurrounding("[", "]")
            if (':' !in sni && !sni.matches(Regex("[0-9.]+")))
                TlsExtensionsUtils.addServerNameExtensionClient(
                    this,
                    Vector(listOf(ServerName(0, IDN.toASCII(sni).toByteArray(Charsets.US_ASCII)))),
                )
            TlsExtensionsUtils.addALPNExtensionClient(
                this,
                Vector(alpn.map(ProtocolName::asUtf8Encoding)),
            )
            TlsExtensionsUtils.addSupportedGroupsExtension(this, Vector(groups))
            TlsExtensionsUtils.addSignatureAlgorithmsExtension(
                this,
                Vector(
                    signatureSchemes.map {
                        SignatureAndHashAlgorithm.getInstance(
                            (it shr 8).toShort(),
                            (it and 255).toShort(),
                        )
                    }
                ),
            )
            TlsExtensionsUtils.addSupportedPointFormatsExtension(this, pointFormats.toShortArray())
            put(35, byteArrayOf()) // session_ticket
            put(22, byteArrayOf()) // encrypt_then_mac
            put(45, byteArrayOf(1, 1)) // psk_dhe_ke
            putAll(extraExtensions)
            keys.toList().filter { it !in extensionOrder }.forEach(::remove)
        }

    internal fun encode(hello: ClientHello, output: OutputStream) {
        check(hello.bindersSize == 0) { "Fingerprint encoder does not support PSK resumption" }
        val body = ByteArrayOutputStream()
        TlsUtils.writeVersion(hello.version, body)
        body.write(hello.random)
        TlsUtils.writeOpaque8(hello.sessionID, body)
        TlsUtils.writeUint16ArrayWithUint16Length(hello.cipherSuites, body)
        TlsUtils.writeUint8ArrayWithUint8Length(shortArrayOf(0), body)
        val extensions = ByteArrayOutputStream()
        val actual = hello.extensions
        check(actual.keys.toList().all { it in extensionOrder }) { "Unprofiled TLS extension" }
        extensionOrder.forEach { type ->
            val bytes = actual[type] as? ByteArray
            if (bytes != null) {
                TlsUtils.writeUint16(type, extensions)
                TlsUtils.writeOpaque16(bytes, extensions)
            }
        }
        // OpenSSL pads a 256..511-byte ClientHello to 512 bytes (including handshake header).
        val length = 4 + body.size() + 2 + extensions.size()
        if (padTo > 0 && length in 256 until padTo) {
            TlsUtils.writeUint16(21, extensions)
            TlsUtils.writeOpaque16(ByteArray(maxOf(0, padTo - length - 4)), extensions)
        }
        TlsUtils.writeOpaque16(extensions.toByteArray(), body)
        output.write(body.toByteArray())
    }
}

// OpenSSL 3.6 default with ALPN http/1.1. Certificate compression is omitted:
// BC 1.86 cannot consume compressed_certificate messages.
val OpenSslFingerprint =
    TlsFingerprint(
        name = "openssl",
        versions = listOf(ProtocolVersion.TLSv13, ProtocolVersion.TLSv12),
        cipherSuites =
            listOf(
                0x1302,
                0x1303,
                0x1301,
                0xc02c,
                0xc030,
                0x009f,
                0xcca9,
                0xcca8,
                0xccaa,
                0xc02b,
                0xc02f,
                0x009e,
                0xc024,
                0xc028,
                0x006b,
                0xc023,
                0xc027,
                0x0067,
                0xc00a,
                0xc014,
                0x0039,
                0xc009,
                0xc013,
                0x0033,
                0x009d,
                0x009c,
                0x003d,
                0x003c,
                0x0035,
                0x002f,
            ),
        extensionOrder = listOf(65281, 0, 11, 10, 35, 16, 22, 23, 13, 43, 45, 51),
        groups = listOf(4588, 29, 23, 30, 24, 25, 256, 257),
        signatureSchemes =
            listOf(
                0x0905,
                0x0906,
                0x0904,
                0x0403,
                0x0503,
                0x0603,
                0x0807,
                0x0808,
                0x081a,
                0x081b,
                0x081c,
                0x0809,
                0x080a,
                0x080b,
                0x0804,
                0x0805,
                0x0806,
                0x0401,
                0x0501,
                0x0601,
                0x0303,
                0x0301,
                0x0302,
                0x0402,
                0x0502,
                0x0602,
            ),
        keyShareGroups = listOf(4588, 29),
        pointFormats = listOf(0),
        extraExtensions = mapOf(65281 to byteArrayOf(0)),
    )

// Selectable best-effort Chrome stub, not a claim of Chrome fingerprint equivalence:
// no GREASE, extension permutation, certificate compression, ECH or hybrid key share yet.
val ChromeFingerprint =
    OpenSslFingerprint.copy(
        name = "chrome",
        cipherSuites =
            listOf(
                0x1301,
                0x1302,
                0x1303,
                0xc02b,
                0xc02f,
                0xc02c,
                0xc030,
                0xcca9,
                0xcca8,
                0xc013,
                0xc014,
                0x009c,
                0x009d,
                0x002f,
                0x0035,
                0x00ff,
            ),
        groups = listOf(29, 23, 24),
        keyShareGroups = listOf(29),
        signatureSchemes = listOf(0x0403, 0x0804, 0x0401, 0x0503, 0x0805, 0x0501, 0x0806, 0x0601),
        pointFormats = listOf(0),
    )

object TlsFingerprints {
    fun named(name: String): TlsFingerprint =
        when (name) {
            "openssl" -> OpenSslFingerprint
            "chrome" -> ChromeFingerprint
            else -> error("Unknown TLS fingerprint: $name")
        }

    val active: TlsFingerprint
        get() =
            named(
                System.getProperty("speecher.tlsFingerprint")
                    ?: System.getenv("SPEECHER_TLS_FINGERPRINT")
                    ?: "openssl"
            )
}
