package app.speecher.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.Dp

// Geometry of packaging/io.github.firemonster612.speecher.svg: a 76 x 56 pill holding four
// 7-wide bars that start 17 units in and repeat every 13.
private const val PILL_WIDTH = 76f
private const val PILL_HEIGHT = 56f
private const val BAR_WIDTH = 7f
private const val FIRST_BAR = 17f
private const val BAR_STEP = 13f

/** Bar heights of the resting mark, as fractions of the pill's height. */
internal val MarkBarHeights = listOf(24f, 40f, 32f, 16f).map { it / PILL_HEIGHT }

/** The four bars of the mark, laid out as they sit inside the pill. Fill the pill's bounds. */
@Composable
internal fun MarkBars(heights: List<Float>, color: Color, modifier: Modifier = Modifier) {
    Canvas(modifier.aspectRatio(PILL_WIDTH / PILL_HEIGHT)) {
        val unit = size.width / PILL_WIDTH
        heights.forEachIndexed { index, fraction ->
            val height = size.height * fraction
            drawRoundRect(
                color = color,
                topLeft = Offset((FIRST_BAR + BAR_STEP * index) * unit, (size.height - height) / 2),
                size = Size(BAR_WIDTH * unit, height),
                cornerRadius = CornerRadius(BAR_WIDTH * unit / 2),
            )
        }
    }
}

/** The app icon as a Compose element: the bone pill on an ink tile. Same in light and dark. */
@Composable
fun BrandTile(size: Dp, modifier: Modifier = Modifier) {
    val colors = MaterialTheme.colorScheme
    val ink = if (isSystemInDarkTheme()) colors.onPrimary else colors.primary
    val bone = if (isSystemInDarkTheme()) colors.primary else colors.onPrimary
    Box(
        modifier.size(size).background(ink, RoundedCornerShape(size * 28 / 128)),
        contentAlignment = Alignment.Center,
    ) {
        MarkBars(
            MarkBarHeights,
            ink,
            Modifier.fillMaxWidth(PILL_WIDTH / 128).background(bone, CircleShape),
        )
    }
}
