package app.speecher.android.dictation

import android.graphics.Bitmap
import android.graphics.Rect
import android.view.accessibility.AccessibilityNodeInfo
import androidx.core.graphics.scale
import java.io.ByteArrayOutputStream
import java.util.Base64
import kotlin.math.roundToInt

/** The target window's title and visible text, read by the chip service at tap. */
data class ScreenCapture(val title: String, val text: String)

/** Enough on-screen text to disambiguate a dictation without flooding the prompt. */
private const val SCREEN_TEXT_CHARACTERS = 2000

/**
 * Enough nodes for a full screen of ordinary views. Each one is an IPC to the target app, so a web
 * view or a self-referencing tree stops here instead of janking the tap or never ending.
 */
private const val SCREEN_NODES = 500

// Bounds the upload while keeping on-screen text legible to a vision model.
private const val SCREENSHOT_LONGEST_SIDE = 1600
private const val SCREENSHOT_QUALITY = 80

/**
 * [root]'s [title] and each node's non-blank [label], one per line in accessibility traversal order
 * (depth first, the order a screen reader reads), cut at [SCREEN_TEXT_CHARACTERS]. [label] is null
 * for nodes to skip. At most [SCREEN_NODES] nodes are fetched. Null if reading any node throws, as
 * a node that went stale mid-walk does: the screen changed, so what was read is dropped.
 */
fun <T> screenCapture(
    root: T,
    title: (T) -> CharSequence?,
    children: (T) -> Sequence<T>,
    label: (T) -> CharSequence?,
): ScreenCapture? = runCatching {
    val lines = mutableListOf<String>()
    var length = 0
    var unfetched = SCREEN_NODES - 1
    val pending = ArrayDeque(listOf(root))
    while (length < SCREEN_TEXT_CHARACTERS) {
        val node = pending.removeLastOrNull() ?: break
        label(node)?.toString()?.trim()?.takeIf(String::isNotEmpty)?.let {
            lines += it
            length += it.length + 1
        }
        val next = children(node).take(unfetched).toList()
        unfetched -= next.size
        pending.addAll(next.asReversed())
    }
    ScreenCapture(
        title(root)?.toString().orEmpty(),
        lines.joinToString("\n").take(SCREEN_TEXT_CHARACTERS),
    )
}
    .getOrNull()

/** Reads [root]'s window: its title and the text of every visible node except password fields. */
fun screenCapture(root: AccessibilityNodeInfo) =
    screenCapture(
        root,
        { it.window?.title },
        { node -> (0 until node.childCount).asSequence().mapNotNull(node::getChild) },
    ) { node ->
        if (!node.isVisibleToUser || node.isPassword) null else node.text ?: node.contentDescription
    }

/**
 * The rows of [window] that none of [covers], the rows of the windows above it, hide. Each cover
 * trims from the edge nearer its centre, so the status bar and a notification come off the top and
 * the keyboard off the bottom; null when nothing is left.
 */
fun uncoveredRows(window: IntRange, covers: List<IntRange>): IntRange? {
    val middle = (window.first + window.last) / 2
    var top = window.first
    var bottom = window.last
    for (cover in covers) {
        if ((cover.first + cover.last) / 2 < middle) top = maxOf(top, cover.last + 1)
        else bottom = minOf(bottom, cover.first - 1)
    }
    return (top..bottom).takeUnless(IntRange::isEmpty)
}

/** [width] by [height] shrunk to [SCREENSHOT_LONGEST_SIDE] on its longest side, never enlarged. */
fun screenshotSize(width: Int, height: Int): Pair<Int, Int> {
    val scale = minOf(1f, SCREENSHOT_LONGEST_SIDE.toFloat() / maxOf(width, height))
    return (width * scale).roundToInt() to (height * scale).roundToInt()
}

/**
 * [crop] of [screen] as a base64 JPEG at [screenshotSize]. A screenshot is a hardware bitmap, so it
 * is copied first. Each full-size copy is about 10 MB, so each is recycled once the next exists.
 */
fun screenshotJpeg(screen: Bitmap, crop: Rect): String {
    val copy = screen.copy(Bitmap.Config.ARGB_8888, false)
    val cropped = Bitmap.createBitmap(copy, crop.left, crop.top, crop.width(), crop.height())
    if (cropped !== copy) copy.recycle()
    val (width, height) = screenshotSize(cropped.width, cropped.height)
    val scaled = cropped.scale(width, height)
    if (scaled !== cropped) cropped.recycle()
    val jpeg = ByteArrayOutputStream()
    scaled.compress(Bitmap.CompressFormat.JPEG, SCREENSHOT_QUALITY, jpeg)
    scaled.recycle()
    return Base64.getEncoder().encodeToString(jpeg.toByteArray())
}
