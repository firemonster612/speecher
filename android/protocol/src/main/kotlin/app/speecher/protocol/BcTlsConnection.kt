package app.speecher.protocol

import java.io.InputStream
import java.io.OutputStream
import java.net.IDN
import java.net.InetAddress
import java.security.KeyStore
import java.security.SecureRandom
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import java.util.Vector
import javax.net.ssl.SSLPeerUnverifiedException
import javax.net.ssl.TrustManagerFactory
import javax.net.ssl.X509TrustManager
import org.bouncycastle.tls.CertificateRequest
import org.bouncycastle.tls.ClientHello
import org.bouncycastle.tls.DefaultTlsClient
import org.bouncycastle.tls.TlsAuthentication
import org.bouncycastle.tls.TlsClientProtocol
import org.bouncycastle.tls.TlsContext
import org.bouncycastle.tls.TlsCredentials
import org.bouncycastle.tls.TlsExtensionsUtils
import org.bouncycastle.tls.TlsServerCertificate
import org.bouncycastle.tls.crypto.impl.bc.BcTlsCrypto

internal fun connectTls(
    input: InputStream,
    output: OutputStream,
    host: String,
    fingerprint: TlsFingerprint,
    clientHelloObserver: ((ByteArray) -> Unit)?,
): TlsClientProtocol {
    val protocol =
        object : TlsClientProtocol(input, output) {
            private var firstHello = true

            override fun sendClientHelloMessage() {
                val current = clientHello
                clientHello =
                    if (firstHello) {
                        firstHello = false
                        object :
                            ClientHello(
                                current.version,
                                current.random,
                                current.sessionID,
                                current.cookie,
                                current.cipherSuites,
                                current.extensions,
                                current.bindersSize,
                            ) {
                            override fun encode(context: TlsContext, output: OutputStream) {
                                val bytes = java.io.ByteArrayOutputStream()
                                fingerprint.encode(this, bytes)
                                val encoded = bytes.toByteArray()
                                clientHelloObserver?.invoke(encoded)
                                output.write(encoded)
                            }
                        }
                    } else {
                        // A HelloRetryRequest reuses this same clientHello, mutating its shared
                        // extensions in place (adding a cookie, swapping the key share). Re-encode
                        // the
                        // retry with BC's stock ClientHello so BC-added extensions do not trip the
                        // fingerprint's profile check; only the first flight (what the JA3 hashes)
                        // needs the fingerprint.
                        ClientHello(
                            current.version,
                            current.random,
                            current.sessionID,
                            current.cookie,
                            current.cipherSuites,
                            current.extensions,
                            current.bindersSize,
                        )
                    }
                super.sendClientHelloMessage()
            }
        }
    protocol.connect(
        object : DefaultTlsClient(BcTlsCrypto(SecureRandom())) {
            override fun getSupportedVersions() = fingerprint.versions.toTypedArray()

            override fun getSupportedCipherSuites() = fingerprint.cipherSuites.toIntArray()

            override fun getClientExtensions() =
                fingerprint.extensions(host).also {
                    supportedGroups = Vector(fingerprint.groups)
                    supportedSignatureAlgorithms =
                        TlsExtensionsUtils.getSignatureAlgorithmsExtension(it)
                }

            override fun getEarlyKeyShareGroups() = Vector(fingerprint.keyShareGroups)

            override fun shouldUseCompatibilityMode() = fingerprint.compatibilityMode

            override fun shouldUseExtendedMasterSecret() = 23 in fingerprint.extensionOrder

            override fun getAuthentication(): TlsAuthentication =
                object : TlsAuthentication {
                    override fun getClientCredentials(
                        request: CertificateRequest
                    ): TlsCredentials? = null

                    override fun notifyServerCertificate(serverCertificate: TlsServerCertificate) {
                        val factory = CertificateFactory.getInstance("X.509")
                        val chain =
                            serverCertificate.certificate.certificateList
                                .map {
                                    factory.generateCertificate(it.encoded.inputStream())
                                        as X509Certificate
                                }
                                .toTypedArray()
                        val trust =
                            TrustManagerFactory.getInstance(
                                TrustManagerFactory.getDefaultAlgorithm()
                            )
                        trust.init(null as KeyStore?)
                        val manager =
                            trust.trustManagers.filterIsInstance<X509TrustManager>().first()
                        val authType =
                            when (chain.first().publicKey.algorithm) {
                                "EC" -> "ECDHE_ECDSA"
                                "RSA" -> "ECDHE_RSA"
                                else -> "UNKNOWN"
                            }
                        manager.checkServerTrusted(chain, authType)
                        verifyHostname(host, chain.first())
                    }
                }
        }
    )
    return protocol
}

internal fun verifyHostname(host: String, certificate: X509Certificate) {
    if (!hostnameMatches(host, certificate.subjectAlternativeNames.orEmpty()))
        throw SSLPeerUnverifiedException("TLS certificate does not match $host")
}

// SAN entries are [type, value]: type 2 is a DNS name, type 7 an IP address (see X509Certificate).
internal fun hostnameMatches(host: String, alternatives: Collection<List<*>>): Boolean {
    val unwrapped = host.removeSurrounding("[", "]")
    val name = if (':' in unwrapped) unwrapped.lowercase() else IDN.toASCII(unwrapped).lowercase()
    val isIp = ':' in name || name.matches(Regex("[0-9.]+"))
    return alternatives.any { entry ->
        if (isIp)
            entry[0] == 7 &&
                InetAddress.getByName(name) == InetAddress.getByName(entry[1] as String)
        else entry[0] == 2 && matchesDnsName(name, (entry[1] as String).lowercase())
    }
}

internal fun matchesDnsName(host: String, pattern: String): Boolean {
    if ('*' !in pattern) return host == pattern
    if (!pattern.startsWith("*.") || '*' in pattern.substring(1)) return false
    val suffix = pattern.substring(1)
    return host.endsWith(suffix) && host.removeSuffix(suffix).let { it.isNotEmpty() && '.' !in it }
}
