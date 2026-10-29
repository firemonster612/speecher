package app.speecher.android.provider

import app.speecher.protocol.OAuthProvider

/** The two accounts Speecher can sign in to. Each one can transcribe and refine. */
enum class Provider {
    Claude,
    ChatGpt,
}

val Provider.oauth: OAuthProvider
    get() = if (this == Provider.Claude) OAuthProvider.Claude else OAuthProvider.ChatGpt

/** The provider's name as people know it, in the app and on the browser's sign-in page. */
val Provider.label: String
    get() =
        when (this) {
            Provider.Claude -> "Claude"
            Provider.ChatGpt -> "ChatGPT"
        }

/**
 * Who turns speech into the transcript: an account's speech service, or a server the person chose,
 * which has no [account]. The values are saved by name, the names the accounts were saved by
 * before.
 */
enum class SpeechProvider(val account: Provider?) {
    ChatGpt(Provider.ChatGpt),
    Claude(Provider.Claude),
    Endpoint(null),
}

/** The provider's name in the app and the panel. */
val SpeechProvider.label: String
    get() = account?.label ?: "Custom Endpoint"

/** Who cleans up the transcript. As with [SpeechProvider], each one is an account for now. */
enum class CleanupProvider(val account: Provider) {
    ChatGpt(Provider.ChatGpt),
    Claude(Provider.Claude),
}

/** The account's speech service. */
val Provider.speech: SpeechProvider
    get() = SpeechProvider.entries.first { it.account == this }

/** The account's cleanup service. */
val Provider.cleanup: CleanupProvider
    get() = CleanupProvider.entries.first { it.account == this }

/** Only ChatGPT has a batch speech-to-text pass; both Insert buttons can run it. */
val SpeechProvider.hasBatchTranscription: Boolean
    get() = this == SpeechProvider.ChatGpt

/** A Custom Endpoint transcribes the whole recording once it stops, so no words show before. */
val SpeechProvider.transcribesAfterStop: Boolean
    get() = this == SpeechProvider.Endpoint

/**
 * The order providers are listed in everywhere in the UI: alphabetical, chosen deliberately so it
 * is not the accident of enum declaration order and carries no bias toward either account.
 */
val providerOrder = listOf(Provider.ChatGpt, Provider.Claude)

/**
 * The provider to actually use: the one the user picked if they are signed into it, otherwise the
 * first signed-in provider in [providerOrder]. Keeps dictation working when only the other account
 * is connected.
 */
fun resolveSignedIn(preferred: Provider, signedIn: Set<Provider>): Provider =
    if (preferred in signedIn) preferred
    else providerOrder.firstOrNull { it in signedIn } ?: preferred

/**
 * The speech provider a dictation uses: a server as chosen, an account as [resolveSignedIn] has it.
 */
fun resolveSpeech(preferred: SpeechProvider, signedIn: Set<Provider>): SpeechProvider =
    preferred.account?.let { resolveSignedIn(it, signedIn).speech } ?: preferred

/**
 * The provider a fresh install defaults to: whichever account the user is signed into, or the first
 * in [providerOrder] when none is (no hardcoded Claude bias).
 */
fun defaultProvider(signedIn: Set<Provider>): Provider =
    providerOrder.firstOrNull { it in signedIn } ?: providerOrder.first()

/** The model and reasoning effort one provider refines with, as the API ids it sends. */
data class RefinementChoice(val model: String, val effort: String)

/**
 * Desktop's efforts: no reasoning for OpenAI, low for Claude. The OpenAI model is desktop's too,
 * but Claude defaults to Sonnet 5.5 where the desktop defaults to Opus 5.5.
 */
val Provider.defaultRefinement: RefinementChoice
    get() =
        when (this) {
            Provider.ChatGpt -> RefinementChoice("gpt-6-luna", "none")
            Provider.Claude -> RefinementChoice("claude-sonnet-5-5", "low")
        }

/**
 * The refinement models Settings suggests, as API id to label, in the desktop's order. Any other
 * model id can be typed instead.
 */
val Provider.refinementModels: Map<String, String>
    get() =
        when (this) {
            Provider.ChatGpt ->
                mapOf(
                    "gpt-6-luna" to "GPT-6 Luna",
                    "gpt-6.1-sol" to "GPT-6.1 Sol",
                    "gpt-6-astra" to "GPT-6 Astra",
                    "gpt-5.6-luna" to "GPT-5.6 Luna",
                    "gpt-5.6-terra" to "GPT-5.6 Terra",
                    "gpt-5.5" to "GPT-5.5",
                    "gpt-5.4-nano" to "GPT-5.4 nano",
                    "gpt-5.4-mini" to "GPT-5.4 mini",
                    "gpt-5.4" to "GPT-5.4",
                )
            Provider.Claude ->
                mapOf(
                    "claude-opus-5-5" to "Claude Opus 5.5",
                    "claude-opus-5" to "Claude Opus 5",
                    "claude-sonnet-5-5" to "Claude Sonnet 5.5",
                    "claude-haiku-5-5" to "Claude Haiku 5.5",
                )
        }

/**
 * The efforts Settings offers, as API id to label, as on the desktop. OpenAI's `reasoning.effort`
 * can turn reasoning off and goes as chosen, so a model may reject one, except that GPT-6.1 Sol,
 * which refuses None, gets Low; Anthropic's `output_config.effort` starts at low, and a model
 * without the chosen one gets the nearest it has.
 */
val Provider.refinementEfforts: Map<String, String>
    get() =
        when (this) {
            Provider.ChatGpt ->
                mapOf(
                    "none" to "None",
                    "low" to "Low",
                    "medium" to "Medium",
                    "high" to "High",
                    "xhigh" to "Extra high",
                )
            Provider.Claude ->
                mapOf(
                    "low" to "Low",
                    "medium" to "Medium",
                    "high" to "High",
                    "xhigh" to "Extra high",
                    "max" to "Max",
                )
        }

/** How fast ChatGPT refines; the faster tiers use more of the plan's usage. */
enum class OpenAiSpeed(val label: String) {
    Standard("Standard"),
    Fast("Fast"),
    /** Only for models [app.speecher.protocol.modelSupportsUltrafast] names; Fast on the rest. */
    Ultrafast("Ultrafast"),
}
