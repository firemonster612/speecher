#include "frontend/quick/host/QuickLibrary.h"

#include "app/ApplicationController.h"
#include "core/BindingProcessor.h"
#include "core/SettingsStore.h"
#include "core/Vocabulary.h"
#include "core/VocabularyEdits.h"
#include "core/VocabularyLimit.h"
#include "core/WritingProfileEdits.h"
#include "dictation/DictationPlan.h"
#include "frontend/quick/host/QmlValues.h"
#include "providers/ProviderRegistry.h"

#include <QDebug>
#include <QFile>

#include <algorithm>
#include <optional>
#include <utility>

namespace speecher {

namespace {

// Why a dictation with this profile doesn't lead with its own speech
// service, as QuickLibrary::profiles words it, or empty while it does. The
// resolver decides, so the editor never claims a service a dictation skips.
QString speechSkipped(const AppSettings &settings, const WritingProfileSettings &profile, const OffersSpeech &offersSpeech)
{
    if (!profile.speechFirst) {
        return {};
    }
    RunOverrides run;
    run.writingProfile = profile.profile;
    const DictationPlan plan = planDictation(settings, Target{}, run, offersSpeech);
    if (plan.sources.value(QStringLiteral("speech")) == QStringLiteral("profile:") + profile.profile) {
        return {};
    }
    return offersSpeech && !offersSpeech(profile.speechFirst->service) ? QStringLiteral("unavailable")
                                                                       : QStringLiteral("language");
}

ApplicationController *g_controller = nullptr;

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
        }
    }
    // A service of its own, or "" for the page's order; its model goes with
    // it, and only with it.
    const auto setFirst = [&fields](std::optional<ServiceChoice> &first, const char *serviceField, const char *modelField) {
        const QString serviceKey = QLatin1String(serviceField);
        if (fields.contains(serviceKey)) {
            const QString service = fields.value(serviceKey).toString();
            first = service.isEmpty() ? std::nullopt : std::optional(ServiceChoice{service});
        }
        const QString modelKey = QLatin1String(modelField);
        if (first && fields.contains(modelKey)) {
            first->model = fields.value(modelKey).toString();
        }
    };
    setFirst(profile.speechFirst, "speech", "speechModel");
    setFirst(profile.refinementFirst, "refinement", "refinementModel");
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

