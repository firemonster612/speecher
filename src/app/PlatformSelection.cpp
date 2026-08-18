#include "app/PlatformComposition.h"

#include <QtGlobal>

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
#include "app/LinuxComposition.h"
#else
// A macOS composition lands here: include app/MacComposition.h and return it
// below. Nothing else in the tree names a concrete composition.
#error "No PlatformComposition is implemented for this platform"
#endif

namespace speecher {

std::shared_ptr<const PlatformComposition> platformComposition()
{
    return linuxComposition();
}

} // namespace speecher
