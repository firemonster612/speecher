#pragma once

#include "core/PasteRules.h"

#include <QObject>
#include <QString>

#include <functional>

namespace speecher {

class YdotoolDelivery : public QObject {
    Q_OBJECT

public:
    explicit YdotoolDelivery(QObject *parent = nullptr);
    bool type(const QString &text, QString *error = nullptr);
    // clearToInject runs after the modifier-release subprocess, immediately
    // before the paste keystroke; false aborts without sending anything.
    bool pasteFromClipboard(const QString &text,
                            PasteMethod method,
                            const std::function<bool()> &clearToInject = {},
                            QString *error = nullptr);
    static bool isAvailable();
    static QString socketPath();
    static QStringList commandArguments(const QString &text);
    static QStringList pasteShortcutArguments(PasteMethod method);
    static QString withoutTrailingWhitespace(const QString &text);
};

} // namespace speecher
