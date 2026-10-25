#pragma once

class QRect;
class QWindow;

namespace speecher::mac {

// Lets clicks on window outside region, in the window's coordinates, reach
// the windows below it, as QWindow::setMask does elsewhere, without clipping
// what the window draws: Cocoa's mask clips drawing, and Qt's window takes
// every click, transparent pixels included. While the window is visible it
// follows every pointer move and takes the mouse only while the pointer is in
// region. A later call replaces the region and applies it at once.
void takeClicksOnlyIn(QWindow *window, const QRect &region);

} // namespace speecher::mac
