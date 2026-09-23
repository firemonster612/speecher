package app.speecher.protocol

import java.io.BufferedInputStream
import java.io.ByteArrayOutputStream
import java.io.EOFException
import java.io.IOException
import java.io.InputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.net.URI
import java.util.UUID
import java.util.zip.GZIPInputStream

class HttpBody(val contentType: String, val bytes: ByteArray)

class HttpResponse(val status: Int, val body: ByteArray)

/** A multipart/form-data body holding one file part, the shape an HTML file upload sends. */
fun multipartFile(
    field: String,
    filename: String,
    contentType: String,
    bytes: ByteArray,
): HttpBody {
    val boundary = "speecher-${UUID.randomUUID()}"
    val head =
        "--$boundary\r\n" +
            "Content-Disposition: form-data; name=\"$field\"; filename=\"$filename\"\r\n" +
            "Content-Type: $contentType\r\n\r\n"
    return HttpBody(
        "multipart/form-data; boundary=$boundary",
        head.toByteArray(Charsets.UTF_8) +
            bytes +
            "\r\n--$boundary--\r\n".toByteArray(Charsets.UTF_8),
    )
}

/**
 * POSTs [body] over HTTP/1.1 and reads the whole response. https URLs use the same browser-shaped
 * BC TLS as the speech WebSocket, because Cloudflare rejects Conscrypt's handshake on chatgpt.com;
 * http URLs use a plain socket, for fake-server builds and tests. One request per connection.
 */
fun httpPost(
    url: String,
    headers: Map<String, String>,
    body: HttpBody,
    fingerprint: TlsFingerprint = TlsFingerprints.active,
): HttpResponse {
    val uri = URI(url)
    val secure =
        when (uri.scheme) {
            "https" -> true
            "http" -> false
            else -> throw IllegalArgumentException("Unsupported HTTP scheme: ${uri.scheme}")
        }
    val fields =
        linkedMapOf("Host" to uri.rawAuthority) +
            headers +
            linkedMapOf(
                "Content-Type" to body.contentType,
                "Content-Length" to body.bytes.size.toString(),
                "Accept-Encoding" to "gzip",
                "Connection" to "close",
            )
    Socket().use { socket ->
        socket.connect(
            InetSocketAddress(uri.host, if (uri.port < 0) (if (secure) 443 else 80) else uri.port),
            10000,
        )
        socket.soTimeout = 10000
        val tls =
            if (secure)
                connectTls(
                    socket.getInputStream(),
                    socket.getOutputStream(),
                    uri.host,
                    fingerprint,
                    null,
                )
            else null
        val output = tls?.outputStream ?: socket.getOutputStream()
        val path = uri.rawPath.ifEmpty { "/" } + (uri.rawQuery?.let { "?$it" } ?: "")
        output.write(
            ("POST $path HTTP/1.1\r\n" +
                    fields.entries.joinToString("") { "${it.key}: ${it.value}\r\n" } +
                    "\r\n")
                .toByteArray(Charsets.UTF_8)
        )
        output.write(body.bytes)
        output.flush()
        return readResponse(BufferedInputStream(tls?.inputStream ?: socket.getInputStream()))
    }
}

private fun readResponse(input: InputStream): HttpResponse {
    var lines: List<String>
    var status: Int
    // Interim 1xx responses (e.g. 103 Early Hints) precede the real one; skip them.
    do {
        lines = generateSequence { readLine(input) }.takeWhile { it.isNotEmpty() }.toList()
        status =
            lines.firstOrNull()?.split(' ')?.getOrNull(1)?.toIntOrNull()
                ?: throw IOException("Invalid HTTP status line: ${lines.firstOrNull()}")
    } while (status in 100..199)
    val fields =
        lines
            .drop(1)
            .filter { ':' in it }
            .associate {
                it.substringBefore(':').trim().lowercase() to it.substringAfter(':').trim()
            }
    val length = fields["content-length"]
    val raw =
        when {
            fields["transfer-encoding"]?.contains("chunked", true) == true -> readChunked(input)
            length != null ->
                readExactly(
                    input,
                    length.toIntOrNull()?.takeIf { it >= 0 }
                        ?: throw IOException("Invalid Content-Length: $length"),
                )
            else -> readAtMost(input)
        }
    val body =
        when (val encoding = fields["content-encoding"]?.lowercase()) {
            null,
            "identity" -> raw
            "gzip" -> GZIPInputStream(raw.inputStream()).use(::readAtMost)
            else -> throw IOException("Unsupported Content-Encoding: $encoding")
        }
    return HttpResponse(status, body)
}

private fun readChunked(input: InputStream): ByteArray {
    val body = ByteArrayOutputStream()
    while (true) {
        val line = readLine(input)
        val size =
            line.substringBefore(';').trim().toIntOrNull(16)?.takeIf { it >= 0 }
                ?: throw IOException("Invalid chunk size: $line")
        if (size == 0) break
        if (body.size() + size > MAX_BODY_BYTES) throw IOException("HTTP response too large")
        body.write(readExactly(input, size))
        if (readLine(input).isNotEmpty()) throw IOException("Invalid chunk terminator")
    }
    // Trailer fields end at an empty line; nothing here needs them.
    do {
        val trailer = readLine(input)
    } while (trailer.isNotEmpty())
    return body.toByteArray()
}

private fun readExactly(input: InputStream, size: Int): ByteArray {
    if (size > MAX_BODY_BYTES) throw IOException("HTTP response too large")
    val bytes = input.readNBytes(size)
    if (bytes.size != size) throw EOFException("HTTP body ended after ${bytes.size} of $size bytes")
    return bytes
}

private fun readAtMost(input: InputStream): ByteArray {
    val bytes = input.readNBytes(MAX_BODY_BYTES + 1)
    if (bytes.size > MAX_BODY_BYTES) throw IOException("HTTP response too large")
    return bytes
}

/** One header or chunk-size line without its line ending. */
private fun readLine(input: InputStream): String {
    val line = ByteArrayOutputStream()
    while (true) {
        val byte = input.read()
        if (byte < 0) throw EOFException("HTTP response ended mid-line")
        if (byte == '\n'.code) break
        if (line.size() >= MAX_LINE_BYTES) throw IOException("HTTP response line too long")
        line.write(byte)
    }
    return line.toString(Charsets.ISO_8859_1.name()).removeSuffix("\r")
}

private const val MAX_BODY_BYTES = 16 * 1024 * 1024

private const val MAX_LINE_BYTES = 32768
