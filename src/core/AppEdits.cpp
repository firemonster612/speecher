#include "core/AppEdits.h"

#include "core/VocabularyEdits.h"

#include <algorithm>

namespace speecher {

namespace {

// A match as two matches compare: ids as Id matching compares them, text
// ignoring case.
QString comparable(const AppMatch &match)
{
    return match.kind == AppMatchKind::Id ? comparableAppId(match.value) : match.value.trimmed().toCaseFolded();
}

bool sameMatch(const AppMatch &left, const AppMatch &right)
{
    return left.kind == right.kind && comparable(left) == comparable(right);
}

bool sharesMatch(const AppEntry &left, const AppEntry &right)
{
    return std::any_of(left.matches.cbegin(), left.matches.cend(), [&right](const AppMatch &match) {
        return std::any_of(right.matches.cbegin(), right.matches.cend(),
                           [&match](const AppMatch &other) { return sameMatch(match, other); });
    });
}

qsizetype indexOfApp(const QList<AppEntry> &apps, const QString &id)
{
    const auto found = std::find_if(apps.cbegin(), apps.cend(), [&id](const AppEntry &app) { return app.id == id; });
    return found == apps.cend() ? -1 : found - apps.cbegin();
}

bool profileOffered(const std::optional<QString> &profile, const AppSettings &settings)
{
    return !profile || !offeredWritingProfile(*profile, settings.refinement.writingProfiles, {}).isEmpty();
}

// Tidies the App at `self` of the settings' Apps (-1 for one being added)
// and says what it can't hold.
EditRefusal checkApp(AppEntry &app, qsizetype self, const AppSettings &settings)
{
    for (AppMatch &match : app.matches) {
        match.value = match.value.trimmed();
    }
    app.matches.removeIf([](const AppMatch &match) { return match.value.isEmpty(); });
    if (app.matches.isEmpty()) {
        return EditRefusal::Blank;
    }
    app.name = app.name.trimmed();
    if (app.name.isEmpty()) {
        app.name = app.matches.first().value;
    }
    const QList<AppEntry> &apps = settings.apps.apps;
    for (qsizetype i = 0; i < apps.size(); ++i) {
        if (i != self && sharesMatch(app, apps.at(i))) {
            return EditRefusal::Taken;
        }
    }
    const bool typeOffered = !app.type || appTypes().contains(*app.type);
    return typeOffered && profileOffered(app.writingProfile, settings) ? EditRefusal::None : EditRefusal::Missing;
}

QStringList appIds(const QList<AppEntry> &apps)
{
    QStringList ids;
    for (const AppEntry &app : apps) {
        ids.append(app.id);
    }
    return ids;
}

} // namespace

EditRefusal addApp(AppSettings &settings, AppEntry app)
{
    QList<AppEntry> &apps = settings.apps.apps;
    if (app.id.isEmpty() || indexOfApp(apps, app.id) >= 0) {
        app.id = newAppId(appIds(apps));
    }
    return appendEntry(apps, app, [&settings](AppEntry &added, qsizetype self) {
        return checkApp(added, self, settings);
    });
}

EditRefusal editApp(AppSettings &settings, const QString &id, const EntryChange<AppEntry> &change)
{
    QList<AppEntry> &apps = settings.apps.apps;
    return changeEntry(apps, indexOfApp(apps, id), change, [&settings](AppEntry &app, qsizetype self) {
        return checkApp(app, self, settings);
    });
}

SettingsEdit removeApp(AppSettings &settings, const QString &id)
{
    QList<AppEntry> &apps = settings.apps.apps;
    const qsizetype index = indexOfApp(apps, id);
    if (index < 0) {
        return {};
    }
    keepVocabularyLimitsOf(settings, apps.at(index));
    const auto putBack = takeEntry(apps, index, [](const AppEntry &entry, const AppEntry &removed) {
        return entry.id == removed.id || sharesMatch(entry, removed);
    });
    return [putBack](AppSettings &draft) { putBack(draft.apps.apps); };
}

EditRefusal editAppType(AppSettings &settings, AppCategory type, const EntryChange<AppTypeSettings> &change)
{
    QList<AppTypeSettings> &types = settings.apps.types;
    const auto found = std::find_if(types.cbegin(), types.cend(),
                                    [type](const AppTypeSettings &settings) { return settings.type == type; });
    return changeEntry(types, found == types.cend() ? -1 : found - types.cbegin(), change,
                       [&settings, type](AppTypeSettings &edited, qsizetype) {
                           edited.type = type;
                           if (edited.pasting == PasteMethod::StandardPaste) {
                               edited.pasting.reset();
                           }
                           return profileOffered(edited.writingProfile, settings) ? EditRefusal::None
                                                                                   : EditRefusal::Missing;
                       });
}

} // namespace speecher
