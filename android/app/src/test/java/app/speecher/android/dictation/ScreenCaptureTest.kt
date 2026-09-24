package app.speecher.android.dictation

import org.junit.Assert.assertEquals
import org.junit.Test

class ScreenCaptureTest {
    private class Node(val label: String?, vararg val children: Node)

    private fun text(
        root: Node,
        children: (Node) -> Sequence<Node> = { it.children.asSequence() },
    ) = screenCapture(root, { "Chat" }, children, Node::label)?.text

    @Test
    fun `visible text reads depth first, skipping blank labels`() {
        val screen =
            Node(null, Node("Sam", Node("  "), Node("Online")), Node("Are we still on?"), Node(""))
        assertEquals("Sam\nOnline\nAre we still on?", text(screen))
    }

    @Test
    fun `visible text stops at 2000 characters`() {
        val screen = Node(null, *Array(30) { Node("x".repeat(299)) })
        assertEquals(2000, text(screen)?.length)
    }

    @Test
    fun `the walk fetches at most 500 nodes and drops the capture if a node throws`() {
        var fetched = 0
        val screen = Node(null, *Array(2000) { Node("") })
        text(screen) { node -> node.children.asSequence().onEach { fetched++ } }
        assertEquals(499, fetched)
        assertEquals(null, text(screen) { error("stale node") })
    }

    @Test
    fun `a screenshot keeps the target's rows below the status bar and above the keyboard`() {
        assertEquals(
            listOf(80..1399, null),
            listOf(
                uncoveredRows(0..2399, listOf(0..79, 1400..2399)),
                uncoveredRows(0..2399, listOf(0..1300, 1200..2399)),
            ),
        )
    }

    @Test
    fun `screenshots shrink to 1600 px on the longest side and never grow`() {
        assertEquals(
            listOf(720 to 1600, 1600 to 720, 800 to 600),
            listOf(
                screenshotSize(1080, 2400),
                screenshotSize(2400, 1080),
                screenshotSize(800, 600),
            ),
        )
    }
}
