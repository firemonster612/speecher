#pragma once

#include <QStringList>

class QStyle;

namespace speecher {

// Qt plugin roots of common distributions, where a system-installed widget
// style lives when the running Qt is the AppImage's own.
QStringList hostStylePluginDirs();

// Loads the system's plugin for the named widget style from the given plugin
// roots and creates the style, or returns nullptr. Loading fails, and is
// explained in `log`, when the plugin was built for a newer Qt than the one
// running. The caller owns the returned style.
QStyle *loadHostStyle(const QString &name, const QStringList &pluginDirs, QStringList *log);

} // namespace speecher
