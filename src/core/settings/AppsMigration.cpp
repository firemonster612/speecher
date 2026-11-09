#include "core/settings/AppsMigration.h"

#include "core/Apps.h"
#include "core/DeliverySteps.h"
#include "core/settings/AppsSettingsCodec.h"
#include "core/settings/SettingsKeys.h"
#include "core/settings/StoredJson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QMap>

#include <algorithm>

namespace speecher {

namespace {

// 1 moved older releases' per-app settings into Apps; 2 turned Pasting
// choices into steps and keys.
constexpr int appsMigrationVersion = 2;

// How older releases' paste rules, and the first Apps settings, narrowed the
// Delivery Steps.
enum class PasteMethod {
    // The steps as they are.
    StandardPaste,
    // The steps, pressing a terminal's paste keys.
    TerminalPaste,
    // The steps without the paste keys.
    DirectInsert,
    ClipboardOnly,
};

// A stored method's name; anything else reads as Standard paste.
PasteMethod pasteMethodFromName(const QString &name)
{
    if (name == u"terminal_paste") return PasteMethod::TerminalPaste;
    if (name == u"direct_insert") return PasteMethod::DirectInsert;
    if (name == u"clipboard_only") return PasteMethod::ClipboardOnly;
    return PasteMethod::StandardPaste;
}

// A paste rule as output/pasteRules stored it.
struct LegacyPasteRule {
    enum class Scope { Global, Category, Application };

