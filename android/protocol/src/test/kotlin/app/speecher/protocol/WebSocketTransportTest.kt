package app.speecher.protocol

import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test

class WebSocketTransportTest {
    @Test
    fun `plain transport upgrades sends masked text and receives reply`() {
        MockWebServer().use { server ->
            server.enqueue(
                MockResponse.Builder()
                    .webSocketUpgrade(
                        object : WebSocketListener() {
                            override fun onMessage(webSocket: WebSocket, text: String) {
                                webSocket.send("reply:$text")
                                webSocket.close(1000, "done")
                            }
                        }
                    )
                    .build()
            )
            server.start()
            val events = LinkedBlockingQueue<String>()
            val transport = webSocketTransport(server.url("/").toString())
            transport.open(
                server.url("/").toString(),
                mapOf("x-app" to "cli"),
                null,
                object : WebSocketTransport.Listener {
                    override fun onOpen() {
                        transport.sendText("hello")
                    }

                    override fun onText(text: String) {
                        events.add(text)
                    }

                    override fun onClosed(code: Int, reason: String) {
                        events.add("$code $reason")
                    }

                    override fun onFailure(error: Throwable, statusCode: Int?) {
                        events.add("failure")
                    }
                },
            )
            assertEquals("reply:hello", events.poll(3, TimeUnit.SECONDS))
            assertEquals("1000 done", events.poll(3, TimeUnit.SECONDS))
            assertEquals("cli", server.takeRequest().headers["x-app"])
            transport.cancel()
        }
    }
}

class WebSocketWireTest {
    @Test
    fun `fragments ping binary and masked extended writes share one connection`() {
        withPeer { transport, peer, events ->
            val input = java.io.DataInputStream(peer.getInputStream())
            val output = peer.getOutputStream()
            acceptUpgrade(input, output)
            assertEquals("open", events.poll(3, TimeUnit.SECONDS))
            val sent = ByteArray(300) { 42 }
            transport.sendBinary(sent)
            assertEquals(2, input.readUnsignedByte() and 15)
            assertEquals(254, input.readUnsignedByte())
            assertEquals(300, input.readUnsignedShort())
            val mask = ByteArray(4).also(input::readFully)
            val payload = ByteArray(300).also(input::readFully)
            org.junit.jupiter.api.Assertions.assertArrayEquals(
                sent,
                payload
                    .mapIndexed { index, byte ->
                        (byte.toInt() xor mask[index % 4].toInt()).toByte()
                    }
                    .toByteArray(),
            )
            // A fragmented text message with an interleaved ping, then binary.
            output.write(
                byteArrayOf(
                    1,
                    2,
                    104,
                    101,
                    0x89.toByte(),
                    1,
                    33,
                    0x80.toByte(),
                    3,
                    108,
                    108,
                    111,
                    0x82.toByte(),
                    2,
                    0,
                    1,
                )
            )
            output.flush()
            assertEquals(0x8a, input.readUnsignedByte())
            assertEquals(0x81, input.readUnsignedByte())
            input.readFully(mask)
            assertEquals(33, input.readUnsignedByte() xor (mask[0].toInt() and 255))
            assertEquals("hello", events.poll(3, TimeUnit.SECONDS))
            assertEquals("binary:0, 1", events.poll(3, TimeUnit.SECONDS))
            transport.close(1000, "done")
            assertEquals(0x88, input.readUnsignedByte())
            assertEquals(0x86, input.readUnsignedByte())
            input.readFully(mask)
            val close =
                ByteArray(6)
                    .also(input::readFully)
                    .mapIndexed { index, byte ->
                        (byte.toInt() xor mask[index % 4].toInt()).toByte()
                    }
                    .toByteArray()
            org.junit.jupiter.api.Assertions.assertArrayEquals(
                byteArrayOf(3, -24, 100, 111, 110, 101),
                close,
            )
            output.write(byteArrayOf(0x88.toByte(), 2, 3, -24))
            output.flush()
            assertEquals("closed:1000", events.poll(3, TimeUnit.SECONDS))
        }
    }

