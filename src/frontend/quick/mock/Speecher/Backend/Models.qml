pragma Singleton
import QtQuick
import Speecher.Launch
import Speecher.Theme

// Mock of what core knows about speech: the services that transcribe, with
// their ratings and whether each can run now; the Local Model catalog with
// downloads, speed tests and this computer's hardware; and the speech Custom
// Endpoint's connection test.
//
// Mock states from the command line:
//   --speech-service=<id>       the chosen service
//   --fallbacks=none|<a,b>      the fallback order
//   --models=empty              nothing downloaded
//   --endpoint-test=ok|failed   a finished connection test (fills the URL)
//   --accel=<backend>           the Acceleration choice
QtObject {
    id: models


    // Services ------------------------------------------------------------

    // The one list of speech services: every other list of them (setup,
    // profiles, Transcribe) derives from it. In the order core lists them:
    // by label. bars is null for a service with no rating; ratingSubject
    // names what was rated when it runs here. access says what it needs.
    readonly property var speechServices: [
        { id: "codex", label: "ChatGPT Codex", icon: "message-circle", recommended: true,
          access: "Uses your sign-in from the Codex app.",
          summary: "Very accurate, around 100 languages.",
          textShows: "A phrase at a time",
          bars: [{ label: "Accuracy", value: 8.5 }, { label: "Speed", value: 7 }],
          models: [{ name: "GPT Live Transcribe", note: "Writes each phrase as you pause." },
                   { name: "GPT Transcribe", note: "Goes over the whole recording when you stop, while Transcribe again for accuracy is on." }] },
        { id: "claude", label: "Claude Voice", icon: "audio-lines",
          access: "Uses your Claude Code sign-in.",
          summary: "About 60 languages, automatic punctuation and numerals.",
          textShows: "Words as you speak",
          bars: [{ label: "Accuracy", value: 5.5 }, { label: "Speed", value: 10 }],
          models: [{ name: "Deepgram Nova 3", note: "Writes your words as you speak." }] },
        { id: "endpoint", label: "Custom Endpoint", icon: "server",
          access: "Your own speech server.",
          summary: "A server you run. Speed, accuracy and languages depend on the server and its model.",
          textShows: "Text after you stop",
          bars: null,
          models: [] },
        { id: "local", label: "Local Model", icon: "cpu",
          access: "No account, works offline after a one-time download.",
          summary: "Runs on this computer: no account, works offline after a one-time download.",
          bars: null,
          models: [] }
    ]

    function service(id) { return speechServices.find(s => s.id === id) || { id: id, label: id, icon: "circle", bars: null, textShows: "", models: [] } }

    // A service's bars; a Local Model is rated by the model it would use.
    function serviceBars(id) {
        if (id !== "local") return service(id).bars
        const m = catalogModel(localModelChoice)
        return m ? m.bars : null
    }
    // How a service's text arrives; a Local Model's depends on its model.
    function serviceTextShows(id) {
        if (id !== "local") return service(id).textShows
        const m = catalogModel(localModelChoice)
        return m && m.streams ? "Words as you speak" : "Text after you stop"
    }
    function ratingSubject(id) {
        if (id !== "local") return ""
        const m = catalogModel(localModelChoice)
        return m ? m.name + " on this computer" : ""
    }
    // The model the Local Model service runs: the chosen one, else the
    // suggestion for this computer.
    readonly property string localModelChoice: Settings.values.speechLocalModel || suggestedModel

    // Why a service can't run right now, or "".
    function problem(id) {
        if (id === "local" && downloaded.length === 0) return "No model downloaded"
        if (id === "endpoint" && !(Settings.values.speechEndpointUrl || "")) return "No server URL is set."
        return ""
    }

    // What the fallback list says under each fallback: { text, tone }.
    function fallbackStatus(id, index) {
        const p = problem(id)
        if (p) return { text: id === "endpoint" ? p : p + ", so it can't stand in yet.", tone: "negative" }
        const primary = service(Settings.values.speechProvider).label
        return { text: index === 0 ? "Used when " + primary + " is unavailable." : "Used when the ones above are unavailable.",
                 tone: "neutral" }
    }

    // The sign-in a speech service uses, or null for one that needs none.
    function account(id) { return id === "codex" ? Accounts.service("openai") : id === "claude" ? Accounts.service("anthropic") : null }
    // Whether a service can dictate now: { state, status }. state is ready,
    // signedOut, download or setup, as the setup assistant shows it.
    function readiness(id) {
        const account = models.account(id)
        if (account) {
            return account.state === "signedIn" ? { state: "ready", status: "Ready" } : { state: "signedOut", status: "Not signed in" }
        }
        if (id === "local") {
            if (downloaded.length) return { state: "ready", status: "Ready" }
            const fraction = downloads[localModelChoice]
            return { state: "download", status: fraction !== undefined ? "Downloading, " + Math.round(fraction * 100) + "%"
                                                                        : catalogModel(localModelChoice).size + " download" }
        }
        return problem(id) ? { state: "setup", status: "Needs a URL" } : { state: "ready", status: "Ready" }
    }
    // The services as setup offers them: what each needs, and where it stands.
    readonly property var serviceChoices: speechServices.map(s => Object.assign(
        { id: s.id, label: s.label, help: s.access, recommended: !!s.recommended }, readiness(s.id)))
    // Downloaded models as a choice, for a profile's own speech model.
    readonly property var downloadedModels: downloaded.map(id => ({ id: id, label: catalogModel(id).name }))

    // Spoken languages the English-only catalog models can't hear.
    function spokenLanguageProblem() {
        const language = Settings.values.spokenLanguage
        if (Settings.values.speechProvider !== "local") return ""
        const m = catalogModel(localModelChoice)
        if (!m || m.multilingual) return ""
        if (language === "auto") return m.name + " can't detect the language. Choose the language you speak."
        if (language === "en") return ""
        const option = Schema.row("spokenLanguage").options.find(o => o.id === language)
        return m.name + " can't listen for " + (option ? option.label.split(" (")[0] : language) + ". Choose another Spoken language."
    }

    // This computer ----------------------------------------------------------

    // What the ratings, speeds and fits are measured against.
    readonly property string computer: "AMD Ryzen 9 7950X, NVIDIA GeForce RTX 4070 with 12 GB, 64 GB of memory"
    readonly property var gpus: [
        { id: "cuda0", backend: "cuda", label: "NVIDIA GeForce RTX 4070" },
        { id: "vulkan0", backend: "vulkan", label: "NVIDIA GeForce RTX 4070" },
        { id: "vulkan1", backend: "vulkan", label: "AMD Radeon Graphics" }
    ]
    // The cards the chosen acceleration reaches; the row shows when there is
    // a choice to make.
    readonly property var graphicsCards: gpus.filter(g => g.backend === Settings.values.localAcceleration)
                                             .map(g => ({ id: g.id, label: g.label }))
    readonly property string runningOn: "NVIDIA GeForce RTX 4070 (CUDA)"
    readonly property string modelFolder: "~/.local/share/speecher/models"
    function openModelFolder() { System.open(modelFolder) }

    // The catalog --------------------------------------------------------------

    // Facts as core's LocalModelCatalog and ProviderRatings give them.
    // ratingLabel: Recommended, Good, Situational or Not recommended. fit: fits,
    // tight, tooLarge.
    readonly property var catalog: [
        { id: "parakeet", name: "Parakeet 0.6B", ratingLabel: "Recommended",
          bestFor: "Most people", sizeBytes: 731357568, size: "731 MB", fit: "fits", fitLabel: "Fits",
          werClear: "1.60%", werEveryday: "3.99%", textShows: "As you speak", streams: true,
          languages: "English", multilingual: false, licence: "NVIDIA Open Model License",
          fileName: "parakeet-unified-en-0.6b-Q8_0.gguf",
          speed: "About 0.4 s per 10 s (estimated)", measuredSpeed: "0.3 s per 10 s (measured)",
          bars: [{ label: "Accuracy", value: 5.5 }, { label: "Speed", value: 9.5 }],
          pros: ["Words appear as you speak", "Fewest mistakes of the models that show words as you speak", "Quick even on older laptops"],
          cons: ["More mistakes than Qwen3-ASR on everyday speech"] },
        { id: "moonshine-small", name: "Moonshine Small", ratingLabel: "Good",
          bestFor: "Computers short on memory", sizeBytes: 198506848, size: "199 MB", fit: "fits", fitLabel: "Fits",
          werClear: "2.54%", werEveryday: "8.55%", textShows: "As you speak", streams: true,
          languages: "English", multilingual: false, licence: "MIT",
          fileName: "moonshine-streaming-small-Q8_0.gguf",
          speed: "About 0.2 s per 10 s (estimated)",
          bars: [{ label: "Accuracy", value: 4 }, { label: "Speed", value: 9.5 }],
          pros: ["Smallest download", "Words appear as you speak"],
          cons: ["About twice the mistakes of Parakeet on everyday speech"] },
        { id: "moonshine-medium", name: "Moonshine Medium", ratingLabel: "Good",
          bestFor: "Computers short on memory", sizeBytes: 295793568, size: "296 MB", fit: "fits", fitLabel: "Fits",
          werClear: "2.16%", werEveryday: "7.87%", textShows: "As you speak", streams: true,
          languages: "English", multilingual: false, licence: "MIT",
          fileName: "moonshine-streaming-medium-Q8_0.gguf",
          speed: "About 0.3 s per 10 s (estimated)",
          bars: [{ label: "Accuracy", value: 5 }, { label: "Speed", value: 9.5 }],
          pros: ["Words appear as you speak", "Fewer mistakes than Moonshine Small"],
          cons: ["About twice the mistakes of Parakeet on everyday speech"] },
        { id: "whisper-turbo", name: "Whisper Large v3 Turbo", ratingLabel: "Situational",
          bestFor: "Few mistakes in under 1 GB, if you can wait", sizeBytes: 886381760, size: "886 MB", fit: "fits", fitLabel: "Fits",
          werClear: "2.01%", werEveryday: "4.38%", textShows: "After you stop", streams: false,
          languages: "99 languages", multilingual: true, licence: "Apache 2.0",
          fileName: "whisper-large-v3-turbo-Q8_0.gguf",
          speed: "About 0.6 s per 10 s (estimated)",
          bars: [{ label: "Accuracy", value: 7.5 }, { label: "Speed", value: 7.5 }],
          pros: ["Fewer mistakes than the Moonshine models on everyday speech"],
          cons: ["Text appears after you stop", "More mistakes than Parakeet on everyday speech", "Now and then leaves out punctuation"] },
        { id: "qwen3-asr", name: "Qwen3-ASR 1.7B", ratingLabel: "Good",
          bestFor: "Fewest mistakes, if you can wait", sizeBytes: 2185030624, size: "2.2 GB", fit: "fits", fitLabel: "Fits",
          werClear: "1.62%", werEveryday: "3.23%", textShows: "After you stop", streams: false,
          languages: "30 languages", multilingual: true, licence: "Apache 2.0",
          fileName: "Qwen3-ASR-1.7B-Q8_0.gguf",
          speed: "About 0.8 s per 10 s (estimated)",
          bars: [{ label: "Accuracy", value: 9 }, { label: "Speed", value: 7.5 }],
          pros: ["Fewest mistakes on everyday speech"],
          cons: ["2.2 GB download", "Text appears after you stop"] },
        { id: "cohere", name: "Cohere Transcribe", ratingLabel: "Situational",
          bestFor: "Clear read-aloud speech, if you can wait", sizeBytes: 2410655232, size: "2.4 GB", fit: "fits", fitLabel: "Fits",
          werClear: "1.27%", werEveryday: "5.08%", textShows: "After you stop", streams: false,
          languages: "14 languages", multilingual: true, licence: "Apache 2.0",
          fileName: "cohere-transcribe-03-2026-Q8_0.gguf",
          speed: "About 0.5 s per 10 s (estimated)",
          bars: [{ label: "Accuracy", value: 7.5 }, { label: "Speed", value: 8 }],
          pros: ["Fewest mistakes on clear speech"],
          cons: ["2.4 GB download", "Text appears after you stop", "More mistakes than Parakeet on everyday speech"] },
        { id: "voxtral-small", name: "Voxtral Small 24B", ratingLabel: "Not recommended",
          bestFor: "Nobody: Qwen3-ASR does as well at an eighth of the size", sizeBytes: 17138659808, size: "17.1 GB",
          fit: "tooLarge", fitLabel: "Too large",
          werClear: "1.60%", werEveryday: "3.55%", textShows: "After you stop", streams: false,
          languages: "8 languages", multilingual: true, licence: "Apache 2.0",
          fileName: "Voxtral-Small-24B-2507-Q5_K_M.gguf",
          speed: "Not measured on a computer like this one yet",
          bars: [{ label: "Accuracy", value: 9 }, { label: "Speed", value: null }],
          pros: ["Few mistakes on everyday speech"],
          cons: ["17 GB download", "Text appears after you stop", "Needs a 24 GB graphics card or a Mac with 32 GB or more", "Slow: several seconds after you stop"],
          problem: "Needs about 23 GB of graphics memory; this computer's card has 12 GB." }
    ]
    readonly property string suggestedModel: "parakeet"
    function catalogModel(id) { return catalog.find(m => m.id === id) || null }
    // A downloaded model's Speed Test result, else the estimate.
    function speedLine(m) { return downloaded.indexOf(m.id) >= 0 && m.measuredSpeed ? m.measuredSpeed : m.speed }
    // The line under a model's name: languages and how its text arrives.
    function metaLine(m) {
        return m.languages + ", text " + m.textShows.toLowerCase() + (m.fit === "tight" ? ", " + m.fitLabel.toLowerCase() : "")
    }

    // The model browser's fixed wording.
    readonly property var text: ({
        useModel: "Use this model", testSpeed: "Test speed", inUse: "In use",
        suggested: "Suggested for this computer", tooLarge: "Too large for this computer",
        ratedFor: "Ratings, speeds and fits are for this computer: %1.",
        compareNote: "Word errors are measured on clear read speech and on everyday speech. Times are estimates until a model is downloaded and tested here.",
        deleteModel: "Delete model",
        deleteBody: "The model file is removed from this computer. You can download it again later.",
        deleteQuestion: "Delete %1?", download: "Download %1", progress: "%1 of %2",
        bestFor: "Best for", wordErrors: "Word errors", wordErrorsValue: "%1 clear, %2 everyday", speed: "10 s of speech",
        textShows: "Text shows", languages: "Languages", licence: "Licence", file: "File",
        fallback: "Fallback %1", unrated: "No rating"
    })
    // Where a speech service stands in the chain: 0 for the service itself,
    // then its place among the fallbacks.
    function roleText(place) { return place === 0 ? "Used as your service." : "Used as fallback " + place + "." }
    // " Rated as parakeet on this computer.", after the role, or nothing.
    function ratedAsText(subject) { return subject ? " Rated as " + subject + "." : "" }

    // The library's filter: every model, or only those on this computer.
    readonly property var libraryFilters: [{ id: "all", label: "All" }, { id: "downloaded", label: "Downloaded" }]

    // Model state ----------------------------------------------------------------

    property var downloaded: Launch.arg("models", "") === "empty" ? [] : ["parakeet", "moonshine-small"]
    // id -> fraction downloaded, for downloads in progress.
    property var downloads: Launch.arg("models", "") === "empty" ? ({}) : ({ "qwen3-asr": 0.43 })
    property string speedTestRunning: ""
    // The model dictation uses: empty while another service is chosen or
    // the chosen model isn't downloaded.
    readonly property string inUse: Settings.values.speechProvider === "local" && downloaded.indexOf(localModelChoice) >= 0
                                    ? localModelChoice : ""

    function download(id) {
        const next = Object.assign({}, downloads)
        next[id] = 0
        downloads = next
    }
    function cancel(id) {
        const next = Object.assign({}, downloads)
        delete next[id]
        downloads = next
    }
    function remove(id) {
        downloaded = downloaded.filter(x => x !== id)
        if (Settings.values.speechLocalModel === id) Settings.set("speechLocalModel", "")
    }
    function use(id) {
        Settings.set("speechLocalModel", id)
        Settings.set("speechProvider", "local")
    }
    function testSpeed(id) {
        speedTestRunning = id
        _speedTest.restart()
    }
    function formatBytes(bytes) {
        return bytes >= 1e9 ? (bytes / 1e9).toFixed(1) + " GB" : Math.round(bytes / 1e6) + " MB"
    }

    property Timer _progress: Timer {
        interval: Theme.ms(120)
        repeat: true
        running: Object.keys(models.downloads).length > 0
        onTriggered: {
            const next = {}
            const done = []
            for (const id in models.downloads) {
                const v = models.downloads[id] + 0.004 + Math.random() * 0.006
                if (v >= 1) done.push(id)
                else next[id] = v
            }
            models.downloads = next
            if (done.length) models.downloaded = models.downloaded.concat(done)
        }
    }
    property Timer _speedTest: Timer {
        interval: Theme.ms(2200)
        onTriggered: models.speedTestRunning = ""
    }

    // The speech Custom Endpoint's connection test -----------------------------

    // idle, testing, ok or failed, with the verdict core words.
    property string endpointTest: Launch.arg("endpoint-test", "idle")
    readonly property string endpointStatus: endpointTest === "testing" ? "Checking…"
        : endpointTest === "ok" ? "Connected. The server lists 3 models."
        : endpointTest === "failed" ? "Couldn't connect to " + (Settings.values.speechEndpointUrl || "the server") + ": connection refused."
        : "Not tested yet."
    readonly property var endpointModels: endpointTest === "ok" ? ["whisper-large-v3-turbo", "distil-large-v3", "whisper-1"] : []
    function testEndpoint() {
        endpointTest = "testing"
        _endpointCheck.restart()
    }
    property Timer _endpointCheck: Timer {
        interval: Theme.ms(1400)
        onTriggered: models.endpointTest = (Settings.values.speechEndpointUrl || "") ? "ok" : "failed"
    }

    Component.onCompleted: Qt.callLater(() => {
        const service = Launch.arg("speech-service", "")
        if (service) Settings.set("speechProvider", service)
        const fallbacks = Launch.arg("fallbacks", "")
        if (fallbacks) Settings.set("speechFallbackList", fallbacks === "none" ? [] : fallbacks.split(","))
        if (endpointTest !== "idle") Settings.set("speechEndpointUrl", "http://192.168.1.20:8080")
        const accel = Launch.arg("accel", "")
        if (accel) Settings.set("localAcceleration", accel)
    })
}