    Scope scope = Scope::Global;
    // An app type's name, or an application id.
    QString match;
    PasteMethod method = PasteMethod::StandardPaste;
    bool enabled = true;
};

LegacyPasteRule::Scope scopeFromName(const QString &name)
{
    if (name == u"application") return LegacyPasteRule::Scope::Application;
    if (name == u"category") return LegacyPasteRule::Scope::Category;
    return LegacyPasteRule::Scope::Global;
}

// What an App migrated from a bare id or match text is called: the text, or
// an id's last dotted part, capitalized.
QString nameFrom(const QString &text)
{
    QString name = text.section(QLatin1Char('.'), -1).trimmed();
    if (name.isEmpty()) {
        name = text.trimmed();
    }
    if (!name.isEmpty()) {
        name[0] = name[0].toUpper();
    }
    return name;
}

QStringList idsOf(const QList<AppEntry> &apps)
{
    QStringList ids;
    for (const AppEntry &entry : apps) {
        ids.append(entry.id);
    }
    return ids;
}

// The App matched by exactly this id, added where there is none.
AppEntry &appForId(QList<AppEntry> &apps, const QString &id)
{
    const QString comparable = comparableAppId(id);
    for (AppEntry &entry : apps) {
        for (const AppMatch &match : entry.matches) {
            if (match.kind == AppMatchKind::Id && comparableAppId(match.value) == comparable) {
                return entry;
            }
        }
    }
    apps.append({.id = newAppId(idsOf(apps)), .name = nameFrom(id), .matches = {{AppMatchKind::Id, id.trimmed()}}});
    return apps.last();
}

// The Pasting page's steps as stored, after migrateDeliverySteps.
DeliverySteps storedSteps(const QSettings &settings)
{
    const DeliverySteps unset;
    return {settings.value(SettingsKeys::InsertThroughAccessibility, unset.insert).toBool(),
            settings.value(SettingsKeys::PressPasteKeys, unset.press).toBool(),
            settings.value(SettingsKeys::KeepOnClipboard, unset.keep).toBool()};
}

// The steps a delivery took under output/method and
// output/restoreClipboardAfterTyping, as older releases stored them, before
// any paste rule narrowed them.
DeliverySteps unnarrowedSteps(const QSettings &settings)
{
    // The stored names, older spellings included.
    const QString method = settings.value(SettingsKeys::OutputMethod).toString();
    const bool keyboardOnly = method == u"ydotool" || method == u"mac-paste" || method == u"win-paste";
    const bool insertOnly = method == u"direct_insert";
    const bool copyOnly = method == u"wl-copy" || method == u"clipboard" || method == u"qt-clipboard" || method == u"qt";
    DeliverySteps steps{!keyboardOnly && !copyOnly, !insertOnly && !copyOnly, true};
    // Restoring meant nothing where nothing was pasted, so the words stayed.
    steps.keep = !settings.value(SettingsKeys::RestoreClipboardAfterTyping).toBool() || (!steps.insert && !steps.press);
    return steps;
}

// The method of the global rule as older releases' delivery read it: the
// first one turned on, and Clipboard only where none was.
PasteMethod globalPasteMethod(const QList<LegacyPasteRule> &rules)
{
    for (const LegacyPasteRule &rule : rules) {
        if (rule.scope == LegacyPasteRule::Scope::Global && rule.enabled) {
            return rule.method;
        }
    }
    return PasteMethod::ClipboardOnly;
}

// What a delivery under an older paste method did: the steps it narrowed
// base to, and the keys it pressed, for the methods that named keys. A
// narrowing that left no step on, which delivered nothing, kept the words
// on the clipboard.
PastingChoice narrowedBy(PasteMethod method, const DeliverySteps &base)
{
    DeliverySteps steps = base;
    switch (method) {
    case PasteMethod::StandardPaste:
        return {steps, PasteKeyCombo::Standard};
    case PasteMethod::TerminalPaste:
        return {steps, terminalKeysDiffer ? PasteKeyCombo::Terminal : PasteKeyCombo::Standard};
    case PasteMethod::ClipboardOnly:
        steps.insert = false;
        steps.press = false;
        break;
    case PasteMethod::DirectInsert:
        // The old delivery inserted for this rule whatever the output method,
        // a keyboard-only one included, and never pressed keys after it. It
        // copied first, too, so an insertion that failed left the words on
        // the clipboard: with no paste to fall back on, Keep stays on.
        steps.insert = true;
        steps.press = false;
        steps.keep = true;
        break;
    }
    if (!steps.any()) {
        steps.keep = true;
    }
    return {steps, std::nullopt};
}

// The parts of choice a layer stores. A part the layer would inherit as it
// is, the Pasting page's steps or the usual keys, stays unset so it follows
// the page, unless a layer it would follow first sets that part (shadowing).
PastingChoice ownParts(const PastingChoice &choice, const DeliverySteps &page, const PastingChoice &shadowing)
{
    PastingChoice own;
    if (choice.steps != page || shadowing.steps) {
        own.steps = choice.steps;
    }
    if (choice.keys && (choice.keys != PasteKeyCombo::Standard || shadowing.keys)) {
        own.keys = choice.keys;
    }
    return own;
}

// Gives the Apps and App Types the older Pasting choices, by App id and by
// App Type, each as the steps and keys a delivery under it took: base
// narrowed by the choice. page is the Pasting page's steps once migrated.
// Steps and keys now settle each on their own, so a layer stores a part
// wherever a layer it follows first has one of its own.
void applyPastingChoices(AppsSettings &apps, const QHash<QString, PasteMethod> &forApps,
                         const QMap<AppCategory, PasteMethod> &forTypes, const DeliverySteps &base,
                         const DeliverySteps &page)
{
    // The parts some App Type other than except sets.
    const auto setByTypesOtherThan = [&apps](AppCategory except) {
        PastingChoice parts;
        for (const AppTypeSettings &type : apps.types) {
            if (type.type != except) {
                parts.steps = parts.steps ? parts.steps : type.pasting.steps;
                parts.keys = parts.keys ? parts.keys : type.pasting.keys;
            }
        }
        return parts;
    };
    // Terminals come before the type of anything running in a terminal, so
    // they go last among the types.
    for (const bool terminals : {false, true}) {
        for (AppTypeSettings &type : apps.types) {
            if ((type.type == AppCategory::Terminal) != terminals) {
                continue;
            }
            type.pasting = forTypes.contains(type.type)
                ? ownParts(narrowedBy(forTypes.value(type.type), base), page,
                           terminals ? setByTypesOtherThan(AppCategory::Terminal) : PastingChoice{})
                : PastingChoice{};
        }
    }
    const PastingChoice setByTypes = setByTypesOtherThan(AppCategory::Unknown);
    for (AppEntry &entry : apps.apps) {
        if (forApps.contains(entry.id)) {
            entry.pasting = ownParts(narrowedBy(forApps.value(entry.id), base), page, setByTypes);
        }
    }
}

// Version 2 over settings version 1 wrote: each App's and App Type's
// "pasting" becomes steps and keys.
void migratePastingChoices(QSettings &settings)
{
    if (!settings.contains(SettingsKeys::AppsEntries) && !settings.contains(SettingsKeys::AppsTypes)) {
        return;
    }
    const QString pasting = QStringLiteral("pasting");
    QHash<QString, PasteMethod> forApps;
    for (const QJsonValue &value : storedArray(settings, SettingsKeys::AppsEntries)) {
        const QString method = value[pasting].toString();
        if (!method.isEmpty()) {
            forApps.insert(value[QStringLiteral("id")].toString(), pasteMethodFromName(method));
        }
    }
    QMap<AppCategory, PasteMethod> forTypes;
    if (settings.contains(SettingsKeys::AppsTypes)) {
        for (const QJsonValue &value : storedArray(settings, SettingsKeys::AppsTypes)) {
            const QString method = value[pasting].toString();
            // "The steps" was no choice of its own for an App Type.
            if (!method.isEmpty() && pasteMethodFromName(method) != PasteMethod::StandardPaste) {
                forTypes.insert(appCategoryFromName(value[QStringLiteral("type")].toString()), pasteMethodFromName(method));
            }
        }
    } else if (terminalKeysDiffer) {
        // Unstored types were the defaults, Terminals' keys among them.
        forTypes.insert(AppCategory::Terminal, PasteMethod::TerminalPaste);
    }
    AppsSettings apps = AppsSettingsCodec::load(settings);
    // Version 1's choices narrowed the Pasting page's steps as they are.
    const DeliverySteps page = storedSteps(settings);
    applyPastingChoices(apps, forApps, forTypes, page, page);
    AppsSettingsCodec::store(settings, apps);
}

// output/pasteRules as stored. Nothing stored, no list or an empty one
// reads as the rules that were the default, as older releases read them:
// Terminals' Terminal paste and a global Standard paste. A list without a
// global rule turned on copied only, everywhere no other rule applied.
QList<LegacyPasteRule> legacyPasteRules(const QSettings &settings)
{
    const QJsonDocument document = QJsonDocument::fromJson(settings.value(SettingsKeys::PasteRules).toByteArray());
    const QList<LegacyPasteRule> defaults{
        {LegacyPasteRule::Scope::Category, appCategoryName(AppCategory::Terminal), PasteMethod::TerminalPaste, true},
        {LegacyPasteRule::Scope::Global, QString(), PasteMethod::StandardPaste, true},
    };
    if (!document.isArray()) {
        return defaults;
    }
    QList<LegacyPasteRule> rules;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        rules.append({scopeFromName(object.value(QStringLiteral("scope")).toString()),
                      object.value(QStringLiteral("match")).toString().trimmed(),
                      pasteMethodFromName(object.value(QStringLiteral("method")).toString()),
                      object.value(QStringLiteral("enabled")).toBool(true)});
    }
    return rules.isEmpty() ? defaults : rules;
}

// The steps a delivery took under the global paste rule, output/method and
// output/restoreClipboardAfterTyping as older releases stored them: the
// Pasting page's steps once migrated.
DeliverySteps legacyDeliverySteps(const QSettings &settings)
{
    return *narrowedBy(globalPasteMethod(legacyPasteRules(settings)), unnarrowedSteps(settings)).steps;
}

} // namespace

