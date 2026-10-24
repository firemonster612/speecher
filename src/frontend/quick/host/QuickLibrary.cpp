#include "frontend/quick/host/QuickLibrary.h"

#include "app/ApplicationController.h"
#include "core/BindingProcessor.h"
#include "core/ProviderChain.h"
#include "core/SettingsStore.h"
#include "core/Vocabulary.h"
#include "core/VocabularyLimit.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QUrl>

#include <algorithm>
#include <optional>

namespace speecher {

namespace {

ApplicationController *g_controller = nullptr;

const QStringList cleanupBases{QStringLiteral("light_cleanup"), QStringLiteral("balanced"),
                               QStringLiteral("strong_polish"), kCustomOnlyCleanupBase};

QString field(const QVariant &value)
{
    return value.toString();
}

// Whether no entry of list but the one at index `self` is called `name`,
// ignoring case: two entries by one name would read as one.
template <typename T, typename NameOf>
bool nameFree(const QList<T> &list, qsizetype self, const QString &name, NameOf nameOf)
{
    for (qsizetype i = 0; i < list.size(); ++i) {
        if (i != self && nameOf(list.at(i)).compare(name, Qt::CaseInsensitive) == 0) {
            return false;
        }
    }
    return true;
}

// A new entry's id: one of the user's, and not one list holds.
template <typename T, typename IdOf>
bool idFree(const QList<T> &list, const QString &id, IdOf idOf)
{
    return id.startsWith(kCustomIdPrefix)
        && std::none_of(list.cbegin(), list.cend(), [&](const T &entry) { return idOf(entry) == id; });
}

bool levelOffered(const QString &id, const RefinementSettings &refinement)
{
    const QList<CustomCleanupLevel> &custom = refinement.customCleanupLevels;
    return kBuiltInCleanupLevels.contains(id)
        || std::any_of(custom.cbegin(), custom.cend(), [&id](const CustomCleanupLevel &level) { return level.id == id; });
}

bool toneOffered(const QString &id, const RefinementSettings &refinement)
{
    const QList<CustomTone> &custom = refinement.customTones;
    return kBuiltInTones.contains(id)
        || std::any_of(custom.cbegin(), custom.cend(), [&id](const CustomTone &tone) { return tone.id == id; });
}

// The lists as their records show them, for an edit to change. Terms are
// kept in send priority order and app matches with the per-application
// profile overrides they replaced folded in, which saving retires.
QList<VocabularyEntry> &editableTerms(AppSettings &settings)
{
    settings.vocabulary = normalizeVocabularyEntries(settings.vocabulary);
    return settings.vocabulary;
}

QList<AppRecognitionRule> &editableRules(AppSettings &settings)
{
    settings.appRecognitionRules = recognitionRulesWithMigratedProfileOverrides(
        settings.appRecognitionRules, settings.refinement.writingProfileOverrides);
    settings.refinement.writingProfileOverrides.clear();
    return settings.appRecognitionRules;
}

QList<WritingProfileSettings> &editableProfiles(AppSettings &settings)
{
    return settings.refinement.writingProfiles;
}

QList<CustomTone> &editableTones(AppSettings &settings)
{
    return settings.refinement.customTones;
}

QList<CustomCleanupLevel> &editableLevels(AppSettings &settings)
{
    return settings.refinement.customCleanupLevels;
}

QList<BindingRule> &editableReplacements(AppSettings &settings)
{
    return settings.bindings;
}

QList<LearnedCorrection> &editableCorrections(AppSettings &settings)
{
    return settings.learnedCorrections;
}

// Takes entry index out of the list listOf names, and returns the edit that
// puts it back at its place; empty for an index the list lacks.
template <typename T>
SettingsEdit takeOut(QList<T> &(*listOf)(AppSettings &), AppSettings &settings, int index)
{
    QList<T> &list = listOf(settings);
    if (index < 0 || index >= list.size()) {
        return {};
    }
    const T removed = list.takeAt(index);
    return [listOf, index, removed](AppSettings &draft) {
        QList<T> &back = listOf(draft);
        back.insert(std::min<qsizetype>(index, back.size()), removed);
    };
}

// Changes entry index of the list listOf names with patchEntry, which says
// whether the patch suits it.
template <typename T, typename Patch>
bool patchAt(QList<T> &(*listOf)(AppSettings &), AppSettings &settings, int index, Patch patchEntry)
{
    QList<T> &list = listOf(settings);
    if (index < 0 || index >= list.size()) {
        return false;
    }
    T entry = list.at(index);
    if (!patchEntry(entry, list, index)) {
        return false;
    }
    list[index] = entry;
    return true;
}

// Each patch below changes the fields it names on one entry, and says whether
// every one is a field the entry has and its value one it may hold. `self`
// is the entry's index in `list`, or -1 for one being added.

bool patchProfile(WritingProfileSettings &profile, const QVariantMap &patch,
                  const QList<WritingProfileSettings> &list, qsizetype self, const RefinementSettings &refinement)
{
    for (auto it = patch.cbegin(); it != patch.cend(); ++it) {
        const QString &key = it.key();
        const QString value = field(it.value());
        if (key == QLatin1String("name")) {
            const QString name = value.trimmed();
            const bool free = nameFree(list, self, name, [&list](const WritingProfileSettings &other) {
                return writingProfileLabel(other.profile, list);
            });
            if (isBuiltInWritingProfile(profile.profile) || name.isEmpty() || !free) {
                return false;
            }
            profile.name = name;
        } else if (key == QLatin1String("cleanup") && levelOffered(value, refinement)) {
            profile.cleanupStrength = value;
        } else if (key == QLatin1String("tone") && toneOffered(value, refinement)) {
            profile.tone = value;
        } else if (key == QLatin1String("language")) {
            profile.outputLanguage = value.trimmed();
        } else if (key == QLatin1String("instructions")) {
            profile.instructions = value;
        } else if (key == QLatin1String("speech") && (value.isEmpty() || isChainProviderId(ProviderRole::Speech, value))) {
            profile.speechProvider = value;
        } else if (key == QLatin1String("speechModel")) {
            profile.speechModel = value.trimmed();
        } else if (key == QLatin1String("refinement")
                   && (value.isEmpty() || isChainProviderId(ProviderRole::Refinement, value))) {
            profile.refinementProvider = value;
        } else if (key == QLatin1String("refinementModel")) {
            profile.refinementModel = value.trimmed();
        } else {
            return false;
        }
    }
    return true;
}

bool patchTone(CustomTone &tone, const QVariantMap &patch, const QList<CustomTone> &list, qsizetype self)
{
    for (auto it = patch.cbegin(); it != patch.cend(); ++it) {
        const QString value = field(it.value());
        if (it.key() == QLatin1String("name")) {
            tone.name = value.trimmed();
        } else if (it.key() == QLatin1String("instruction")) {
            tone.instruction = value;
        } else {
            return false;
        }
    }
    return !tone.name.isEmpty() && !tone.instruction.trimmed().isEmpty()
        && nameFree(list, self, tone.name, [](const CustomTone &other) { return other.name; });
}

bool patchLevel(CustomCleanupLevel &level, const QVariantMap &patch, const QList<CustomCleanupLevel> &list,
                qsizetype self)
{
    for (auto it = patch.cbegin(); it != patch.cend(); ++it) {
        const QString value = field(it.value());
        if (it.key() == QLatin1String("name")) {
            level.name = value.trimmed();
        } else if (it.key() == QLatin1String("base") && cleanupBases.contains(value)) {
            level.base = value;
        } else if (it.key() == QLatin1String("instructions")) {
            level.instructions = value;
        } else {
            return false;
        }
    }
    // Custom only is nothing but the instructions.
    const bool instructed = level.base != kCustomOnlyCleanupBase || !level.instructions.trimmed().isEmpty();
    return !level.name.isEmpty() && instructed
        && nameFree(list, self, level.name, [](const CustomCleanupLevel &other) { return other.name; });
}

bool patchTerm(VocabularyEntry &entry, const QVariantMap &patch, const QList<VocabularyEntry> &list, qsizetype self,
               const QList<WritingProfileSettings> &profiles)
{
    for (auto it = patch.cbegin(); it != patch.cend(); ++it) {
        if (it.key() == QLatin1String("term")) {
            entry.term = field(it.value()).simplified();
        } else if (it.key() == QLatin1String("keyTerm")) {
            entry.keyTerm = it.value().toBool();
        } else if (it.key() == QLatin1String("context")) {
            entry.context = field(it.value()).trimmed();
        } else if (it.key() == QLatin1String("profiles")) {
            entry.profiles = offeredVocabularyProfiles(it.value().toStringList(), profiles);
        } else {
            return false;
        }
    }
    return !entry.term.isEmpty() && nameFree(list, self, entry.term, [](const VocabularyEntry &other) { return other.term; });
}

bool patchReplacement(BindingRule &rule, const QVariantMap &patch)
{
    for (auto it = patch.cbegin(); it != patch.cend(); ++it) {
        if (it.key() == QLatin1String("phrase")) {
            rule.phrase = field(it.value()).trimmed();
        } else if (it.key() == QLatin1String("replacement")) {
            rule.replacement = field(it.value());
        } else {
            return false;
        }
    }
    return true;
}

bool patchRule(AppRecognitionRule &rule, const QVariantMap &patch, const QList<AppRecognitionRule> &list,
               qsizetype self, const QList<WritingProfileSettings> &profiles)
{
    for (auto it = patch.cbegin(); it != patch.cend(); ++it) {
        const QString value = field(it.value());
        if (it.key() == QLatin1String("match")) {
            rule.match = value.trimmed();
        } else if (it.key() == QLatin1String("category") && value.isEmpty()) {
            rule.category.reset();
        } else if (it.key() == QLatin1String("category") && appCategoryFromName(value) != AppCategory::Unknown) {
            rule.category = appCategoryFromName(value);
        } else if (it.key() == QLatin1String("profile") && value.isEmpty()) {
            rule.writingProfile.reset();
        } else if (it.key() == QLatin1String("profile") && !offeredWritingProfile(value, profiles, {}).isEmpty()) {
            rule.writingProfile = value;
        } else {
            return false;
        }
    }
    return !rule.match.isEmpty() && (rule.category || rule.writingProfile)
        && nameFree(list, self, rule.match, [](const AppRecognitionRule &other) { return other.match; });
}

// The replacements stay saveable: no blank or repeated phrase.
bool replacementsValid(const QList<BindingRule> &rules)
{
    return BindingProcessor::validateRules(rules).ok();
}

// The item's fields but its id, which goes to *id.
QVariantMap withoutId(const QVariantMap &item, QString *id)
{
    QVariantMap rest = item;
    *id = field(rest.take(QStringLiteral("id")));
    return rest;
}

// One list the UI edits: adding to it and changing an entry. A list removed
// from another way (profiles, tones, levels, corrections) has no remove.
struct EditedList {
    const char *name;
    bool (*add)(AppSettings &, const QVariantMap &);
    bool (*update)(AppSettings &, int, const QVariantMap &);
    SettingsEdit (*remove)(AppSettings &, int);
};

const EditedList editedLists[] = {
    {"profiles",
     [](AppSettings &s, const QVariantMap &item) {
         QList<WritingProfileSettings> &list = s.refinement.writingProfiles;
         WritingProfileSettings profile;
         const QVariantMap rest = withoutId(item, &profile.profile);
         if (!idFree(list, profile.profile, [](const WritingProfileSettings &p) { return p.profile; })
             || !rest.contains(QStringLiteral("name")) || !patchProfile(profile, rest, list, -1, s.refinement)) {
             return false;
         }
         list.append(profile);
         return true;
     },
     [](AppSettings &s, int index, const QVariantMap &patch) {
         const RefinementSettings refinement = s.refinement;
         return patchAt(editableProfiles, s, index, [&](WritingProfileSettings &profile, const auto &list, int self) {
             return patchProfile(profile, patch, list, self, refinement);
         });
     },
     nullptr},
    {"customTones",
     [](AppSettings &s, const QVariantMap &item) {
         QList<CustomTone> &list = s.refinement.customTones;
         CustomTone tone;
         const QVariantMap rest = withoutId(item, &tone.id);
         if (!idFree(list, tone.id, [](const CustomTone &t) { return t.id; }) || !patchTone(tone, rest, list, -1)) {
             return false;
         }
         list.append(tone);
         return true;
     },
     [](AppSettings &s, int index, const QVariantMap &patch) {
         return patchAt(editableTones, s, index,
                        [&](CustomTone &tone, const auto &list, int self) { return patchTone(tone, patch, list, self); });
     },
     nullptr},
    {"customCleanupLevels",
     [](AppSettings &s, const QVariantMap &item) {
         QList<CustomCleanupLevel> &list = s.refinement.customCleanupLevels;
         CustomCleanupLevel level;
         const QVariantMap rest = withoutId(item, &level.id);
         if (!idFree(list, level.id, [](const CustomCleanupLevel &l) { return l.id; })
             || !patchLevel(level, rest, list, -1)) {
             return false;
         }
         list.append(level);
         return true;
     },
     [](AppSettings &s, int index, const QVariantMap &patch) {
         return patchAt(editableLevels, s, index, [&](CustomCleanupLevel &level, const auto &list, int self) {
             return patchLevel(level, patch, list, self);
         });
     },
     nullptr},
    {"terms",
     [](AppSettings &s, const QVariantMap &item) {
         QList<VocabularyEntry> &list = editableTerms(s);
         VocabularyEntry entry;
         if (!patchTerm(entry, item, list, -1, s.refinement.writingProfiles)) {
             return false;
         }
         list.append(entry);
         return true;
     },
     [](AppSettings &s, int index, const QVariantMap &patch) {
         const QList<WritingProfileSettings> &profiles = s.refinement.writingProfiles;
         return patchAt(editableTerms, s, index, [&](VocabularyEntry &entry, const QList<VocabularyEntry> &list, int self) {
             return patchTerm(entry, patch, list, self, profiles);
         });
     },
     [](AppSettings &s, int index) { return takeOut(editableTerms, s, index); }},
    {"corrections",
     nullptr,
     [](AppSettings &s, int index, const QVariantMap &patch) {
         return patchAt(editableCorrections, s, index, [&](LearnedCorrection &correction, const auto &, int) {
             if (patch.keys() != QStringList{QStringLiteral("enabled")}) {
                 return false;
             }
             correction.enabled = patch.value(QStringLiteral("enabled")).toBool();
             return true;
         });
     },
     nullptr},
    {"replacements",
     [](AppSettings &s, const QVariantMap &item) {
         BindingRule rule;
         QList<BindingRule> &list = editableReplacements(s);
         if (!patchReplacement(rule, item)) {
             return false;
         }
         list.append(rule);
         return replacementsValid(list);
     },
     [](AppSettings &s, int index, const QVariantMap &patch) {
         return patchAt(editableReplacements, s, index,
                        [&](BindingRule &rule, const auto &, int) { return patchReplacement(rule, patch); })
             && replacementsValid(s.bindings);
     },
     [](AppSettings &s, int index) { return takeOut(editableReplacements, s, index); }},
    {"recognitionRules",
     [](AppSettings &s, const QVariantMap &item) {
         QList<AppRecognitionRule> &list = editableRules(s);
         AppRecognitionRule rule;
         if (!patchRule(rule, item, list, -1, s.refinement.writingProfiles)) {
             return false;
         }
         list.append(rule);
         return true;
     },
     [](AppSettings &s, int index, const QVariantMap &patch) {
         const QList<WritingProfileSettings> &profiles = s.refinement.writingProfiles;
         return patchAt(editableRules, s, index, [&](AppRecognitionRule &rule, const QList<AppRecognitionRule> &list, int self) {
             return patchRule(rule, patch, list, self, profiles);
         });
     },
     [](AppSettings &s, int index) { return takeOut(editableRules, s, index); }},
};

const EditedList *findList(const QString &name)
{
    for (const EditedList &list : editedLists) {
        if (name == QLatin1String(list.name)) {
            return &list;
        }
    }
    return nullptr;
}

QVariantMap ruleRecord(const AppRecognitionRule &rule)
{
    return {{QStringLiteral("match"), rule.match},
            {QStringLiteral("category"), rule.category ? appCategoryName(*rule.category) : QString()},
            {QStringLiteral("profile"), rule.writingProfile.value_or(QString())}};
}

// The file a person named for an import, read whole, or nothing with why.
std::optional<QByteArray> readImport(const QString &path, QString *error)
{
    QString local = path.trimmed();
    if (local.startsWith(QLatin1String("file:"))) {
        local = QUrl(local).toLocalFile();
    } else if (local == QLatin1String("~") || local.startsWith(QLatin1String("~/"))) {
        local = QDir::homePath() + local.mid(1);
    }
    QFile file(local);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return std::nullopt;
    }
    return file.readAll();
}

QVariantMap importResult(int added, const QString &error)
{
    return {{QStringLiteral("added"), added}, {QStringLiteral("error"), error}};
}

} // namespace

