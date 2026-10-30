#pragma once

#include "core/SettingsEdit.h"

#include <QObject>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantList>

namespace speecher {

class ApplicationController;

// The UI's LibraryLists: the Writing Profiles with the user's tones and
// cleanup levels, the vocabulary terms, Learned Corrections, replacements
// and app matches, as plain values read from and written through the
// daemon's SettingsStore. The Library singleton (Library.qml) builds on it
// and words it. Edits name a list by its property and an entry by its key
// there (see update). Core's WritingProfileEdits and VocabularyEdits make
// them and say why one is refused; this converts the records.
class QuickLibrary : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(LibraryLists)
    // { id, name, builtIn, cleanup, tone, language, instructions, speech,
    // speechModel, refinement, refinementModel }: the built-ins, then the
    // user's in the order added. An empty service or model is the one the
    // Speech to text or Refinement page chose.
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY listsChanged)
    // { id, name, instruction }
    Q_PROPERTY(QVariantList customTones READ customTones NOTIFY listsChanged)
    // { id, name, base, instructions }; base is light_cleanup, balanced,
    // strong_polish or custom_only.
    Q_PROPERTY(QVariantList customCleanupLevels READ customCleanupLevels NOTIFY listsChanged)
    // { term, keyTerm, context, profiles, source, uses }, in the order the
    // speech service is sent them. source is manual, csv or learned; an
    // empty profiles is every profile.
    Q_PROPERTY(QVariantList terms READ terms NOTIFY listsChanged)
    // { id, heard, corrected, app, confidence, enabled }, newest first. app is
    // the application id it was learned in, empty for one learned in several.
    Q_PROPERTY(QVariantList corrections READ corrections NOTIFY listsChanged)
    // { phrase, replacement }
    Q_PROPERTY(QVariantList replacements READ replacements NOTIFY listsChanged)
    // { match, category, profile }: the user's app matches, which win over
    // the built-in ones. An empty category or profile sets none.
    Q_PROPERTY(QVariantList recognitionRules READ recognitionRules NOTIFY listsChanged)
    // The same for the matches Speecher ships with.
    Q_PROPERTY(QVariantList builtInRecognitionRules READ builtInRecognitionRules CONSTANT)
    // How many key terms a speech service takes at most, and how many terms
    // refinement reads.
    Q_PROPERTY(int keyTermLimit READ keyTermLimit CONSTANT)
    Q_PROPERTY(int refinementTermLimit READ refinementTermLimit CONSTANT)
    // Whether the chosen speech service takes key terms, and how many of the
    // key terms it receives.
    Q_PROPERTY(bool speechTakesKeyTerms READ speechTakesKeyTerms NOTIFY listsChanged)
    Q_PROPERTY(int keyTermsSent READ keyTermsSent NOTIFY listsChanged)

public:
    // While one lives, the Library QML makes reads and saves through its
    // controller. The host keeps one for as long as it loads QML, as does a
    // test that loads a page.
    class ControllerScope {
    public:
        explicit ControllerScope(ApplicationController *controller);
        ~ControllerScope();
        Q_DISABLE_COPY_MOVE(ControllerScope)
    };

    // QML makes it as Library's base, on the controller of the live
    // ControllerScope.
    explicit QuickLibrary(QObject *parent = nullptr);
    explicit QuickLibrary(ApplicationController *controller, QObject *parent = nullptr);

    QVariantList profiles() const { return m_profiles; }
    QVariantList customTones() const { return m_customTones; }
    QVariantList customCleanupLevels() const { return m_customCleanupLevels; }
    QVariantList terms() const { return m_terms; }
    QVariantList corrections() const { return m_corrections; }
    QVariantList replacements() const { return m_replacements; }
    QVariantList recognitionRules() const { return m_recognitionRules; }
    QVariantList builtInRecognitionRules() const;
    int keyTermLimit() const;
    int refinementTermLimit() const;
    bool speechTakesKeyTerms() const { return m_speechTakesKeyTerms; }
    int keyTermsSent() const { return m_keyTermsSent; }

    // An id for a profile, tone or cleanup level to add, starting with
    // prefix (custom_profile, custom_tone, custom_level).
    Q_INVOKABLE QString newId(const QString &prefix) const;
    // Adds item to the list named (profiles, customTones, customCleanupLevels,
    // terms, replacements, recognitionRules), with the fields its records
    // show; one of the first three carries its newId. A tone or level takes
    // builtInNames, the names the page shows the built-in ones by, which it
    // can't be called. Returns why it was refused, as EditRefusal says: blank,
    // taken, incomplete, missing or builtIn, or invalid for a field the
    // records lack. Empty when saved.
    Q_INVOKABLE QString add(const QString &list, const QVariantMap &item, const QStringList &builtInNames = {});
    // Changes the fields patch names on the entry `key` names in the list
    // named: a profile's, tone's, level's or correction's id, a term, a
    // replacement's phrase or a match's app. What the record does not show
    // stays. Returns as add does; missing once the entry is gone.
    Q_INVOKABLE QString update(const QString &list, const QString &key, const QVariantMap &patch,
                               const QStringList &builtInNames = {});
    // Removes the entry key names from terms, corrections, replacements or
    // recognitionRules. Returns the token undo takes to put it back, or -1
    // when nothing was removed.
    Q_INVOKABLE int remove(const QString &list, const QString &key);
    // Removes one of the user's profiles, and saving it drops what named it:
    // app matches lose it, terms limited to it lose the limit, and the
    // fallback profile becomes Other. No undo: the page asks first.
    Q_INVOKABLE void removeProfile(const QString &id);
    // Remove one of the user's tones or cleanup levels, moving the profiles
    // that chose it, and return the undo token, or -1.
    Q_INVOKABLE int removeTone(const QString &id);
    Q_INVOKABLE int removeLevel(const QString &id);
    // Forgets the newest Learned Correction; returns the undo token, or -1.
    Q_INVOKABLE int undoLastCorrection();
    // Puts back what the removal with this token took, unless an entry by
    // its name was added since. Only the newest removal's token works, once:
    // the window's one toast offers only its Undo.
    Q_INVOKABLE void undo(int token);
    // Merge the terms of a CSV file, or add the snippets of a JSON file that
    // the list does not hold yet. Returns { added, error }: how many are new,
    // and why the file could not be read or parsed, empty when it could.
    Q_INVOKABLE QVariantMap importTerms(const QUrl &file);
    Q_INVOKABLE QVariantMap importReplacements(const QUrl &file);

signals:
    void listsChanged();

private:
    // Saves what edit makes of the current settings, when it returns true.
    bool save(const std::function<bool(AppSettings &)> &edit);
    // Saves what edit makes when it refuses nothing, and names the refusal.
    QString saveUnlessRefused(const std::function<EditRefusal(AppSettings &)> &edit);
    int keepUndo(const SettingsEdit &undo);
    void refresh();

    ApplicationController *m_controller;
    // The newest removal's undo, whose token is m_lastUndo.
    SettingsEdit m_undo;
    int m_lastUndo = -1;
    QVariantList m_profiles;
    QVariantList m_customTones;
    QVariantList m_customCleanupLevels;
    QVariantList m_terms;
    QVariantList m_corrections;
    QVariantList m_replacements;
    QVariantList m_recognitionRules;
    bool m_speechTakesKeyTerms = false;
    int m_keyTermsSent = 0;
};

} // namespace speecher
