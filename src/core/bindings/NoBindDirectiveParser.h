#pragma once

#include "core/AppSettings.h"

#include <QString>
#include <QStringList>

namespace speecher {

class NoBindDirectiveParser {
public:
    static bool hasDirective(const QString &transcript);
    static QStringList excludedPhrases(const QString &transcript, const QList<BindingRule> &rules);
};

} // namespace speecher