QuickLibrary::QuickLibrary(QObject *parent)
    : QuickLibrary(g_controller, parent)
{
}

QuickLibrary::QuickLibrary(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
    Q_ASSERT_X(controller, "QuickLibrary", "a ControllerScope lives while QML loads");
    SettingsStore *store = controller->settings();
    connect(store, &SettingsStore::snapshotApplied, this, &QuickLibrary::refresh);
    connect(store, &SettingsStore::vocabularyAdded, this, &QuickLibrary::refresh);
    connect(store, &SettingsStore::vocabularyUsageRecorded, this, &QuickLibrary::refresh);
    connect(store, &SettingsStore::learnedCorrectionsChanged, this, &QuickLibrary::refresh);
    refresh();
}

QuickLibrary::ControllerScope::ControllerScope(ApplicationController *controller)
{
    g_controller = controller;
}

QuickLibrary::ControllerScope::~ControllerScope()
{
    g_controller = nullptr;
}

QVariantList QuickLibrary::builtInRecognitionRules() const
{
    QVariantList rules;
    for (const AppRecognitionRule &rule : builtInAppRecognitionRules()) {
        rules.append(ruleRecord(rule));
    }
    return rules;
}

int QuickLibrary::keyTermLimit() const
{
    return VocabularyLimit::maxKeyterms;
}

