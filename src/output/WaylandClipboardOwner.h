#pragma once

#include "output/WlClipboardDelivery.h"

namespace speecher {

class WaylandClipboardOwner {
public:
    bool start(const QList<ClipboardMimePart> &parts, QString *error = nullptr);
};

} // namespace speecher
