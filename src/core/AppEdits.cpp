#include "core/AppEdits.h"

#include "core/VocabularyEdits.h"
#include <QUuid>

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

qsizetype indexOfType(const QList<AppTypeSettings> &types, const QString &id)
{
    const auto found = std::find_if(types.cbegin(), types.cend(), [&id](const AppTypeSettings &type) { return type.id == id; });
    return found == types.cend() ? -1 : found - types.cbegin();
}

EditRefusal checkTypeName(AppTypeSettings &type, const QList<AppTypeSettings> &types)
{
    type.name = type.name.trimmed();
    if (type.name.isEmpty()) {
        return EditRefusal::Blank;
    }
    for (const AppTypeSettings &other : types) {
        if (other.id != type.id && other.id.startsWith(kCustomIdPrefix)
            && other.name.compare(type.name, Qt::CaseInsensitive) == 0) {
            return EditRefusal::Taken;
        }
    }
    return EditRefusal::None;
}

bool profileOffered(const std::optional<QString> &profile, const AppSettings &settings)
{
    return !profile || !offeredWritingProfile(*profile, settings.refinement.writingProfiles, {}).isEmpty();
}

// Steps of its own keep one on, as the Pasting page's do.
bool stepsValid(const PastingChoice &pasting)
{
    return !pasting.steps || pasting.steps->any();
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
    if (!stepsValid(app.pasting)) {
        return EditRefusal::Incomplete;
    }
    const bool typeOffered = !app.type || indexOfType(settings.apps.types, *app.type) >= 0;
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
    const SettingsEdit limitsBack = keepVocabularyLimitsOf(settings, apps.at(index));
    const auto putBack = takeEntry(apps, index, [](const AppEntry &entry, const AppEntry &removed) {
        return entry.id == removed.id || sharesMatch(entry, removed);
    });
    return [putBack, limitsBack](AppSettings &draft) {
        putBack(draft.apps.apps);
        limitsBack(draft);
    };
}

AddAppTypeResult addAppType(AppSettings &settings, const QString &name)
{
    QList<AppTypeSettings> &types = settings.apps.types;
    AppTypeSettings added{{}, name};
    const EditRefusal refusal = checkTypeName(added, types);
    if (refusal != EditRefusal::None) {
        return {{}, refusal};
    }
    do {
        added.id = kCustomIdPrefix + QUuid::createUuid().toString(QUuid::Id128).left(12);
    } while (indexOfType(types, added.id) >= 0);
    types.insert(indexOfType(types, QStringLiteral("general")), added);
    return {added.id, EditRefusal::None};
}

EditRefusal editAppType(AppSettings &settings, const QString &id, const EntryChange<AppTypeSettings> &change)
{
    QList<AppTypeSettings> &types = settings.apps.types;
    return changeEntry(types, indexOfType(types, id), change,
                       [&settings, &types, &id](AppTypeSettings &edited, qsizetype self) {
                           edited.id = id;
                           if (id.startsWith(kCustomIdPrefix)) {
                               const EditRefusal refusal = checkTypeName(edited, types);
                               if (refusal != EditRefusal::None) {
                                   return refusal;
                               }
                           } else if (edited.name != types.at(self).name) {
                               return EditRefusal::Missing;
                           }
                           if (!stepsValid(edited.pasting)) {
                               return EditRefusal::Incomplete;
                           }
                           return profileOffered(edited.writingProfile, settings) ? EditRefusal::None
                                                                                   : EditRefusal::Missing;
                       });
}

SettingsEdit removeAppType(AppSettings &settings, const QString &id)
{
    QList<AppTypeSettings> &types = settings.apps.types;
    const qsizetype index = indexOfType(types, id);
    if (!id.startsWith(kCustomIdPrefix) || index < 0) {
        return {};
    }
    const AppTypeSettings removed = types.takeAt(index);
    QStringList members;
    for (AppEntry &app : settings.apps.apps) {
        if (app.type == id) {
            members.append(app.id);
            app.type.reset();
        }
    }
    return [index, removed, members](AppSettings &draft) {
        QList<AppTypeSettings> &back = draft.apps.types;
        AppTypeSettings checked = removed;
        if (indexOfType(back, removed.id) >= 0 || checkTypeName(checked, back) != EditRefusal::None) {
            return;
        }
        back.insert(std::min(index, back.size() - 1), removed);
        for (AppEntry &app : draft.apps.apps) {
            if (!app.type && members.contains(app.id)) {
                app.type = removed.id;
            }
        }
    };
}

} // namespace speecher
