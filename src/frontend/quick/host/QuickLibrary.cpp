#include "frontend/quick/host/QuickLibrary.h"

#include "app/ApplicationController.h"
#include "core/BindingProcessor.h"
#include "core/SettingsStore.h"
#include "core/Vocabulary.h"
#include "core/VocabularyEdits.h"
#include "core/VocabularyLimit.h"
#include "core/WritingProfileEdits.h"

#include <QDebug>
#include <QFile>

#include <algorithm>
#include <optional>
#include <utility>

namespace speecher {

namespace {

ApplicationController *g_controller = nullptr;

// What add and update return for a record they can't convert.
const QString invalidRecord = QStringLiteral("invalid");

QString refusalName(EditRefusal refusal)
{
    switch (refusal) {
    case EditRefusal::None:
        return {};
    case EditRefusal::Blank:
        return QStringLiteral("blank");
    case EditRefusal::Taken:
        return QStringLiteral("taken");
    case EditRefusal::Incomplete:
        return QStringLiteral("incomplete");
    case EditRefusal::Missing:
        return QStringLiteral("missing");
    case EditRefusal::BuiltIn:
        return QStringLiteral("builtIn");
    }
    return invalidRecord;
}

// Each setter below copies the record fields `fields` names onto an entry;
// the list's EditedList names the fields it takes.

void setProfileFields(WritingProfileSettings &profile, const QVariantMap &fields)
{
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        const QString value = it.value().toString();
        if (it.key() == QLatin1String("name")) {
            profile.name = value;
        } else if (it.key() == QLatin1String("cleanup")) {
            profile.cleanupStrength = value;
        } else if (it.key() == QLatin1String("tone")) {
            profile.tone = value;
        } else if (it.key() == QLatin1String("language")) {
            profile.outputLanguage = value;
        } else if (it.key() == QLatin1String("instructions")) {
            profile.instructions = value;
        } else if (it.key() == QLatin1String("speech")) {
            profile.speechProvider = value;
        } else if (it.key() == QLatin1String("speechModel")) {
            profile.speechModel = value;
        } else if (it.key() == QLatin1String("refinement")) {
            profile.refinementProvider = value;
        } else if (it.key() == QLatin1String("refinementModel")) {
            profile.refinementModel = value;
        }
    }
}

void setToneFields(CustomTone &tone, const QVariantMap &fields)
{
    if (fields.contains(QStringLiteral("name"))) {
        tone.name = fields.value(QStringLiteral("name")).toString();
    }
    if (fields.contains(QStringLiteral("instruction"))) {
        tone.instruction = fields.value(QStringLiteral("instruction")).toString();
    }
}

void setLevelFields(CustomCleanupLevel &level, const QVariantMap &fields)
{
    if (fields.contains(QStringLiteral("name"))) {
        level.name = fields.value(QStringLiteral("name")).toString();
    }
    if (fields.contains(QStringLiteral("base"))) {
        level.base = fields.value(QStringLiteral("base")).toString();
    }
    if (fields.contains(QStringLiteral("instructions"))) {
        level.instructions = fields.value(QStringLiteral("instructions")).toString();
    }
}

void setTermFields(VocabularyEntry &entry, const QVariantMap &fields)
{
    if (fields.contains(QStringLiteral("term"))) {
        entry.term = fields.value(QStringLiteral("term")).toString();
    }
    if (fields.contains(QStringLiteral("keyTerm"))) {
        entry.keyTerm = fields.value(QStringLiteral("keyTerm")).toBool();
    }
    if (fields.contains(QStringLiteral("context"))) {
        entry.context = fields.value(QStringLiteral("context")).toString();
    }
    if (fields.contains(QStringLiteral("profiles"))) {
        entry.profiles = fields.value(QStringLiteral("profiles")).toStringList();
    }
}

void setCorrectionFields(LearnedCorrection &correction, const QVariantMap &fields)
{
    if (fields.contains(QStringLiteral("heard"))) {
        correction.original = fields.value(QStringLiteral("heard")).toString();
    }
    if (fields.contains(QStringLiteral("corrected"))) {
        correction.corrected = fields.value(QStringLiteral("corrected")).toString();
    }
    if (fields.contains(QStringLiteral("enabled"))) {
        correction.enabled = fields.value(QStringLiteral("enabled")).toBool();
    }
}

void setReplacementFields(BindingRule &rule, const QVariantMap &fields)
{
    if (fields.contains(QStringLiteral("phrase"))) {
        rule.phrase = fields.value(QStringLiteral("phrase")).toString();
    }
    if (fields.contains(QStringLiteral("replacement"))) {
        rule.replacement = fields.value(QStringLiteral("replacement")).toString();
    }
}

// An empty category or profile sets none.
void setRuleFields(AppRecognitionRule &rule, const QVariantMap &fields)
{
    if (fields.contains(QStringLiteral("match"))) {
        rule.match = fields.value(QStringLiteral("match")).toString();
    }
    if (fields.contains(QStringLiteral("category"))) {
        const QString category = fields.value(QStringLiteral("category")).toString();
        rule.category = category.isEmpty() ? std::nullopt : std::optional(appCategoryFromName(category));
    }
    if (fields.contains(QStringLiteral("profile"))) {
        const QString profile = fields.value(QStringLiteral("profile")).toString();
        rule.writingProfile = profile.isEmpty() ? std::nullopt : std::optional(profile);
    }
}

// One list the UI edits: the fields its records take, adding to it, changing
// the entry a key names, and removing that entry. Adding and changing take
// the built-in names the page shows, which only tones and levels use. A list
// removed from another way (profiles, tones, levels) has no remove; one only
// dictation adds to (corrections) has no add.
struct EditedList {
    const char *name;
    QStringList fields;
    EditRefusal (*add)(AppSettings &, const QVariantMap &, const QStringList &);
    EditRefusal (*update)(AppSettings &, const QString &, const QVariantMap &, const QStringList &);
    SettingsEdit (*remove)(AppSettings &, const QString &);
};

const EditedList editedLists[] = {
    {"profiles",
     {QStringLiteral("name"), QStringLiteral("cleanup"), QStringLiteral("tone"), QStringLiteral("language"),
      QStringLiteral("instructions"), QStringLiteral("speech"), QStringLiteral("speechModel"),
      QStringLiteral("refinement"), QStringLiteral("refinementModel")},
     [](AppSettings &s, const QVariantMap &item, const QStringList &) {
         WritingProfileSettings profile;
         profile.profile = item.value(QStringLiteral("id")).toString();
         setProfileFields(profile, item);
         return addWritingProfile(s, profile);
     },
     [](AppSettings &s, const QString &id, const QVariantMap &patch, const QStringList &) {
         return editWritingProfile(s, id, [&patch](WritingProfileSettings &profile) { setProfileFields(profile, patch); });
     },
     nullptr},
    {"customTones",
     {QStringLiteral("name"), QStringLiteral("instruction")},
     [](AppSettings &s, const QVariantMap &item, const QStringList &builtInNames) {
         CustomTone tone;
         tone.id = item.value(QStringLiteral("id")).toString();
         setToneFields(tone, item);
         return addCustomTone(s, tone, builtInNames);
     },
     [](AppSettings &s, const QString &id, const QVariantMap &patch, const QStringList &builtInNames) {
         return editCustomTone(s, id, [&patch](CustomTone &tone) { setToneFields(tone, patch); }, builtInNames);
     },
     nullptr},
    {"customCleanupLevels",
     {QStringLiteral("name"), QStringLiteral("base"), QStringLiteral("instructions")},
     [](AppSettings &s, const QVariantMap &item, const QStringList &builtInNames) {
         CustomCleanupLevel level;
         level.id = item.value(QStringLiteral("id")).toString();
         setLevelFields(level, item);
         return addCustomCleanupLevel(s, level, builtInNames);
     },
     [](AppSettings &s, const QString &id, const QVariantMap &patch, const QStringList &builtInNames) {
         return editCustomCleanupLevel(
             s, id, [&patch](CustomCleanupLevel &level) { setLevelFields(level, patch); }, builtInNames);
     },
     nullptr},
    {"terms",
     {QStringLiteral("term"), QStringLiteral("keyTerm"), QStringLiteral("context"), QStringLiteral("profiles")},
     [](AppSettings &s, const QVariantMap &item, const QStringList &) {
         VocabularyEntry entry;
         setTermFields(entry, item);
         return addVocabularyEntry(s, entry);
     },
     [](AppSettings &s, const QString &term, const QVariantMap &patch, const QStringList &) {
         return editVocabularyEntry(s, term, [&patch](VocabularyEntry &entry) { setTermFields(entry, patch); });
     },
     removeVocabularyEntry},
    {"corrections",
     {QStringLiteral("heard"), QStringLiteral("corrected"), QStringLiteral("enabled")},
     nullptr,
     [](AppSettings &s, const QString &id, const QVariantMap &patch, const QStringList &) {
         return editLearnedCorrection(s, id, [&patch](LearnedCorrection &correction) { setCorrectionFields(correction, patch); });
     },
     removeLearnedCorrection},
    {"replacements",
     {QStringLiteral("phrase"), QStringLiteral("replacement")},
     [](AppSettings &s, const QVariantMap &item, const QStringList &) {
         BindingRule rule;
         setReplacementFields(rule, item);
         return addBindingRule(s, rule);
     },
     [](AppSettings &s, const QString &phrase, const QVariantMap &patch, const QStringList &) {
         return editBindingRule(s, phrase, [&patch](BindingRule &rule) { setReplacementFields(rule, patch); });
     },
     removeBindingRule},
    {"recognitionRules",
     {QStringLiteral("match"), QStringLiteral("category"), QStringLiteral("profile")},
     [](AppSettings &s, const QVariantMap &item, const QStringList &) {
         AppRecognitionRule rule;
         setRuleFields(rule, item);
         return addAppRecognitionRule(s, rule);
     },
     [](AppSettings &s, const QString &match, const QVariantMap &patch, const QStringList &) {
         return editAppRecognitionRule(s, match, [&patch](AppRecognitionRule &rule) { setRuleFields(rule, patch); });
     },
     removeAppRecognitionRule},
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

// Whether every field of record is one the list's records have, with a value
// its setter can convert. An add's id is checked by core.
bool convertible(const EditedList &list, QVariantMap record)
{
    record.remove(QStringLiteral("id"));
    const QString category = record.value(QStringLiteral("category")).toString();
    const bool knownCategory = category.isEmpty() || appCategoryFromName(category) != AppCategory::Unknown;
    const QStringList keys = record.keys();
    return knownCategory && std::all_of(keys.cbegin(), keys.cend(), [&list](const QString &key) {
        return list.fields.contains(key);
    });
}

QVariantMap ruleRecord(const AppRecognitionRule &rule)
{
    return {{QStringLiteral("match"), rule.match},
            {QStringLiteral("category"), rule.category ? appCategoryName(*rule.category) : QString()},
            {QStringLiteral("profile"), rule.writingProfile.value_or(QString())}};
}

// The file chosen for an import, read whole, or nothing with why.
std::optional<QByteArray> readImport(const QUrl &url, QString *error)
{
    QFile file(url.toLocalFile());
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

QString QuickLibrary::add(const QString &list, const QVariantMap &item, const QStringList &builtInNames)
{
    const EditedList *edited = findList(list);
    if (!edited || !edited->add || !convertible(*edited, item)) {
        qWarning().noquote() << "Library.add(" + list + ") takes no entry with" << item.keys();
        return invalidRecord;
    }
    return saveUnlessRefused([&](AppSettings &s) { return edited->add(s, item, builtInNames); });
}

QString QuickLibrary::update(const QString &list, const QString &key, const QVariantMap &patch,
                            const QStringList &builtInNames)
{
    const EditedList *edited = findList(list);
    if (!edited || !edited->update || patch.contains(QStringLiteral("id")) || !convertible(*edited, patch)) {
        qWarning().noquote() << "Library.update(" + list + ") takes no fields" << patch.keys();
        return invalidRecord;
    }
    return saveUnlessRefused([&](AppSettings &s) { return edited->update(s, key, patch, builtInNames); });
}

int QuickLibrary::remove(const QString &list, const QString &key)
{
    const EditedList *edited = findList(list);
    SettingsEdit undo;
    if (edited && edited->remove) {
        save([&](AppSettings &s) { return bool(undo = edited->remove(s, key)); });
    }
    return keepUndo(undo);
}

void QuickLibrary::removeProfile(const QString &id)
{
    // Saving drops what named it: app matches, term limits, the fallback.
    if (!save([&id](AppSettings &s) { return removeWritingProfile(s, id); })) {
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
    save([&](AppSettings &s) {
        return !s.learnedCorrections.isEmpty() && bool(undo = removeLearnedCorrection(s, s.learnedCorrections.first().id));
    });
    return keepUndo(undo);
}

void QuickLibrary::undo(int token)
{
    if (token != m_lastUndo || !m_undo) {
        return;
    }
    const SettingsEdit edit = std::exchange(m_undo, {});
    save([&](AppSettings &s) {
        edit(s);
        return true;
    });
}

QVariantMap QuickLibrary::importTerms(const QUrl &file)
{
    QString error;
    const std::optional<QByteArray> csv = readImport(file, &error);
    if (!csv) {
        return importResult(0, error);
    }
    const QList<VocabularyEntry> incoming = parseVocabularyCsv(*csv, &error);
    if (!error.isEmpty()) {
        return importResult(0, error);
    }
    int added = 0;
    save([&](AppSettings &s) {
        const QList<VocabularyEntry> before = s.vocabulary;
        added = importVocabularyEntries(s, incoming);
        // A term listed already can take a context it lacked, so a file that
        // adds none may still change the list.
        return s.vocabulary != before;
    });
    return importResult(added, {});
}

QVariantMap QuickLibrary::importReplacements(const QUrl &file)
{
    QString error;
    const std::optional<QByteArray> json = readImport(file, &error);
    if (!json) {
        return importResult(0, error);
    }
    const QList<BindingRule> incoming = BindingProcessor::parseJsonImport(*json, &error);
    if (!error.isEmpty()) {
        return importResult(0, error);
    }
    int added = 0;
    save([&](AppSettings &s) { return (added = importBindingRules(s, incoming)) > 0; });
    return importResult(added, {});
}

bool QuickLibrary::save(const std::function<bool(AppSettings &)> &edit)
{
    return m_controller->settings()->edit(edit);
}

QString QuickLibrary::saveUnlessRefused(const std::function<EditRefusal(AppSettings &)> &edit)
{
    EditRefusal refusal = EditRefusal::None;
    save([&](AppSettings &s) { return (refusal = edit(s)) == EditRefusal::None; });
    return refusalName(refusal);
}

int QuickLibrary::keepUndo(const SettingsEdit &undo)
{
    if (!undo) {
        return -1;
    }
    m_undo = undo;
    return ++m_lastUndo;
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
