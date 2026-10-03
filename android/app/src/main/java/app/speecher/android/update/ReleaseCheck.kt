package app.speecher.android.update

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.jsonPrimitive
import okhttp3.OkHttpClient
import okhttp3.Request

data class ApkUpdate(val version: String, val downloadUrl: String)

/**
 * Android releases are tagged `android-v<versionName>` and never marked latest, because the desktop
 * app's updater owns the repository's latest release.
 */
private const val TAG_PREFIX = "android-v"

/** The highest Android release, if it is newer than [installedVersion] and has an APK attached. */
fun newerApk(
    http: OkHttpClient,
    installedVersion: String,
    releasesUrl: String =
        "https://api.github.com/repos/firemonster612/speecher/releases?per_page=100",
): ApkUpdate? {
    val request =
        Request.Builder().url(releasesUrl).header("Accept", "application/vnd.github+json").build()
    http.newCall(request).execute().use { response ->
        if (!response.isSuccessful) error("Could not check releases: HTTP ${response.code}")
        // Pick by version: GitHub orders the list by tagged commit date, not by version.
        val (version, release) =
            (Json.parseToJsonElement(response.body.string()) as JsonArray)
                .mapNotNull { it as? JsonObject }
                .filter { it["prerelease"]?.jsonPrimitive?.booleanOrNull != true }
                .mapNotNull { release ->
                    val tag = release["tag_name"]?.jsonPrimitive?.content ?: return@mapNotNull null
                    if (tag.startsWith(TAG_PREFIX)) tag.removePrefix(TAG_PREFIX) to release
                    else null
                }
                .reduceOrNull { best, next -> if (isNewer(next.first, best.first)) next else best }
                ?: return null
        if (!isNewer(version, installedVersion)) return null
        val assets = release["assets"] as? JsonArray ?: return null
        val apk =
            assets
                .mapNotNull { it as? JsonObject }
                .firstOrNull {
                    it["name"]?.jsonPrimitive?.content?.endsWith(".apk", ignoreCase = true) == true
                } ?: return null
        val url = apk["browser_download_url"]?.jsonPrimitive?.content ?: return null
        return ApkUpdate(version, url)
    }
}

private fun isNewer(candidate: String, installed: String): Boolean {
    val next = candidate.split('.').map { it.toIntOrNull() ?: return false }
    val current = installed.removePrefix("v").split('.').map { it.toIntOrNull() ?: return false }
    for (index in 0 until maxOf(next.size, current.size)) {
        val comparison = (next.getOrElse(index) { 0 }).compareTo(current.getOrElse(index) { 0 })
        if (comparison != 0) return comparison > 0
    }
    return false
}
