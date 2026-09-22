package app.speecher.android.update

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.jsonPrimitive
import okhttp3.OkHttpClient
import okhttp3.Request

data class ApkUpdate(val version: String, val downloadUrl: String)

class ReleaseCheck(private val http: OkHttpClient) {
    fun newerApk(
        installedVersion: String,
        releaseUrl: String = "https://api.github.com/repos/firemonster612/speecher/releases/latest",
    ): ApkUpdate? {
        val request =
            Request.Builder()
                .url(releaseUrl)
                .header("Accept", "application/vnd.github+json")
                .build()
        http.newCall(request).execute().use { response ->
            if (!response.isSuccessful) error("Could not check releases: HTTP ${response.code}")
            val release = Json.parseToJsonElement(response.body.string()) as JsonObject
            val version =
                release["tag_name"]?.jsonPrimitive?.content?.removePrefix("v") ?: return null
            if (!isNewer(version, installedVersion)) return null
            val assets = release["assets"] as? JsonArray ?: return null
            val apk =
                assets
                    .mapNotNull { it as? JsonObject }
                    .firstOrNull {
                        it["name"]?.jsonPrimitive?.content?.endsWith(".apk", ignoreCase = true) ==
                            true
                    } ?: return null
            val url = apk["browser_download_url"]?.jsonPrimitive?.content ?: return null
            return ApkUpdate(version, url)
        }
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
