package app.speecher.android.dictation

import android.graphics.Bitmap
import android.view.accessibility.AccessibilityNodeInfo
import androidx.core.graphics.scale
import java.io.ByteArrayOutputStream
import java.util.Base64
import kotlin.math.roundToInt

/** The target window's title and visible text, read by the chip service at tap. */
data class ScreenCapture(val title: String, val text: String)

/** Enough on-screen text to disambiguate a dictation without flooding the prompt. */
private const val SCREEN_TEXT_CHARACTERS = 2000

// Bounds the upload while keeping on-screen text legible to a vision model.
private const val SCREENSHOT_LONGEST_SIDE = 1600
private const val SCREENSHOT_QUALITY = 80

/**
 * Each node's non-blank [label], one per line in accessibility traversal order (depth first, the
 * order a screen reader reads), cut at [SCREEN_TEXT_CHARACTERS]. [label] is null for nodes to skip.
 */
fun <T> visibleText(root: T, children: (T) -> List<T>, label: (T) -> CharSequence?): String {
    val lines = mutableListOf<String>()
    var length = 0
    fun visit(node: T) {
        if (length >= SCREEN_TEXT_CHARACTERS) return
        label(node)?.toString()?.trim()?.takeIf(String::isNotEmpty)?.let {
            lines += it
            length += it.length + 1
        }
        children(node).forEach(::visit)
    }
    visit(root)
    return lines.joinToString("\n").take(SCREEN_TEXT_CHARACTERS)
}

/** Reads [root]'s window: its title and the text of every visible node except password fields. */
fun screenCapture(root: AccessibilityNodeInfo) =
    ScreenCapture(
        root.window?.title?.toString().orEmpty(),
        visibleText(root, { node -> (0 until node.childCount).mapNotNull(node::getChild) }) { node
            ->
            if (!node.isVisibleToUser || node.isPassword) null
            else node.text ?: node.contentDescription
        },
    )

/** [width] by [height] shrunk to [SCREENSHOT_LONGEST_SIDE] on its longest side, never enlarged. */
fun screenshotSize(width: Int, height: Int): Pair<Int, Int> {
    val scale = minOf(1f, SCREENSHOT_LONGEST_SIDE.toFloat() / maxOf(width, height))
    return (width * scale).roundToInt() to (height * scale).roundToInt()
}

/**
 * [screen] as a base64 JPEG at [screenshotSize]. Copies first: a screenshot is a hardware bitmap.
 */
fun screenshotJpeg(screen: Bitmap): String {
    val (width, height) = screenshotSize(screen.width, screen.height)
    val scaled = screen.copy(Bitmap.Config.ARGB_8888, false).scale(width, height)
    val jpeg = ByteArrayOutputStream()
    scaled.compress(Bitmap.CompressFormat.JPEG, SCREENSHOT_QUALITY, jpeg)
    return Base64.getEncoder().encodeToString(jpeg.toByteArray())
}
