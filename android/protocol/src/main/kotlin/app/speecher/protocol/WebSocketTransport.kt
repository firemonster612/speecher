package app.speecher.protocol

import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import java.io.EOFException
import java.io.IOException
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.net.URI
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.security.MessageDigest
import java.security.SecureRandom
import java.util.Base64
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

interface WebSocketTransport {
    fun open(url: String, headers: Map<String, String>, subprotocol: String?, listener: Listener)

    fun sendText(text: String): Boolean

    fun sendBinary(bytes: ByteArray): Boolean

    fun close(code: Int = 1000, reason: String? = null)

    fun cancel()

    interface Listener {
        fun onOpen()

        fun onText(text: String)

        fun onBinary(bytes: ByteArray) {}

        fun onClosed(code: Int, reason: String)

        fun onFailure(error: Throwable, statusCode: Int?)
    }
}

fun webSocketTransport(url: String): WebSocketTransport =
    when (URI(url).scheme) {
        "wss",
        "https" -> BcTlsWebSocketTransport()
        "ws",
        "http" -> SocketWebSocketTransport()
        else -> error("Unsupported WebSocket scheme")
    }

class BcTlsWebSocketTransport(
    fingerprint: TlsFingerprint = TlsFingerprints.active,
    clientHelloObserver: ((ByteArray) -> Unit)? = null,
) : SocketWebSocketTransport(fingerprint, clientHelloObserver)

