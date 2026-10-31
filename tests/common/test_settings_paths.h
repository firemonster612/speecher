#pragma once

// Where a test's settings live, and putting them back there once a test
// closed them (SettingsFile::close moves later readers to a throwaway folder).

#include "core/SettingsStore.h"

#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSettings>

namespace speecher {

// Where this test process keeps its settings files (tests/test_main.cpp).
inline QString preferencesPath(SettingsStore &settings)
{
    QDir preferences = QFileInfo(settings.raw().fileName()).absoluteDir();
    preferences.cdUp();
    return preferences.path();
}

// Deleting the settings moves every QSettings made afterwards to a throwaway
// folder; the guard puts them back here for the tests that come after.
inline auto keepSettingsHere(SettingsStore &settings)
{
    return qScopeGuard([preferences = preferencesPath(settings)] {
        QSettings::setPath(QSettings::defaultFormat(), QSettings::UserScope, preferences);
        QSettings::setPath(QSettings::defaultFormat(), QSettings::SystemScope, preferences);
    });
}

} // namespace speecher
