package app.speecher.protocol

import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

class HostnameVerificationTest {
    private fun dns(vararg names: String) = names.map { listOf(2, it) }

    @Test
    fun `exact DNS name matches`() {
        assertTrue(hostnameMatches("claude.ai", dns("claude.ai")))
        assertTrue(hostnameMatches("Claude.AI", dns("claude.ai")))
        assertFalse(hostnameMatches("evil.com", dns("claude.ai")))
    }

    @Test
    fun `no SAN never matches`() {
        assertFalse(hostnameMatches("claude.ai", emptyList()))
    }

    @Test
    fun `left-most wildcard matches exactly one label`() {
        assertTrue(hostnameMatches("api.claude.ai", dns("*.claude.ai")))
        assertFalse(hostnameMatches("claude.ai", dns("*.claude.ai")))
        assertFalse(hostnameMatches("a.b.claude.ai", dns("*.claude.ai")))
    }

    @Test
    fun `wildcards are only valid as a single left-most label`() {
        assertFalse(matchesDnsName("a.claude.ai", "a.*.ai"))
        assertFalse(matchesDnsName("a.b.ai", "*.*.ai"))
        assertTrue(matchesDnsName("api.claude.ai", "*.claude.ai"))
        assertFalse(matchesDnsName("claude.ai", "*.claude.ai"))
    }

    @Test
    fun `IP host matches only an IP SAN, and DNS host ignores IP SANs`() {
        assertTrue(hostnameMatches("93.184.216.34", listOf(listOf(7, "93.184.216.34"))))
        assertFalse(hostnameMatches("93.184.216.34", dns("93.184.216.34")))
        assertFalse(hostnameMatches("claude.ai", listOf(listOf(7, "1.2.3.4"))))
    }
}
