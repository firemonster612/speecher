package app.speecher.android

import org.junit.Assert.assertEquals
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
}