open class SocketWebSocketTransport
internal constructor(
    private val fingerprint: TlsFingerprint? = null,
    private val clientHelloObserver: ((ByteArray) -> Unit)? = null,
) : WebSocketTransport {
    private val socket = Socket()
    private val random = SecureRandom()
    private val writer = Executors.newSingleThreadScheduledExecutor { task ->
        Thread(task, "speech-ws-writer").apply { isDaemon = true }
    }
    @Volatile private var output: OutputStream? = null
    @Volatile private var cancelled = false
    @Volatile private var opened = false
    private val closing = AtomicBoolean(false)
    private val terminated = AtomicBoolean(false)
    private val queuedBytes = AtomicLong(0)
    @Volatile private var closeTimer: Thread? = null
    private lateinit var listener: WebSocketTransport.Listener

    override fun open(
        url: String,
        headers: Map<String, String>,
        subprotocol: String?,
        listener: WebSocketTransport.Listener,
    ) {
        check(!this::listener.isInitialized) { "Transport already opened" }
        this.listener = listener
        Thread(
                {
                    try {
                        val uri = URI(url)
                        val secure = uri.scheme in listOf("wss", "https")
                        require(secure == (fingerprint != null)) {
                            "Wrong transport for URL scheme"
                        }
                        require(uri.userInfo == null && uri.fragment == null) {
                            "Invalid WebSocket URL"
                        }
                        socket.connect(
                            InetSocketAddress(
                                uri.host,
                                if (uri.port < 0) {
                                    if (secure) 443 else 80
                                } else uri.port,
                            ),
                            10000,
                        )
                        socket.soTimeout = 10000
                        val tls = fingerprint?.let {
                            connectTls(
                                socket.getInputStream(),
                                socket.getOutputStream(),
                                uri.host,
                                it,
                                clientHelloObserver,
                            )
                        }
                        val input = DataInputStream(tls?.inputStream ?: socket.getInputStream())
                        output = tls?.outputStream ?: socket.getOutputStream()
                        upgrade(uri, headers, subprotocol, input)
                        socket.soTimeout = 0
                        opened = true
                        if (!cancelled) listener.onOpen()
                        readFrames(input)
                    } catch (error: Exception) {
                        if (!cancelled) deliverFailure(error, (error as? UpgradeFailure)?.status)
                    } finally {
                        cancel()
                    }
                },
                "speech-ws-reader",
            )
            .apply { isDaemon = true }
            .start()
    }

    private fun upgrade(
        uri: URI,
        headers: Map<String, String>,
        subprotocol: String?,
        input: DataInputStream,
    ) {
        val key = Base64.getEncoder().encodeToString(ByteArray(16).also(random::nextBytes))
        val fields =
            linkedMapOf(
                "Host" to uri.rawAuthority,
                "Upgrade" to "websocket",
                "Connection" to "Upgrade",
                "Sec-WebSocket-Key" to key,
                "Sec-WebSocket-Version" to "13",
            )
        headers.forEach { (name, value) ->
            require(
                name.matches(Regex("[!#$%&'*+.^_`|~0-9A-Za-z-]+")) &&
                    '\r' !in value &&
                    '\n' !in value
            ) {
                "Invalid WebSocket header"
            }
            require(fields.keys.none { it.equals(name, true) }) { "Reserved WebSocket header" }
            fields[name] = value
        }
        if (subprotocol != null) {
            require('\r' !in subprotocol && '\n' !in subprotocol) {
                "Invalid WebSocket subprotocol"
            }
            fields["Sec-WebSocket-Protocol"] = subprotocol
        }
        val path = uri.rawPath.ifEmpty { "/" } + (uri.rawQuery?.let { "?$it" } ?: "")
        output!!.write(
            ("GET $path HTTP/1.1\r\n" +
                    fields.entries.joinToString("") { "${it.key}: ${it.value}\r\n" } +
                    "\r\n")
                .toByteArray(Charsets.UTF_8)
        )
        output!!.flush()
        val response = ByteArrayOutputStream()
        var w = -1
        var x = -1
        var y = -1
        var z = -1
        while (!(w == 13 && x == 10 && y == 13 && z == 10)) {
            if (response.size() >= 32768) throw IOException("WebSocket response headers too large")
            val byte = input.readUnsignedByte()
            response.write(byte)
            w = x
            x = y
            y = z
            z = byte
        }
        val lines = response.toString(Charsets.US_ASCII.name()).split("\r\n")
        val status =
            lines.first().split(' ').getOrNull(1)?.toIntOrNull()
                ?: throw IOException("Invalid HTTP response")
        if (status != 101) throw UpgradeFailure(status)
        val received =
            lines
                .drop(1)
                .filter { ':' in it }
                .associate { it.substringBefore(':').lowercase() to it.substringAfter(':').trim() }
        val accept =
            Base64.getEncoder()
                .encodeToString(
                    MessageDigest.getInstance("SHA-1")
                        .digest(
                            (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").toByteArray(
                                Charsets.US_ASCII
                            )
                        )
                )
        if (
            received["sec-websocket-accept"] != accept ||
                !received["upgrade"].equals("websocket", true) ||
                received["connection"]?.split(',')?.none { it.trim().equals("upgrade", true) } !=
                    false
        )
            throw IOException("Invalid WebSocket upgrade")
        if (received.containsKey("sec-websocket-extensions"))
            throw IOException("Unrequested WebSocket extension")
        received["sec-websocket-protocol"]?.let { selected ->
            val offered =
                subprotocol
                    ?: headers.entries
                        .firstOrNull { it.key.equals("Sec-WebSocket-Protocol", true) }
                        ?.value
            if (selected !in offered.orEmpty().split(',').map(String::trim))
                throw IOException("Unrequested WebSocket subprotocol")
        }
    }

    private fun readFrames(input: DataInputStream) {
        var message = ByteArrayOutputStream()
        var messageType = 0
        while (!cancelled) {
            val first = input.readUnsignedByte()
            val second = input.readUnsignedByte()
            val fin = first and 128 != 0
            val opcode = first and 15
            if (first and 112 != 0 || second and 128 != 0)
                throw IOException("Invalid server frame flags")
            val marker = second and 127
            val length =
                when (marker) {
                    126 -> input.readUnsignedShort().toLong()
                    127 -> input.readLong()
                    else -> marker.toLong()
                }
            if (
                length < 0 ||
                    length > 16 * 1024 * 1024 ||
                    (marker == 126 && length < 126) ||
                    (marker == 127 && length < 65536)
            )
                throw IOException("Invalid frame length")
            if (opcode >= 8 && (!fin || length > 125)) throw IOException("Invalid control frame")
            val bytes = ByteArray(length.toInt()).also(input::readFully)
            when (opcode) {
                8 -> {
                    if (bytes.size == 1) throw IOException("Invalid close payload")
                    val code =
                        if (bytes.isEmpty()) 1005
                        else ((bytes[0].toInt() and 255) shl 8) or (bytes[1].toInt() and 255)
                    if (bytes.isNotEmpty() && !validCloseCode(code))
                        throw IOException("Invalid close code")
                    val reason =
                        if (bytes.isEmpty()) "" else decodeText(bytes.copyOfRange(2, bytes.size))
                    if (closing.compareAndSet(false, true)) submit(8, bytes)
                    deliverClosed(code, reason)
                    return
                }
                9 -> submit(10, bytes)
                10 -> Unit
                0,
                1,
                2 -> {
                    if (opcode == 0 && messageType == 0 || opcode != 0 && messageType != 0)
                        throw IOException("Invalid continuation frame")
                    if (opcode != 0) messageType = opcode
                    if (message.size() + bytes.size > 16 * 1024 * 1024)
                        throw IOException("WebSocket message too large")
                    message.write(bytes)
                    if (fin) {
                        if (messageType == 1) listener.onText(decodeText(message.toByteArray()))
                        else listener.onBinary(message.toByteArray())
                        message = ByteArrayOutputStream()
                        messageType = 0
                    }
                }
                else -> throw IOException("Invalid frame opcode")
            }
        }
    }

    override fun sendText(text: String) = enqueue(1, text.toByteArray(Charsets.UTF_8))

    override fun sendBinary(bytes: ByteArray) = enqueue(2, bytes.copyOf())

    // Only the single writer thread runs writeFrame, so a blocked network write never stalls a
    // caller
    // (e.g. the audio thread) or the reader thread. Control frames (pong, close) go straight
    // through.
    private fun submit(opcode: Int, bytes: ByteArray) {
        try {
            writer.execute { runWrite(opcode, bytes) }
        } catch (_: RejectedExecutionException) {}
    }

    private fun runWrite(opcode: Int, bytes: ByteArray) {
        if (cancelled) return
        // On the single writer thread a data task ordered after the close frame sees closing ==
        // true;
        // dropping it keeps data frames from following the close (RFC 6455 5.5.1).
        if ((opcode == 1 || opcode == 2) && closing.get()) return
        try {
            writeFrame(opcode, bytes)
        } catch (error: IOException) {
            // A write failing because cancel() closed the socket is not a reportable failure.
            if (!cancelled) deliverFailure(error, null)
            socket.close()
        }
    }

    // Data frames are bounded: on a stalled connection the queue cannot grow without limit the way
    // a
    // blocking write once bounded it. Over the cap the send is refused so the caller fails fast.
    private fun enqueue(opcode: Int, bytes: ByteArray): Boolean {
        if (cancelled || closing.get() || !opened) return false
        if (queuedBytes.addAndGet(bytes.size.toLong()) > MAX_QUEUED_BYTES) {
            queuedBytes.addAndGet(-bytes.size.toLong())
            return false
        }
        try {
            writer.execute {
                try {
                    runWrite(opcode, bytes)
                } finally {
                    queuedBytes.addAndGet(-bytes.size.toLong())
                }
            }
        } catch (_: RejectedExecutionException) {
            queuedBytes.addAndGet(-bytes.size.toLong())
            return false
        }
        return true
    }

    private fun writeFrame(opcode: Int, bytes: ByteArray) {
        val target = output ?: throw EOFException("WebSocket not open")
        val mask = ByteArray(4).also(random::nextBytes)
        val frame = ByteArrayOutputStream()
        frame.write(128 or opcode)
        when {
            bytes.size < 126 -> frame.write(128 or bytes.size)
            bytes.size <= 65535 -> {
                frame.write(128 or 126)
                frame.write(bytes.size shr 8)
                frame.write(bytes.size)
            }
            else -> {
                frame.write(128 or 127)
                frame.write(ByteBuffer.allocate(8).putLong(bytes.size.toLong()).array())
            }
        }
        frame.write(mask)
        bytes.forEachIndexed { index, byte ->
            frame.write(byte.toInt() xor mask[index % 4].toInt())
        }
        target.write(frame.toByteArray())
        target.flush()
    }

    override fun close(code: Int, reason: String?) {
        require(validCloseCode(code)) { "Invalid close code" }
        val text = reason.orEmpty().toByteArray(Charsets.UTF_8)
        require(text.size <= 123) { "Close reason too long" }
        if (cancelled || !opened || !closing.compareAndSet(false, true)) return
        submit(8, ByteBuffer.allocate(2 + text.size).putShort(code.toShort()).put(text).array())
        // The fallback runs off the writer thread so it still fires even if a stalled socket has
        // wedged the writer; cancel() then unblocks the write, and the terminated CAS keeps
        // delivery
        // to exactly one callback if the peer's echo wins the race. Publish the field before
        // start()
        // and recheck cancelled after the sleep so a racing cancel() can never leave a late
        // callback.
        val timer =
            Thread(
                {
                    try {
                        Thread.sleep(5000)
                    } catch (_: InterruptedException) {
                        return@Thread
                    }
                    if (cancelled) return@Thread
                    deliverClosed(code, reason.orEmpty())
                    cancel()
                },
                "speech-ws-close",
            )
        timer.isDaemon = true
        closeTimer = timer
        timer.start()
    }

    override fun cancel() {
        cancelled = true
        socket.close()
        writer.shutdownNow()
        closeTimer?.interrupt()
    }

    private fun deliverClosed(code: Int, reason: String) {
        if (terminated.compareAndSet(false, true)) listener.onClosed(code, reason)
    }

    private fun deliverFailure(error: Throwable, status: Int?) {
        if (terminated.compareAndSet(false, true)) listener.onFailure(error, status)
    }
}

private const val MAX_QUEUED_BYTES = 8L * 1024 * 1024

private class UpgradeFailure(val status: Int) : IOException("WebSocket upgrade HTTP $status")

private fun validCloseCode(code: Int) =
    code in 1000..1014 && code !in listOf(1004, 1005, 1006) || code in 3000..4999

private fun decodeText(bytes: ByteArray): String =
    Charsets.UTF_8.newDecoder()
        .onMalformedInput(CodingErrorAction.REPORT)
        .decode(ByteBuffer.wrap(bytes))
        .toString()
