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

/** As [httpPostStreaming], reading the whole response body. */
fun httpPost(
    url: String,
    headers: Map<String, String>,
    body: HttpBody,
    fingerprint: TlsFingerprint = TlsFingerprints.active,
): HttpResponse =
    httpPostStreaming(url, headers, body, fingerprint) { status, input ->
        HttpResponse(status, readAtMost(input))
    }

/**
 * POSTs [body] over HTTP/1.1 and hands the status and decoded body stream to [read] as the bytes
 * arrive. https URLs use the same browser-shaped BC TLS as the speech WebSocket, because Cloudflare
 * rejects Conscrypt's handshake on chatgpt.com; http URLs use a plain socket, for fake-server
 * builds and tests. One request per connection.
 */
fun <T> httpPostStreaming(
    url: String,
    headers: Map<String, String>,
    body: HttpBody,
    fingerprint: TlsFingerprint = TlsFingerprints.active,
    read: (status: Int, body: InputStream) -> T,
): T {
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
        return readResponse(BufferedInputStream(tls?.inputStream ?: socket.getInputStream()), read)
    }
}

private fun <T> readResponse(input: InputStream, read: (Int, InputStream) -> T): T {
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
            fields["transfer-encoding"]?.contains("chunked", true) == true ->
                ChunkedInputStream(input)
            length != null ->
                readExactly(
                        input,
                        length.toIntOrNull()?.takeIf { it >= 0 }
                            ?: throw IOException("Invalid Content-Length: $length"),
                    )
                    .inputStream()
            else -> input
        }
    val body =
        when (val encoding = fields["content-encoding"]?.lowercase()) {
            null,
            "identity" -> raw
            "gzip" -> GZIPInputStream(raw)
            else -> throw IOException("Unsupported Content-Encoding: $encoding")
        }
    return read(status, body)
}

/**
 * Decodes a chunked body as it arrives, so a server-sent-event stream reaches its reader event by
 * event. Each chunk's terminator is read lazily, at the start of the next read, so a read never
 * waits on bytes the server has not sent yet.
 */
private class ChunkedInputStream(private val input: InputStream) : InputStream() {
    private var remaining = 0
    private var started = false
    private var finished = false

    override fun read(): Int {
        val byte = ByteArray(1)
        return if (read(byte, 0, 1) < 0) -1 else byte[0].toInt() and 0xff
    }

    override fun read(buffer: ByteArray, offset: Int, length: Int): Int {
        if (finished) return -1
        if (length == 0) return 0
        if (remaining == 0) {
            if (started && readLine(input).isNotEmpty())
                throw IOException("Invalid chunk terminator")
            started = true
            val line = readLine(input)
            remaining =
                line.substringBefore(';').trim().toIntOrNull(16)?.takeIf { it >= 0 }
                    ?: throw IOException("Invalid chunk size: $line")
            if (remaining == 0) {
                // Trailer fields end at an empty line; nothing here needs them.
                do {
                    val trailer = readLine(input)
                } while (trailer.isNotEmpty())
                finished = true
                return -1
            }
        }
        val count = input.read(buffer, offset, minOf(length, remaining))
        if (count < 0) throw EOFException("HTTP chunk ended early")
        remaining -= count
        return count
    }
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