int QuickLibrary::refinementTermLimit() const
{
    return VocabularyLimit::maxRefinementTerms;
}

QString QuickLibrary::newId(const QString &prefix) const
{
    if (!prefix.startsWith(kCustomIdPrefix)) {
        qWarning().noquote() << "Library.newId: the prefix" << prefix << "does not start with" << kCustomIdPrefix;
        return {};
    }
    const AppSettings settings = m_controller->settings()->dictationSnapshot();
    QStringList taken;
    for (const WritingProfileSettings &profile : settings.refinement.writingProfiles) {
        taken.append(profile.profile);
    }
    for (const CustomTone &tone : settings.refinement.customTones) {
        taken.append(tone.id);
    }
    for (const CustomCleanupLevel &level : settings.refinement.customCleanupLevels) {
        taken.append(level.id);
    }
    return newCustomId(prefix, taken);
}

bool QuickLibrary::add(const QString &list, const QVariantMap &item)
{
    const EditedList *edited = findList(list);
    if (edited && edited->add && save([&](AppSettings &s) { return edited->add(s, item); })) {
        return true;
    }
    qWarning().noquote() << "Library.add(" + list + ") refused an entry with" << item.keys();
    return false;
}

bool QuickLibrary::update(const QString &list, int index, const QVariantMap &patch)
{
    const EditedList *edited = findList(list);
    if (edited && edited->update && save([&](AppSettings &s) { return edited->update(s, index, patch); })) {
        return true;
    }
    qWarning().noquote() << "Library.update(" + list + ")" << index << "refused the fields" << patch.keys();
    return false;
}

