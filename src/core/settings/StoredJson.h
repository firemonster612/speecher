#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

// What the settings migrations share for reading the JSON lists older
// releases stored under one key.

namespace speecher {

// The JSON array stored under key; empty when nothing or anything else is.
inline QJsonArray storedArray(const QSettings &settings, const QString &key)
{
    return QJsonDocument::fromJson(settings.value(key).toByteArray()).array();
}

} // namespace speecher