void migrateApps(QSettings &settings)
{
    const int version = settings.value(SettingsKeys::AppsMigrationVersion).toInt();
    if (version >= appsMigrationVersion) {
        return;
    }
    settings.setValue(SettingsKeys::AppsMigrationVersion, appsMigrationVersion);
    if (version == 1) {
        migratePastingChoices(settings);
        return;
    }
    const bool storedPasteRules = settings.contains(SettingsKeys::PasteRules);
    if (!storedPasteRules && !settings.contains(SettingsKeys::WritingProfileOverrides)
        && !settings.contains(SettingsKeys::AppRecognitionRules)
        && !settings.contains(SettingsKeys::DefaultWritingProfile)) {
        return;
    }

    AppsSettings apps;
    QHash<QString, PasteMethod> appPasting;
    QMap<AppCategory, PasteMethod> typePasting;
    // An earlier override or rule for the same app won, so a later one sets
    // only what is still unset.
    for (const QJsonValue &value : storedArray(settings, SettingsKeys::WritingProfileOverrides)) {
        const QJsonObject override = value.toObject();
        const QString id = override.value(QStringLiteral("applicationId")).toString().trimmed();
        if (id.isEmpty() || !override.value(QStringLiteral("enabled")).toBool(true)) {
            continue;
        }
        AppEntry &entry = appForId(apps.apps, id);
        if (!entry.writingProfile) {
            entry.writingProfile = writingProfileFromName(override.value(QStringLiteral("profile")).toString());
        }
    }

    const QList<LegacyPasteRule> pasteRules = legacyPasteRules(settings);
    for (const LegacyPasteRule &rule : pasteRules) {
        if (rule.scope != LegacyPasteRule::Scope::Application || !rule.enabled || rule.match.isEmpty()) {
            continue;
        }
        const QString id = appForId(apps.apps, rule.match).id;
        if (!appPasting.contains(id)) {
            appPasting.insert(id, rule.method);
        }
    }

    for (const QJsonValue &value : storedArray(settings, SettingsKeys::AppRecognitionRules)) {
        const QJsonObject rule = value.toObject();
        const QString match = rule.value(QStringLiteral("match")).toString().trimmed();
        const AppCategory category = appCategoryFromName(rule.value(QStringLiteral("category")).toString());
        const QString profile = rule.value(QStringLiteral("writingProfile")).toString();
        if (match.isEmpty() || (category == AppCategory::Unknown && profile.isEmpty())) {
            continue;
        }
        AppEntry entry{.id = newAppId(idsOf(apps.apps)), .name = nameFrom(match), .matches = {{AppMatchKind::Name, match}}};
        if (category != AppCategory::Unknown) {
            entry.type = category;
        }
        if (!profile.isEmpty()) {
            entry.writingProfile = writingProfileFromName(profile);
            entry.matches.append({AppMatchKind::WindowTitle, match});
        } else {
            entry.catalogPicksProfile = true;
        }
        apps.apps.append(entry);
    }

    // Each App Type took its own rule, the first turned on, or else the
    // global one. Terminals without a rule of their own left anything in a
    // terminal to its type's rule, as Terminals left unset still do.
    const PasteMethod global = globalPasteMethod(pasteRules);
    for (const AppCategory type : appTypes()) {
        const auto own = std::find_if(pasteRules.cbegin(), pasteRules.cend(), [type](const LegacyPasteRule &rule) {
            return rule.scope == LegacyPasteRule::Scope::Category && rule.enabled && appCategoryFromName(rule.match) == type;
        });
        if (own != pasteRules.cend()) {
            typePasting.insert(type, own->method);
        } else if (type != AppCategory::Terminal) {
            typePasting.insert(type, global);
        }
    }
    // Against the output settings as older releases stored them, which
    // migrateDeliverySteps removes after this.
    const DeliverySteps base = unnarrowedSteps(settings);
    const PastingChoice globalChoice = narrowedBy(global, base);
    applyPastingChoices(apps, appPasting, typePasting, base, *globalChoice.steps);
    // Apps typed Terminals still took the global rule's keys.
    for (AppTypeSettings &type : apps.types) {
        if (type.type == AppCategory::Terminal && !typePasting.contains(type.type)
            && globalChoice.keys == PasteKeyCombo::Terminal) {
            type.pasting.keys = globalChoice.keys;
        }
    }

    if (settings.contains(SettingsKeys::DefaultWritingProfile)) {
        for (AppTypeSettings &type : apps.types) {
            if (type.type == AppCategory::General) {
                type.writingProfile = writingProfileFromName(settings.value(SettingsKeys::DefaultWritingProfile).toString());
            }
        }
    }
    AppsSettingsCodec::store(settings, apps);
}

void migrateDeliverySteps(QSettings &settings)
{
    constexpr int currentMigrationVersion = 1;
    if (settings.value(SettingsKeys::DeliveryStepsMigrationVersion).toInt() >= currentMigrationVersion) {
        return;
    }
    settings.setValue(SettingsKeys::DeliveryStepsMigrationVersion, currentMigrationVersion);
    if (!settings.contains(SettingsKeys::PasteRules) && !settings.contains(SettingsKeys::OutputMethod)
        && !settings.contains(SettingsKeys::RestoreClipboardAfterTyping)) {
        return;
    }
    const DeliverySteps steps = legacyDeliverySteps(settings);
    settings.setValue(SettingsKeys::InsertThroughAccessibility, steps.insert);
    settings.setValue(SettingsKeys::PressPasteKeys, steps.press);
    settings.setValue(SettingsKeys::KeepOnClipboard, steps.keep);
    settings.remove(SettingsKeys::OutputMethod);
    settings.remove(SettingsKeys::RestoreClipboardAfterTyping);
}

} // namespace speecher