int QuickLibrary::remove(const QString &list, int index)
{
    const EditedList *edited = findList(list);
    SettingsEdit undo;
    if (edited && edited->remove) {
        save([&](AppSettings &s) { return bool(undo = edited->remove(s, index)); });
    }
    if (!undo) {
        qWarning().noquote() << "Library.remove(" + list + ")" << index << "removed nothing";
    }
    return keepUndo(undo);
}

void QuickLibrary::removeProfile(const QString &id)
{
    // Saving drops what named it: app matches, term limits, the fallback.
    const bool removed = !isBuiltInWritingProfile(id) && save([&](AppSettings &s) {
        return s.refinement.writingProfiles.removeIf([&id](const WritingProfileSettings &p) { return p.profile == id; }) > 0;
    });
    if (!removed) {
        qWarning().noquote() << "Library.removeProfile:" << id << "is not one of the user's profiles";
    }
}

int QuickLibrary::removeTone(const QString &id)
{
    SettingsEdit undo;
    save([&](AppSettings &s) { return bool(undo = removeCustomTone(s, id)); });
    return keepUndo(undo);
}

int QuickLibrary::removeLevel(const QString &id)
{
    SettingsEdit undo;
    save([&](AppSettings &s) { return bool(undo = removeCustomCleanupLevel(s, id)); });
    return keepUndo(undo);
}