void setVocabularyFields(VocabularyEntry &entry, const QVariantMap &fields)
{
    if (fields.contains(QStringLiteral("term"))) {
        entry.term = fields.value(QStringLiteral("term")).toString();
    }
    if (fields.contains(QStringLiteral("replacement"))) {
        entry.replacement = fields.value(QStringLiteral("replacement")).toString();
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
    if (fields.contains(QStringLiteral("apps"))) {
        entry.apps = fields.value(QStringLiteral("apps")).toStringList();
    }
    if (fields.contains(QStringLiteral("enabled"))) {
        entry.enabled = fields.value(QStringLiteral("enabled")).toBool();
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
    {"vocabulary",
     {QStringLiteral("term"), QStringLiteral("replacement"), QStringLiteral("keyTerm"), QStringLiteral("context"),
      QStringLiteral("profiles"), QStringLiteral("apps"), QStringLiteral("enabled")},
     [](AppSettings &s, const QVariantMap &item, const QStringList &) {
         VocabularyEntry entry;
         setVocabularyFields(entry, item);
         return addVocabularyEntry(s, entry);
     },
     [](AppSettings &s, const QString &id, const QVariantMap &patch, const QStringList &) {
         return editVocabularyEntry(s, id, [&patch](VocabularyEntry &entry) { setVocabularyFields(entry, patch); });
     },
     removeVocabularyEntry},
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
    const QStringList keys = record.keys();
    return std::all_of(keys.cbegin(), keys.cend(), [&list](const QString &key) {
        return list.fields.contains(key);
    });
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
    if (!edited || !edited->remove) {
        qWarning().noquote() << "Library.remove(" + list + ") is no list to remove from";
        return -1;
    }
    SettingsEdit undo;
    save([&](AppSettings &s) { return bool(undo = edited->remove(s, key)); });
    return keepUndo(undo);
}

void QuickLibrary::removeProfile(const QString &id)
{
    // Saving drops what named it: Apps' and App Types' profile, term limits.
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
        const VocabularyEntry *newest = nullptr;
        for (const VocabularyEntry &entry : s.vocabulary) {
            if (entry.isLearned() && (!newest || entry.createdAtMs > newest->createdAtMs)) {
                newest = &entry;
            }
        }
        return newest && bool(undo = removeVocabularyEntry(s, QString(newest->id)));
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

QVariantMap QuickLibrary::importVocabulary(const QUrl &file)
{
    QString error;
    const std::optional<QByteArray> contents = readImport(file, &error);
    if (!contents) {
        return importResult(0, error);
    }
    QList<VocabularyEntry> incoming;
    if (file.toLocalFile().endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        for (const BindingRule &rule : BindingProcessor::parseJsonImport(*contents, &error)) {
            VocabularyEntry entry{rule.phrase, QStringLiteral("import")};
            entry.replacement = rule.replacement;
            incoming.append(entry);
        }
    } else {
        incoming = parseVocabularyCsv(*contents, &error);
    }
    if (!error.isEmpty()) {
        return importResult(0, error);
    }
    int added = 0;
    save([&](AppSettings &s) {
        const QList<VocabularyEntry> before = s.vocabulary;
        added = importVocabularyEntries(s, incoming);
        // A spelling listed already can take a context it lacked, so a file
        // that adds none may still change the list.
        return s.vocabulary != before;
    });
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
    const ProviderRegistry *registry = m_controller->providerRegistry();
    const OffersSpeech offersSpeech = registry ? registry->offersSpeech() : OffersSpeech();

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
            {QStringLiteral("speech"), profile.speechFirst ? profile.speechFirst->service : QString()},
            {QStringLiteral("speechModel"), profile.speechFirst ? profile.speechFirst->model : QString()},
            {QStringLiteral("speechSkipped"), speechSkipped(settings, profile, offersSpeech)},
            {QStringLiteral("refinement"), profile.refinementFirst ? profile.refinementFirst->service : QString()},
            {QStringLiteral("refinementModel"), profile.refinementFirst ? profile.refinementFirst->model : QString()},
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
    QVariantList vocabulary;
    QStringList keyTerms;
    for (const VocabularyEntry &entry : normalizeVocabularyEntries(settings.vocabulary)) {
        vocabulary.append(QVariantMap{{QStringLiteral("id"), entry.id},
                                      {QStringLiteral("kind"), entry.isReplacement() ? QStringLiteral("replacement")
                                                                                      : QStringLiteral("spelling")},
                                      {QStringLiteral("term"), entry.term},
                                      {QStringLiteral("replacement"), entry.replacement},
                                      {QStringLiteral("keyTerm"), entry.keyTerm},
                                      {QStringLiteral("context"), entry.context},
                                      {QStringLiteral("profiles"), entry.profiles},
                                      {QStringLiteral("apps"), entry.apps},
                                      {QStringLiteral("source"), entry.source},
                                      {QStringLiteral("learned"), entry.isLearned()},
                                      {QStringLiteral("enabled"), entry.enabled},
                                      {QStringLiteral("uses"), qMax(0, entry.frequency)},
                                      {QStringLiteral("confidence"), entry.confidence},
                                      {QStringLiteral("evidence"), entry.evidenceCount},
                                      {QStringLiteral("learnedAtMs"), double(entry.createdAtMs)}});
        if (!entry.isReplacement() && entry.keyTerm && entry.enabled) {
            keyTerms.append(entry.term);
        }
    }
    const QString speech = settings.speech.services.main();
    const bool takesKeyTerms = VocabularyLimit::takesKeyterms(speech);
    const int keyTermsSent = int(VocabularyLimit::speechKeyterms(keyTerms, speech).size());

    if (profiles == m_profiles && tones == m_customTones && levels == m_customCleanupLevels
        && vocabulary == m_vocabulary && takesKeyTerms == m_speechTakesKeyTerms && keyTermsSent == m_keyTermsSent) {
        return;
    }
    m_profiles = profiles;
    m_customTones = tones;
    m_customCleanupLevels = levels;
    m_vocabulary = vocabulary;
    m_speechTakesKeyTerms = takesKeyTerms;
    m_keyTermsSent = keyTermsSent;
    emit listsChanged();
}

} // namespace speecher
