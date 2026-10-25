#pragma once

#include <QByteArray>
#include <QGuiApplication>
#include <QQuickWindow>

namespace speecher::test {

// The open window whose QML type's name starts with typeName.
inline QQuickWindow *openWindow(const QByteArray &typeName)
{
    for (QWindow *window : QGuiApplication::topLevelWindows()) {
        if (window->isVisible() && QByteArray(window->metaObject()->className()).startsWith(typeName)) {
            return qobject_cast<QQuickWindow *>(window);
        }
    }
    return nullptr;
}

} // namespace speecher::test