int QuickLibrary::undoLastCorrection()
{
    SettingsEdit undo;
    save([&](AppSettings &s) { return bool(undo = takeOut(editableCorrections, s, 0)); });
    return keepUndo(undo);
}

void QuickLibrary::undo(int token)
{
    const SettingsEdit edit = m_undos.take(token);
    if (!edit) {
        return;
    }
    save([&](AppSettings &s) {
        edit(s);
        return true;
    });
}

QVariantMap QuickLibrary::importTerms(const QString &path)
{
    QString error;
    const std::optional<QByteArray> csv = readImport(path, &error);
    if (!csv) {
        return importResult(0, error);
    }
    const QList<VocabularyEntry> incoming = parseVocabularyCsv(*csv, &error);
    if (!error.isEmpty()) {
        return importResult(0, error);
    }
    int added = 0;
    save([&](AppSettings &s) {
        QList<VocabularyEntry> &terms = editableTerms(s);
        for (const VocabularyEntry &entry : incoming) {
            if (nameFree(terms, -1, entry.term, [](const VocabularyEntry &other) { return other.term; })) {
                terms.append(entry);
                ++added;
            }
        }
        return added > 0;
    });
    return importResult(added, {});
}

QVariantMap QuickLibrary::importReplacements(const QString &path)
{
    QString error;
    const std::optional<QByteArray> json = readImport(path, &error);
    if (!json) {
        return importResult(0, error);
    }
    const QList<BindingRule> incoming = BindingProcessor::parseJsonImport(*json, &error);
    if (!error.isEmpty()) {
        return importResult(0, error);
    }
    int added = 0;
    save([&](AppSettings &s) {
        QList<BindingRule> &rules = editableReplacements(s);
        for (const BindingRule &rule : incoming) {
            const QString phrase = BindingProcessor::normalizedPhrase(rule.phrase);
            const bool held = std::any_of(rules.cbegin(), rules.cend(), [&phrase](const BindingRule &other) {
                return BindingProcessor::normalizedPhrase(other.phrase) == phrase;
            });
            if (!held) {
                rules.append(rule);
                ++added;
            }
        }
        return added > 0 && replacementsValid(rules);
    });
    return importResult(added, {});
}

