package app.speecher.android

import java.io.IOException
import java.net.InetAddress
import mockwebserver3.MockResponse
import mockwebserver3.MockWebServer
import okhttp3.OkHttpClient
import okhttp3.Request
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class LocalNetworkTest {
    @Test
    fun `private, link-local, unique local and mDNS hosts are on the local network`() {
        val servers =
            listOf(
                "http://192.168.1.20:8080" to true,
                "http://10.0.0.5" to true,
                "http://172.20.1.1/v1" to true,
                "http://169.254.10.1" to true,
                "http://[fd12::1]:9000" to true,
                "http://whisper.local:8080" to true,
                "https://8.8.8.8" to false,
                "http://127.0.0.1:8080" to false,
            )
        assertEquals(servers, servers.map { (server, _) -> server to onLocalNetwork(server) })
    }

    @Test
    fun `an API key goes over plain http only to the local network or this device`() {
        val servers =
            listOf(
                "http://192.168.1.20:8080" to false,
                "http://127.0.0.1:8080" to false,
                "http://localhost:8080" to false,
                "https://8.8.8.8" to false,
                "http://8.8.8.8" to true,
            )
        assertEquals(servers, servers.map { (server, _) -> server to exposesApiKey(server, "k") })
        assertEquals(false, exposesApiKey("http://8.8.8.8", ""))
    }

    @Test
    fun `a keyed plain http request connects only to the local network or this device`() {
        MockWebServer().use { server ->
            server.enqueue(MockResponse())
            server.start()
            val url = "http://whisper.local:${server.port}"
            // whisper.local answered with these addresses as the request connects.
            fun status(vararg addresses: String): Int {
                val http =
                    OkHttpClient.Builder().dns { addresses.map(InetAddress::getByName) }.build()
                return keyedEndpointHttp(http, url, "k")
                    .newCall(Request.Builder().url("$url/v1/models").build())
                    .execute()
                    .use { it.code }
            }

            assertEquals(200, status("8.8.8.8", "127.0.0.1"))
            assertEquals(1, server.requestCount)
            val refused = assertThrows(IOException::class.java) { status("8.8.8.8") }
            assertEquals(KEY_NEEDS_LOCAL_NETWORK, refused.cause?.message)
        }
    }
}