    @Test
    fun `server close delivers exactly one onClosed and no failure`() {
        withPeer { _, peer, events ->
            val input = java.io.DataInputStream(peer.getInputStream())
            acceptUpgrade(input, peer.getOutputStream())
            assertEquals("open", events.poll(3, TimeUnit.SECONDS))
            peer.getOutputStream().write(byteArrayOf(0x88.toByte(), 2, 3, -24))
            peer.getOutputStream().flush()
            assertEquals("closed:1000", events.poll(3, TimeUnit.SECONDS))
            assertEquals(null, events.poll(300, TimeUnit.MILLISECONDS))
        }
    }

    @Test
    fun `upgrade errors carry status and invalid accept is rejected`() {
        withPeer { _, peer, events ->
            readHeaders(java.io.DataInputStream(peer.getInputStream()))
            peer
                .getOutputStream()
                .write("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n".toByteArray())
            assertEquals("failure:403", events.poll(3, TimeUnit.SECONDS))
        }
        withPeer { _, peer, events ->
            readHeaders(java.io.DataInputStream(peer.getInputStream()))
            peer
                .getOutputStream()
                .write(
                    "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: wrong\r\n\r\n"
                        .toByteArray()
                )
            assertEquals("failure:null", events.poll(3, TimeUnit.SECONDS))
        }
    }

    @Test
    fun `cancel interrupts a pending upgrade without listener failure`() {
        withPeer { transport, peer, events ->
            val input = java.io.DataInputStream(peer.getInputStream())
            readHeaders(input)
            transport.cancel()
            assertEquals(-1, input.read())
            assertEquals(null, events.poll(100, TimeUnit.MILLISECONDS))
        }
    }

    private fun withPeer(
        block: (WebSocketTransport, java.net.Socket, LinkedBlockingQueue<String>) -> Unit
    ) {
        java.net.ServerSocket(0, 1, java.net.InetAddress.getLoopbackAddress()).use { server ->
            server.soTimeout = 3000
            val url = "ws://localhost:${server.localPort}/speech"
            val transport = webSocketTransport(url)
            val events = LinkedBlockingQueue<String>()
            transport.open(
                url,
                emptyMap(),
                null,
                object : WebSocketTransport.Listener {
                    override fun onOpen() {
                        events.add("open")
                    }

                    override fun onText(text: String) {
                        events.add(text)
                    }

                    override fun onBinary(bytes: ByteArray) {
                        events.add("binary:${bytes.joinToString()}")
                    }

                    override fun onClosed(code: Int, reason: String) {
                        events.add("closed:$code")
                    }

                    override fun onFailure(error: Throwable, statusCode: Int?) {
                        events.add("failure:$statusCode")
                    }
                },
            )
            try {
                server.accept().use { peer ->
                    peer.soTimeout = 3000
                    block(transport, peer, events)
                }
            } finally {
                transport.cancel()
            }
        }
    }

    private fun readHeaders(input: java.io.DataInputStream): String {
        val bytes = java.io.ByteArrayOutputStream()
        while (!bytes.toString("US-ASCII").endsWith("\r\n\r\n")) bytes.write(
            input.readUnsignedByte()
        )
        return bytes.toString("US-ASCII")
    }

    private fun acceptUpgrade(input: java.io.DataInputStream, output: java.io.OutputStream) {
        val request = readHeaders(input)
        val key =
            request
                .split("\r\n")
                .first { it.startsWith("Sec-WebSocket-Key:") }
                .substringAfter(':')
                .trim()
        val accept =
            java.util.Base64.getEncoder()
                .encodeToString(
                    java.security.MessageDigest.getInstance("SHA-1")
                        .digest((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").toByteArray())
                )
        output.write(
            "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: $accept\r\n\r\n"
                .toByteArray()
        )
        output.flush()
    }
}