bool QuickLibrary::save(const std::function<bool(AppSettings &)> &edit)
{
    // As Settings.set does: the snapshot leaves unread keyring secrets
    // unread, and applySnapshot keeps them.
    SettingsStore *store = m_controller->settings();
    AppSettings draft = store->dictationSnapshot();
    if (!edit(draft)) {
        return false;
    }
    store->applySnapshot(draft);
    return true;
}

int QuickLibrary::keepUndo(const SettingsEdit &undo)
{
    if (!undo) {
        return -1;
    }
    m_undos.insert(++m_lastUndo, undo);
    return m_lastUndo;
}

void QuickLibrary::refresh()
{
    const AppSettings settings = m_controller->settings()->dictationSnapshot();
    const RefinementSettings &refinement = settings.refinement;

    QVariantList profiles;
    for (const WritingProfileSettings &profile : refinement.writingProfiles) {
        profiles.append(QVariantMap{
            {QStringLiteral("id"), profile.profile},
            {QStringLiteral("name"), writingProfileLabel(profile.profile, refinement.writingProfiles)},
            {QStringLiteral("builtIn"), isBuiltInWritingProfile(profile.profile)},
            {QStringLiteral("cleanup"), profile.cleanupStrength},
            {QStringLiteral("tone"), profile.tone},
            {QStringLiteral("language"), profile.outputLanguage},
            {QStringLiteral("instructions"), profile.instructions},
            {QStringLiteral("speech"), profile.speechProvider},
            {QStringLiteral("speechModel"), profile.speechModel},
            {QStringLiteral("refinement"), profile.refinementProvider},
            {QStringLiteral("refinementModel"), profile.refinementModel},
        });
    }
    QVariantList tones;
    for (const CustomTone &tone : refinement.customTones) {
        tones.append(QVariantMap{{QStringLiteral("id"), tone.id},
                                 {QStringLiteral("name"), tone.name},
                                 {QStringLiteral("instruction"), tone.instruction}});
    }
    QVariantList levels;
    for (const CustomCleanupLevel &level : refinement.customCleanupLevels) {
        levels.append(QVariantMap{{QStringLiteral("id"), level.id},
                                  {QStringLiteral("name"), level.name},
                                  {QStringLiteral("base"), level.base},
                                  {QStringLiteral("instructions"), level.instructions}});
    }
    QVariantList terms;
    QStringList keyTerms;
    for (const VocabularyEntry &entry : normalizeVocabularyEntries(settings.vocabulary)) {
        terms.append(QVariantMap{{QStringLiteral("term"), entry.term},
                                 {QStringLiteral("keyTerm"), entry.keyTerm},
                                 {QStringLiteral("context"), entry.context},
                                 {QStringLiteral("profiles"), entry.profiles},
                                 {QStringLiteral("source"), entry.source},
                                 {QStringLiteral("uses"), qMax(0, entry.frequency)}});
        if (entry.keyTerm) {
            keyTerms.append(entry.term);
        }
    }
    QVariantList corrections;
    for (const LearnedCorrection &correction : settings.learnedCorrections) {
        corrections.append(QVariantMap{{QStringLiteral("id"), correction.id},
                                       {QStringLiteral("heard"), correction.original},
                                       {QStringLiteral("corrected"), correction.corrected},
                                       {QStringLiteral("app"), correction.applicationId},
                                       {QStringLiteral("confidence"), correction.confidence},
                                       {QStringLiteral("enabled"), correction.enabled}});
    }
    QVariantList replacements;
    for (const BindingRule &rule : settings.bindings) {
        replacements.append(QVariantMap{{QStringLiteral("phrase"), rule.phrase},
                                        {QStringLiteral("replacement"), rule.replacement}});
    }
    QVariantList rules;
    for (const AppRecognitionRule &rule :
         recognitionRulesWithMigratedProfileOverrides(settings.appRecognitionRules, refinement.writingProfileOverrides)) {
        rules.append(ruleRecord(rule));
    }
    const QString &speech = settings.speech.providerId;
    const bool takesKeyTerms = VocabularyLimit::takesKeyterms(speech);
    const int keyTermsSent = int(VocabularyLimit::speechKeyterms(keyTerms, speech).size());

    if (profiles == m_profiles && tones == m_customTones && levels == m_customCleanupLevels && terms == m_terms
        && corrections == m_corrections && replacements == m_replacements && rules == m_recognitionRules
        && takesKeyTerms == m_speechTakesKeyTerms && keyTermsSent == m_keyTermsSent) {
        return;
    }
    m_profiles = profiles;
    m_customTones = tones;
    m_customCleanupLevels = levels;
    m_terms = terms;
    m_corrections = corrections;
    m_replacements = replacements;
    m_recognitionRules = rules;
    m_speechTakesKeyTerms = takesKeyTerms;
    m_keyTermsSent = keyTermsSent;
    emit listsChanged();
}

} // namespace speecher
