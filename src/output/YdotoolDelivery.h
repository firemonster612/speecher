#pragma once

#include "core/PasteRules.h"

#include <QObject>
#include <QString>

namespace speecher {

class YdotoolDelivery : public QObject {
    Q_OBJECT

public:
    explicit YdotoolDelivery(QObject *parent = nullptr);
    bool type(const QString &text, QString *error = nullptr);
    bool pasteFromClipboard(const QString &text, PasteMethod method, QString *error = nullptr);
    bool copySelection(PasteMethod method, QString *error = nullptr);
    static bool isAvailable();
    static QString socketPath();
    static QStringList commandArguments(const QString &text);
    static QStringList pasteShortcutArguments(PasteMethod method);
    static QStringList copyShortcutArguments(PasteMethod method);
    static QString withoutTrailingWhitespace(const QString &text);
};

} // namespace speecher
