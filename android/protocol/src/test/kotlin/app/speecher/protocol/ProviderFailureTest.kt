package app.speecher.protocol

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonObject
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test

class ProviderFailureTest {
    @Test
    fun `HTTP statuses are classified as on the desktop`() {
        assertEquals(
            listOf(
                ProviderFailureKind.Authentication,
                ProviderFailureKind.Authentication,
                ProviderFailureKind.RateLimited,
                ProviderFailureKind.Server,
                ProviderFailureKind.Other,
                ProviderFailureKind.Other,
            ),
            listOf(401, 403, 429, 503, 400, 404).map(::failureKindForHttpStatus),
        )
    }

    @Test
    fun `a streamed error's code decides before its type`() {
        assertEquals(
            listOf(
                ProviderFailureKind.Authentication,
                ProviderFailureKind.RateLimited,
                ProviderFailureKind.RateLimited,
                ProviderFailureKind.Server,
                ProviderFailureKind.Other,
            ),
            listOf(
                    """{"type":"invalid_request_error","code":"invalid_api_key"}""",
                    """{"type":"server_error","code":429}""",
                    """{"type":"rate_limit_error"}""",
                    """{"type":"overloaded_error"}""",
                    """{"type":"invalid_request_error","code":"model_not_found"}""",
                )
                .map { streamedErrorKind(Json.parseToJsonElement(it).jsonObject) },
        )
    }

    @Test
    fun `only failures another provider could avoid move a dictation on`() {
        assertEquals(
            setOf(
                ProviderFailureKind.Unavailable,
                ProviderFailureKind.Authentication,
                ProviderFailureKind.Network,
                ProviderFailureKind.Timeout,
                ProviderFailureKind.Server,
                ProviderFailureKind.RateLimited,
            ),
            ProviderFailureKind.entries.filter { it.permitsFallback }.toSet(),
        )
    }
}
