package app.speecher.android.update

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.jsonPrimitive
import okhttp3.OkHttpClient
import okhttp3.Request

/** Which builds the app offers: hand-tested releases, or a build of every push to master. */
enum class UpdateChannel(val label: String, val description: String, val feed: String) {
    Stable("Stable", "Hand-tested releases.", "Stable Release"),
    Nightly("Nightly", "Untested builds from every push to master.", "Nightly Build"),
}

data class ApkUpdate(val version: String, val downloadUrl: String, val pageUrl: String)

/** One Android release: its version, its GitHub notes and page, and its APK once attached. */
data class AndroidRelease(
    val version: String,
    val notes: String,
    val pageUrl: String,
    val apkUrl: String?,
    val nightly: Boolean = false,
)

/**
 * Android releases are tagged `android-v<versionName>` and never marked latest, because the desktop
 * app's updater owns the repository's latest release.
 */
private const val TAG_PREFIX = "android-v"

/**
 * Every master build replaces the prerelease under this tag. Its notes start with a `Version:`
 * line, since the tag does not carry one.
 */
private const val NIGHTLY_TAG = "android-nightly"

private const val NIGHTLY_VERSION_LINE = "Version: "

/** Every Android release in the repository's release list. Call on a worker thread. */
fun androidReleases(
    http: OkHttpClient,
    releasesUrl: String =
        "https://api.github.com/repos/firemonster612/speecher/releases?per_page=100",
): List<AndroidRelease> {
    val request =
        Request.Builder().url(releasesUrl).header("Accept", "application/vnd.github+json").build()
    http.newCall(request).execute().use { response ->
        if (!response.isSuccessful) error("Could not check releases: HTTP ${response.code}")
        return (Json.parseToJsonElement(response.body.string()) as JsonArray).mapNotNull {
            (it as? JsonObject)?.let(::androidRelease)
        }
    }
}

private fun androidRelease(release: JsonObject): AndroidRelease? {
    val tag = release.text("tag_name") ?: return null
    val notes = release.text("body").orEmpty()
    val prerelease = release["prerelease"]?.jsonPrimitive?.booleanOrNull == true
    val version =
        when {
            tag == NIGHTLY_TAG ->
                notes
                    .lineSequence()
                    .firstOrNull { it.startsWith(NIGHTLY_VERSION_LINE) }
                    ?.removePrefix(NIGHTLY_VERSION_LINE)
                    ?.trim()
            tag.startsWith(TAG_PREFIX) && !prerelease -> tag.removePrefix(TAG_PREFIX)
            else -> null
        } ?: return null
    val apk =
        (release["assets"] as? JsonArray)
            ?.mapNotNull { it as? JsonObject }
            ?.firstOrNull { it.text("name")?.endsWith(".apk", ignoreCase = true) == true }
    return AndroidRelease(
        version,
        notes,
        release.text("html_url").orEmpty(),
        apk?.text("browser_download_url"),
        nightly = tag == NIGHTLY_TAG,
    )
}

private fun JsonObject.text(key: String): String? = this[key]?.jsonPrimitive?.content

/**
 * The highest release on [channel], if it is newer than [installedVersion] and has an APK attached.
 * Picked by version, since GitHub orders the list by tagged commit date. The Nightly channel offers
 * Stable Releases too, so a nightly moves on to the release it led up to.
 */
fun newerApk(
    releases: List<AndroidRelease>,
    channel: UpdateChannel,
    installedVersion: String,
): ApkUpdate? {
    val newest =
        releases
            .filter { channel == UpdateChannel.Nightly || !it.nightly }
            .maxWithOrNull { left, right -> compareVersions(left.version, right.version) }
            ?.takeIf { compareVersions(it.version, installedVersion) > 0 } ?: return null
    return newest.apkUrl?.let { ApkUpdate(newest.version, it, newest.pageUrl) }
}

/**
 * What's New: every release after [since] up to [installedVersion], newest first. With no [since],
 * or none in between, the newest release up to [installedVersion] stands in.
 */
fun releaseNotes(
    releases: List<AndroidRelease>,
    since: String?,
    installedVersion: String,
): List<AndroidRelease> {
    val installed =
        releases
            .filter { compareVersions(it.version, installedVersion) <= 0 }
            .sortedWith { left, right -> compareVersions(right.version, left.version) }
    return installed
        .filter { since != null && compareVersions(it.version, since) > 0 }
        .ifEmpty { installed.take(1) }
}

/**
 * The version What's New covers changes since: one not yet seen, else [previousRun] when [current]
 * is an upgrade from it.
 */
fun whatsNewSince(previousRun: String?, current: String, unseen: String?): String? =
    unseen ?: previousRun?.takeIf { compareVersions(current, it) > 0 }

/**
 * Orders versions by semantic-versioning precedence: the numeric core first, then a prerelease such
 * as `0.2.1-nightly.57` below the `0.2.1` it leads up to. Build metadata after `+` is ignored.
 */
fun compareVersions(left: String, right: String): Int {
    val (leftCore, leftPrerelease) = versionParts(left)
    val (rightCore, rightPrerelease) = versionParts(right)
    for (index in 0 until maxOf(leftCore.size, rightCore.size)) {
        val comparison = leftCore.getOrElse(index) { 0 }.compareTo(rightCore.getOrElse(index) { 0 })
        if (comparison != 0) return comparison
    }
    return when {
        leftPrerelease == rightPrerelease -> 0
        leftPrerelease == null -> 1
        rightPrerelease == null -> -1
        else -> comparePrereleases(leftPrerelease, rightPrerelease)
    }
}

private fun versionParts(version: String): Pair<List<Int>, List<String>?> {
    val plain = version.removePrefix("v").substringBefore('+')
    val core = plain.substringBefore('-').split('.').map { it.toIntOrNull() ?: 0 }
    return core to plain.substringAfter('-', "").takeIf { it.isNotEmpty() }?.split('.')
}

private fun comparePrereleases(left: List<String>, right: List<String>): Int {
    for ((leftPart, rightPart) in left.zip(right)) {
        val leftNumber = leftPart.toIntOrNull()
        val rightNumber = rightPart.toIntOrNull()
        val comparison =
            if (leftNumber != null && rightNumber != null) leftNumber.compareTo(rightNumber)
            else leftPart.compareTo(rightPart)
        if (comparison != 0) return comparison
    }
    return left.size.compareTo(right.size)
}

/**
 * How a version reads to people: a nightly as its build number and commit, as on the desktop, and
 * anything else as it is.
 */
fun versionDisplay(version: String): String {
    val build = version.substringAfter("-nightly.", "").substringBefore('+')
    if (build.isEmpty()) return version
    val commit = version.substringAfter('+', "")
    return "nightly build $build" + if (commit.isEmpty()) "" else " ($commit)"
}
