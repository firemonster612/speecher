#include "core/settings/AppsSettingsCodec.h"

#include "core/settings/SettingsKeys.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace speecher::AppsSettingsCodec {

namespace {

const QString kId = QStringLiteral("id");
const QString kName = QStringLiteral("name");
const QString kIcon = QStringLiteral("icon");
const QString kMatches = QStringLiteral("matches");
const QString kKind = QStringLiteral("kind");
const QString kValue = QStringLiteral("value");
const QString kType = QStringLiteral("type");
const QString kWritingProfile = QStringLiteral("writingProfile");
const QString kSteps = QStringLiteral("steps");
const QString kPasteKeys = QStringLiteral("pasteKeys");
const QString kHistory = QStringLiteral("history");

QString matchKindName(AppMatchKind kind)
{
    switch (kind) {
    case AppMatchKind::Name:
        return QStringLiteral("name");
    case AppMatchKind::WindowTitle:
        return QStringLiteral("title");
    case AppMatchKind::Id:
        break;
    }
    return QStringLiteral("id");
}

std::optional<AppMatchKind> matchKindFromName(const QString &name)
{
    if (name == u"id") return AppMatchKind::Id;
    if (name == u"name") return AppMatchKind::Name;
    if (name == u"title") return AppMatchKind::WindowTitle;
    return std::nullopt;
}

QJsonArray arrayAt(const QSettings &settings, const QString &key, bool *stored = nullptr)
{
    const QJsonDocument document = QJsonDocument::fromJson(settings.value(key).toByteArray());
    if (stored) {
        *stored = document.isArray();
    }
    return document.array();
}

std::optional<AppCategory> typeAt(const QJsonObject &object)
{
    const AppCategory type = appCategoryFromName(object.value(kType).toString());
    return type == AppCategory::Unknown ? std::nullopt : std::optional(type);
}

std::optional<QString> profileAt(const QJsonObject &object)
{
    const QString profile = object.value(kWritingProfile).toString();
    return profile.isEmpty() ? std::nullopt : std::optional(writingProfileFromName(profile));
}

// Steps with none on were never written by Speecher, and read as none.
PastingChoice pastingAt(const QJsonObject &object)
{
    PastingChoice pasting;
    const QJsonValue steps = object.value(kSteps);
    if (steps.isObject()) {
        const DeliverySteps stored{steps[deliveryStepName(DeliveryStep::Insert)].toBool(),
                                   steps[deliveryStepName(DeliveryStep::Press)].toBool(),
                                   steps[deliveryStepName(DeliveryStep::Keep)].toBool()};
        if (stored.any()) {
            pasting.steps = stored;
        }
    }
    const QString keys = object.value(kPasteKeys).toString();
    if (!keys.isEmpty()) {
        pasting.keys = pasteKeyComboFromName(keys);
    }
    return pasting;
}

// Only Stats and Nothing: an App can lower What Speecher Keeps, never raise it.
std::optional<HistoryKeep> historyAt(const QJsonObject &object)
{
    const std::optional<HistoryKeep> keep = historyKeepFromName(object.value(kHistory).toString());
    return keep && *keep <= HistoryKeep::Stats ? keep : std::nullopt;
}

void putSettings(QJsonObject &object, const std::optional<QString> &profile, const PastingChoice &pasting,
                 const std::optional<HistoryKeep> &history)
{
    if (history) {
        object.insert(kHistory, historyKeepName(*history));
    }
    if (profile) {
        object.insert(kWritingProfile, *profile);
    }
    if (pasting.steps) {
        QJsonObject steps;
        for (const DeliveryStep step : {DeliveryStep::Insert, DeliveryStep::Press, DeliveryStep::Keep}) {
            steps.insert(deliveryStepName(step), pasting.steps->has(step));
        }
        object.insert(kSteps, steps);
    }
    if (pasting.keys) {
        object.insert(kPasteKeys, pasteKeyComboName(*pasting.keys));
    }
}

} // namespace

AppsSettings load(const QSettings &settings)
{
    AppsSettings apps;
    apps.apps.clear();
    for (const QJsonValue &value : arrayAt(settings, SettingsKeys::AppsEntries)) {
        const QJsonObject object = value.toObject();
        AppEntry entry{object.value(kId).toString(), object.value(kName).toString(), object.value(kIcon).toString(),
                       {}, typeAt(object), profileAt(object), pastingAt(object), historyAt(object)};
        for (const QJsonValue &match : object.value(kMatches).toArray()) {
            const std::optional<AppMatchKind> kind = matchKindFromName(match[kKind].toString());
            const QString text = match[kValue].toString().trimmed();
            if (kind && !text.isEmpty()) {
                entry.matches.append({*kind, text});
            }
        }
        // An entry with nothing to recognize the app by, or no id to edit it
        // by, was never written by Speecher.
        if (!entry.id.isEmpty() && !entry.matches.isEmpty()) {
            apps.apps.append(entry);
        }
    }

    bool typesStored = false;
    const QJsonArray types = arrayAt(settings, SettingsKeys::AppsTypes, &typesStored);
    if (!typesStored) {
        return apps;
    }
    for (AppTypeSettings &type : apps.types) {
        for (const QJsonValue &value : types) {
            const QJsonObject object = value.toObject();
            if (typeAt(object) == type.type) {
                type.writingProfile = profileAt(object);
                type.pasting = pastingAt(object);
                type.history = historyAt(object);
                break;
            }
        }
    }
    return apps;
}

void store(QSettings &settings, const AppsSettings &apps)
{
    QJsonArray entries;
    for (const AppEntry &entry : apps.apps) {
        QJsonArray matches;
        for (const AppMatch &match : entry.matches) {
            matches.append(QJsonObject{{kKind, matchKindName(match.kind)}, {kValue, match.value.trimmed()}});
        }
        QJsonObject object{{kId, entry.id}, {kName, entry.name}, {kMatches, matches}};
        if (!entry.icon.isEmpty()) {
            object.insert(kIcon, entry.icon);
        }
        if (entry.type) {
            object.insert(kType, appCategoryName(*entry.type));
        }
        putSettings(object, entry.writingProfile, entry.pasting, entry.history);
        entries.append(object);
    }
    QJsonArray types;
    for (const AppTypeSettings &type : apps.types) {
        QJsonObject object{{kType, appCategoryName(type.type)}};
        putSettings(object, type.writingProfile, type.pasting, type.history);
        types.append(object);
    }
    settings.setValue(SettingsKeys::AppsEntries, QJsonDocument(entries).toJson(QJsonDocument::Compact));
    settings.setValue(SettingsKeys::AppsTypes, QJsonDocument(types).toJson(QJsonDocument::Compact));
}

} // namespace speecher::AppsSettingsCodec
