#pragma once

class QRect;
class QWindow;

namespace speecher {

// Lets clicks on the popup's window outside inputRegion, in the window's
// coordinates, reach the app underneath, without cutting off what it draws
// out to shadowRegion: the input region and as far as the capsule's shadow
// reaches. Where the platform's mask also clips drawing (Windows), clicks on
// the shadow stay with the popup. A later call replaces the regions.
void setPopupInputRegion(QWindow &window, const QRect &inputRegion, const QRect &shadowRegion);

} // namespace speecher
