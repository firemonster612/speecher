#include "app/PlatformComposition.h"

#include <QtGlobal>

#if defined(Q_OS_MACOS)
#include "app/MacComposition.h"
#elif defined(Q_OS_UNIX)
#include "app/LinuxComposition.h"
#else
#error "No PlatformComposition is implemented for this platform"
#endif

namespace speecher {

std::shared_ptr<const PlatformComposition> platformComposition()
{
#if defined(Q_OS_MACOS)
    return macComposition();
#else
    return linuxComposition();
#endif
}

} // namespace speecher
