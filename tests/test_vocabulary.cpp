#include "common/test_suites.h"
#include "common/vocabulary_entries.h"
#include "core/Vocabulary.h"
#include "core/VocabularyLimit.h"
#include "core/AppEdits.h"
#include "core/SettingsStore.h"
#include "core/VocabularyEdits.h"
#include "core/VocabularyScope.h"
#include "core/settings/SettingsKeys.h"
#include "core/settings/VocabularyMigration.h"
#include "core/settings/VocabularySettingsCodec.h"

#include <QFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace speecher;

namespace {

QStringList vocabularyTermsOf(const QList<VocabularyEntry> &entries)
{
    QStringList terms;
    for (const VocabularyEntry &entry : entries) {
        terms.append(entry.term);
    }
    return terms;
}

// Terms as someone types them in: manual key terms.
QList<VocabularyEntry> manualTerms(const QStringList &terms)
{
    QList<VocabularyEntry> entries;
    for (const QString &term : terms) {
        entries.append({term});
    }
    return entries;
}

} // namespace

class VocabularyTests : public QObject {
    Q_OBJECT

private slots:
    void vocabularyLimits()
    {
        QCOMPARE(VocabularyLimit::tokenCount(QStringLiteral("Deepgram Nova 3")), 3);
        QCOMPARE(VocabularyLimit::tokenCount(QStringList{QStringLiteral("Deepgram Nova 3"), QStringLiteral("API")}), 4);

        QStringList tooManyTerms;
        for (int i = 0; i < 105; ++i) {
            tooManyTerms << QStringLiteral("term%1").arg(i);
        }
        QCOMPARE(VocabularyLimit::limited(tooManyTerms).size(), VocabularyLimit::maxKeyterms);

        QStringList tooManyTokens;
        for (int i = 0; i < 101; ++i) {
            tooManyTokens << QStringLiteral("alpha%1 beta gamma delta epsilon").arg(i);
        }
        const QStringList limitedTokens = VocabularyLimit::limited(tooManyTokens);
        QCOMPARE(VocabularyLimit::tokenCount(limitedTokens), VocabularyLimit::maxTokens);
        QCOMPARE(limitedTokens.size(), VocabularyLimit::maxKeyterms);

        QStringList phrases;
        for (int i = 0; i < 90; ++i) {
            phrases << QStringLiteral("two token%1").arg(i);
        }
        phrases << QStringLiteral("this term has far too many tokens to fit inside the remaining keyterm budget");
        QVERIFY(VocabularyLimit::tokenCount(VocabularyLimit::limited(phrases)) <= VocabularyLimit::maxTokens);

        SettingsStore settings;
        settings.raw().clear();
        settings.edit([&](AppSettings &draft) { draft.vocabulary = manualTerms(tooManyTerms); return true; });
        QCOMPARE(settings.dictationSnapshot().speech.vocabulary.size(), VocabularyLimit::maxKeyterms);
    }

    void everyTermIsKeptWhileOnlyTheSentSubsetIsCapped()
    {
        QList<VocabularyEntry> many;
        for (int index = 0; index < 182; ++index) {
            many.append({QStringLiteral("term%1").arg(index), QStringLiteral("manual"), false, 0, 0});
        }
        // One late, starred term proves priority decides the sent subset.
        many.append({QStringLiteral("zzz starred"), QStringLiteral("manual"), true, 0, 0});

        const QList<VocabularyEntry> normalized = normalizeVocabularyEntries(many);
        QCOMPARE(normalized.size(), 183);
        QCOMPARE(normalized.first().term, QStringLiteral("zzz starred"));

        SettingsStore settings;
        settings.raw().clear();
        settings.edit([&](AppSettings &draft) { draft.vocabulary = many; return true; });
        QCOMPARE(settings.dictationSnapshot().vocabulary.size(), 183);
        const QStringList sent = settings.dictationSnapshot().speech.vocabulary;
        QCOMPARE(sent.size(), VocabularyLimit::maxKeyterms);
        QCOMPARE(sent.first(), QStringLiteral("zzz starred"));

    }

    void claudeKeyTermsAreWhatItsHeaderCarries()
    {
        // 東京 is six bytes of UTF-8, and 90 twelve-byte terms after it fill
        // 1176 bytes, past the header's 1024: Claude gets 東京 and the first
        // 78, a custom endpoint all 91.
        QStringList terms{QString::fromUtf8("東京")};
        for (int index = 0; index < 90; ++index) {
            terms << QStringLiteral("project%1").arg(index, 5, 10, QLatin1Char('0'));
        }
        const QStringList claude = VocabularyLimit::speechKeyterms(terms, QStringLiteral("claude"));
        QCOMPARE(claude.size(), 79);
        QCOMPARE(claude.first(), QString::fromUtf8("東京"));
        QCOMPARE(VocabularyLimit::speechKeyterms(terms, QStringLiteral("endpoint")).size(), 91);
        QCOMPARE(VocabularyLimit::speechKeyterms(terms, QStringLiteral("codex")), QStringList());
    }

    void learnedCorrectionsRespectTheSendCap()
    {
        SettingsStore settings;
        settings.raw().clear();
        QStringList terms;
        for (int i = 0; i < VocabularyLimit::maxKeyterms; ++i) {
            terms << QStringLiteral("term%1").arg(i);
        }
        settings.edit([&](AppSettings &draft) { draft.vocabulary = manualTerms(terms); return true; });
        settings.edit([](AppSettings &draft) {
            draft.vocabulary.append(test::learnedEntry(QStringLiteral("githab"), QStringLiteral("GitHub"),
                                                       QStringLiteral("editor")));
            return true;
        });

        // At the cap, the correction must not push the request over it; the
        // person's own terms win.
        const QStringList sent = settings.snapshot().speech.vocabulary;
        QCOMPARE(sent.size(), VocabularyLimit::maxKeyterms);
        QVERIFY(!sent.contains(QStringLiteral("GitHub")));

        // Under the cap the correction rides along.
        settings.edit([](AppSettings &draft) {
            draft.vocabulary = {{QStringLiteral("Speecher")}, test::learnedEntry(QStringLiteral("githab"),
                                                                                 QStringLiteral("GitHub"))};
            return true;
        });
        QVERIFY(settings.snapshot().speech.vocabulary.contains(QStringLiteral("GitHub")));

        // The token cap holds too: fewer than 100 terms but exactly 500
        // tokens, so the correction is what would push the request over.
        QStringList wordyTerms;
        for (int i = 0; i < 50; ++i) {
            wordyTerms << QStringLiteral("term%1 two three four five six seven eight nine ten").arg(i);
        }
        QCOMPARE(VocabularyLimit::tokenCount(wordyTerms), VocabularyLimit::maxTokens);
        settings.edit([&](AppSettings &draft) { draft.vocabulary = manualTerms(wordyTerms); return true; });
        const QStringList wordySent = settings.snapshot().speech.vocabulary;
        QVERIFY(VocabularyLimit::tokenCount(wordySent) <= VocabularyLimit::maxTokens);
        QVERIFY(!wordySent.contains(QStringLiteral("GitHub")));
    }

    void importKeepsEveryRowInTheFile()
    {
        QByteArray csv = QByteArrayLiteral("term\n");
        for (int index = 0; index < 150; ++index) {
            csv += QStringLiteral("imported%1\n").arg(index).toUtf8();
        }
        QString error;
        const QList<VocabularyEntry> imported = parseVocabularyCsv(csv, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(imported.size(), 150);
    }

    void vocabularyFileHoldsOneTermPerLine()
    {
        QCOMPARE(parseVocabularyFile("readSharedChoice\n\n# a comment\n  Speecher   CLI  \r\n"),
                 QStringList({QStringLiteral("readSharedChoice"), QStringLiteral("Speecher CLI")}));
#ifdef Q_OS_LINUX
        // It opens but cannot be read.
        QCOMPARE(readVocabularyFile(QStringLiteral("/proc/self/mem")), std::nullopt);
#endif
    }

    void addingTermsSkipsTheOnesAlreadyListed()
    {
        SettingsStore settings;
        settings.raw().clear();
        VocabularyEntry limited{QStringLiteral("pnpm")};
        limited.profiles = {QStringLiteral("work")};
        settings.edit([&](AppSettings &draft) {
            draft.vocabulary = {{QStringLiteral("KWin"), QStringLiteral("csv"), false, 3, 10}, limited};
            return true;
        });
        QSignalSpy added(&settings, &SettingsStore::vocabularyAdded);

        // A term limited to some Writing Profiles is already listed.
        QCOMPARE(settings.addVocabularyTerms({QStringLiteral("kwin"), QStringLiteral("  File   Session "),
                                              QStringLiteral("file session"), QStringLiteral("PNPM")}),
                 QStringList({QStringLiteral("kwin"), QStringLiteral("file session"), QStringLiteral("PNPM")}));
        QCOMPARE(added.count(), 1);
        const QList<VocabularyEntry> entries = settings.dictationSnapshot().vocabulary;
        QCOMPARE(vocabularyTermsOf(entries),
                 QStringList({QStringLiteral("KWin"), QStringLiteral("File Session"), QStringLiteral("pnpm")}));
        QCOMPARE(entries.first().source, QStringLiteral("csv"));
        QCOMPARE(entries.first().frequency, 3);
        QCOMPARE(entries.at(1).source, QStringLiteral("manual"));
        QVERIFY(entries.at(1).keyTerm);
        QCOMPARE(entries.last().profiles, limited.profiles);

        QCOMPARE(settings.addVocabularyTerms({QStringLiteral("KWIN")}), QStringList({QStringLiteral("KWIN")}));
        QCOMPARE(added.count(), 1);
    }

    // A failed save leaves the vocabulary as it was and does not fail the next add.
    void addingTermsAfterAFailedSave()
    {
#ifdef Q_OS_WIN
        QSKIP("The registry has no file to make read-only.");
#endif
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) { draft.vocabulary = {{QStringLiteral("KWin")}}; return true; });
        settings.raw().sync();
        QSignalSpy added(&settings, &SettingsStore::vocabularyAdded);
        QFile file(settings.raw().fileName());
        const QFile::Permissions permissions = file.permissions();
        const auto restorePermissions = qScopeGuard([&file, permissions] { file.setPermissions(permissions); });
        QVERIFY(file.setPermissions(QFile::ReadOwner));

        QCOMPARE(settings.addVocabularyTerms({QStringLiteral("pnpm")}), std::nullopt);
        QCOMPARE(vocabularyTermsOf(settings.dictationSnapshot().vocabulary), QStringList({QStringLiteral("KWin")}));
        QCOMPARE(added.count(), 0);

        QVERIFY(file.setPermissions(permissions));
        QCOMPARE(settings.addVocabularyTerms({QStringLiteral("pnpm")}), QStringList());
        QCOMPARE(vocabularyTermsOf(settings.dictationSnapshot().vocabulary),
                 QStringList({QStringLiteral("KWin"), QStringLiteral("pnpm")}));
        QCOMPARE(added.count(), 1);
    }

    void vocabularyMetadataPersistsImportsDeduplicatesAndTracksUsage()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) {
            draft.vocabulary = {
                {QStringLiteral("KWin"), QStringLiteral("manual"), true, 2, 10},
                {QStringLiteral("kwin"), QStringLiteral("csv"), false, 7, 20},
                {QStringLiteral("Wayland"), QStringLiteral("manual"), false, 0, 0},
            };
            return true;
        });

        QList<VocabularyEntry> entries = settings.dictationSnapshot().vocabulary;
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.first().term, QStringLiteral("KWin"));
        QVERIFY(entries.first().starred);
        QCOMPARE(entries.first().frequency, 7);
        QCOMPARE(entries.first().lastUsedMs, 20);

        settings.recordVocabularyUsage(QStringLiteral("KWin works on Wayland."));
        entries = settings.dictationSnapshot().vocabulary;
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.at(0).frequency, 8);
        QCOMPARE(entries.at(1).frequency, 1);
        QVERIFY(entries.at(0).lastUsedMs > 20);

        QString error;
        const QList<VocabularyEntry> imported = parseVocabularyCsv(
            QByteArrayLiteral("term,source,starred,frequency,last_used_ms\n"
                              "\"Nova, Three\",research,yes,4,123\n"
                              "Plasma,csv,no,2,99\n"),
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(imported.size(), 2);
        QCOMPARE(imported.first().term, QStringLiteral("Nova, Three"));
        QVERIFY(imported.first().starred);
        QCOMPARE(imported.first().source, QStringLiteral("research"));
        QCOMPARE(imported.first().frequency, 4);
    }

    // A term keeps its context and profiles, a second spelling of it cannot
    // lift its limit, and a deleted profile leaves every term it limited.
    void contextAndProfilesPersistMergeAndImport()
    {
        SettingsStore settings;
        settings.raw().clear();
        QList<WritingProfileSettings> profiles = defaultWritingProfileSettings();
        profiles.append({QStringLiteral("custom_standup"), QStringLiteral("balanced"), QStringLiteral("none"),
                         QString(), QStringLiteral("Standup")});
        settings.edit([&](AppSettings &draft) { draft.refinement.writingProfiles = profiles; return true; });
        VocabularyEntry kubernetes{QStringLiteral("Kubernetes")};
        kubernetes.context = QStringLiteral("  The container platform.\n");
        kubernetes.profiles = {QStringLiteral("work"), QStringLiteral("custom_standup")};
        // As an imported row naming the term would arrive.
        VocabularyEntry lowercase{QStringLiteral("kubernetes")};
        VocabularyEntry grafana{QStringLiteral("Grafana")};
        grafana.profiles = {QStringLiteral("custom_standup")};
        settings.edit([&](AppSettings &draft) { draft.vocabulary = {kubernetes, lowercase, grafana}; return true; });

        QList<VocabularyEntry> entries = settings.dictationSnapshot().vocabulary;
        QCOMPARE(vocabularyTermsOf(entries), (QStringList{QStringLiteral("Grafana"), QStringLiteral("Kubernetes")}));
        QCOMPARE(entries.at(1).context, QStringLiteral("The container platform."));
        QCOMPARE(entries.at(1).profiles, (QStringList{QStringLiteral("work"), QStringLiteral("custom_standup")}));

        // Every term saved before Key term existed is one; one turned off
        // stays off, even when an import names it again.
        QVERIFY(entries.at(0).keyTerm);
        entries[0].keyTerm = false;
        settings.edit([&](AppSettings &draft) {
            draft.vocabulary = entries + QList<VocabularyEntry>{{QStringLiteral("grafana")}};
            return true;
        });
        QVERIFY(!settings.dictationSnapshot().vocabulary.at(0).keyTerm);

        settings.edit([](AppSettings &draft) {
            draft.refinement.writingProfiles = defaultWritingProfileSettings();
            return true;
        });
        entries = settings.dictationSnapshot().vocabulary;
        QCOMPARE(entries.at(0).profiles, QStringList());
        // Recreated under the same id, the profile starts with no terms.
        settings.edit([&](AppSettings &draft) { draft.refinement.writingProfiles = profiles; return true; });
        entries = settings.dictationSnapshot().vocabulary;
        QCOMPARE(entries.at(0).profiles, QStringList());
        QCOMPARE(entries.at(1).profiles, QStringList{QStringLiteral("work")});

        QString error;
        const QList<VocabularyEntry> imported = parseVocabularyCsv(
            QByteArrayLiteral("term,context\n"
                              "Sev1,\"Incident severity, in on-call chats.\"\n"),
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(imported.size(), 1);
        QCOMPARE(imported.first().context, QStringLiteral("Incident severity, in on-call chats."));
    }

    void vocabularyUsageRequiresTermBoundaries()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.edit([](AppSettings &draft) {
            draft.vocabulary = {
                {QStringLiteral("cat"), QStringLiteral("manual"), false, 0, 0},
                {QStringLiteral("go"), QStringLiteral("manual"), false, 0, 0},
                {QStringLiteral("New York"), QStringLiteral("manual"), false, 0, 0},
                {QStringLiteral("東京"), QStringLiteral("manual"), false, 0, 0},
            };
            return true;
        });

        settings.recordVocabularyUsage(QStringLiteral("Education is ongoing near 東京駅 and New\nYork."));
        const QList<VocabularyEntry> entries = settings.dictationSnapshot().vocabulary;
        const auto frequency = [&entries](const QString &term) {
            const auto entry = std::find_if(entries.cbegin(), entries.cend(), [&term](const auto &candidate) {
                return candidate.term == term;
            });
            return entry == entries.cend() ? -1 : entry->frequency;
        };

        QCOMPARE(frequency(QStringLiteral("cat")), 0);
        QCOMPARE(frequency(QStringLiteral("go")), 0);
        QCOMPARE(frequency(QStringLiteral("New York")), 1);
        QCOMPARE(frequency(QStringLiteral("東京")), 1);

        settings.recordVocabularyUsage(QStringLiteral("A cat, ready to go!"));
        const QList<VocabularyEntry> updated = settings.dictationSnapshot().vocabulary;
        const auto updatedFrequency = [&updated](const QString &term) {
            const auto entry = std::find_if(updated.cbegin(), updated.cend(), [&term](const auto &candidate) {
                return candidate.term == term;
            });
            return entry == updated.cend() ? -1 : entry->frequency;
        };
        QCOMPARE(updatedFrequency(QStringLiteral("cat")), 1);
        QCOMPARE(updatedFrequency(QStringLiteral("go")), 1);
    }

    // The one scope test, profile by app: an entry limited to profiles, to
    // apps (an App's id or a correction's application id) or to both applies
    // only where every limit holds, and a request made before the profile is
    // known takes every entry that is on.
    void anEntryAppliesWhereItsLimitsHold_data()
    {
        QTest::addColumn<QStringList>("profiles");
        QTest::addColumn<QStringList>("apps");
        QTest::addColumn<bool>("enabled");
        // Where it applies, of: Work in Kate, Personal in Kate, Work in
        // Firefox, Work in a Gmail tab, and before the profile is known.
        QTest::addColumn<QList<bool>>("applies");

        const QString kate = QStringLiteral("org.kde.kate");
        QTest::newRow("anywhere") << QStringList{} << QStringList{} << true << QList<bool>{true, true, true, true, true};
        QTest::newRow("profile") << QStringList{WritingProfile::Work} << QStringList{} << true
                                 << QList<bool>{true, false, true, true, true};
        QTest::newRow("application id") << QStringList{} << QStringList{QStringLiteral("ORG.KDE.KATE")} << true
                                        << QList<bool>{true, true, false, false, true};
        QTest::newRow("App by id") << QStringList{} << QStringList{QStringLiteral("app:kate")} << true
                                   << QList<bool>{true, true, false, false, true};
        QTest::newRow("App by title") << QStringList{} << QStringList{QStringLiteral("app:gmail")} << true
                                      << QList<bool>{false, false, false, true, true};
        QTest::newRow("either app") << QStringList{} << QStringList{QStringLiteral("app:gmail"), kate} << true
                                    << QList<bool>{true, true, false, true, true};
        QTest::newRow("profile and app") << QStringList{WritingProfile::Work} << QStringList{kate} << true
                                         << QList<bool>{true, false, false, false, true};
        QTest::newRow("removed App") << QStringList{} << QStringList{QStringLiteral("app:gone")} << true
                                     << QList<bool>{false, false, false, false, true};
        QTest::newRow("off") << QStringList{} << QStringList{} << false << QList<bool>{false, false, false, false, false};
    }

    void anEntryAppliesWhereItsLimitsHold()
    {
        QFETCH(QStringList, profiles);
        QFETCH(QStringList, apps);
        QFETCH(bool, enabled);
        QFETCH(QList<bool>, applies);

        AppsSettings settings;
        settings.apps = {
            {QStringLiteral("app:kate"), QStringLiteral("Kate"), {}, {{AppMatchKind::Id, QStringLiteral("org.kde.kate.desktop")}}},
            {QStringLiteral("app:gmail"), QStringLiteral("Gmail"), {}, {{AppMatchKind::WindowTitle, QStringLiteral("Gmail")}}},
        };
        Target kate;
        kate.applicationId = QStringLiteral("org.kde.kate");
        Target firefox;
        firefox.applicationId = QStringLiteral("org.mozilla.firefox");
        Target gmail = firefox;
        gmail.windowTitle = QStringLiteral("Inbox - Gmail - Firefox");
        const QList<VocabularyScope> scopes{
            {WritingProfile::Work, kate, settings},
            {WritingProfile::Personal, kate, settings},
            {WritingProfile::Work, firefox, settings},
            {WritingProfile::Work, gmail, settings},
            {QString(), firefox, settings},
        };

        VocabularyEntry entry{QStringLiteral("Kubernetes")};
        entry.profiles = profiles;
        entry.apps = apps;
        entry.enabled = enabled;
        QList<bool> actual;
        for (const VocabularyScope &scope : scopes) {
            actual.append(vocabularyEntryApplies(entry, scope));
        }
        QCOMPARE(actual, applies);
    }

    // Of two replacements for one phrase, the person's own wins over a
    // Learned Correction, and the one limited to the app over the one limited
    // to the profile over the one for anywhere.
    void theNarrowerReplacementWins()
    {
        Target slack;
        slack.applicationId = QStringLiteral("com.slack.Slack");
        const VocabularyScope inSlack{WritingProfile::Work, slack, {}};
        VocabularyEntry anywhere = test::replacementEntry(QStringLiteral("sig"), QStringLiteral("Best"));
        VocabularyEntry forWork = test::replacementEntry(QStringLiteral("sig"), QStringLiteral("Regards"));
        forWork.profiles = {WritingProfile::Work};
        VocabularyEntry forSlack = test::replacementEntry(QStringLiteral("sig"), QStringLiteral("cheers"));
        forSlack.apps = {QStringLiteral("com.slack.Slack")};
        const VocabularyEntry learned = test::learnedEntry(QStringLiteral("SIG"), QStringLiteral("SIG"),
                                                           QStringLiteral("com.slack.Slack"));
        const QDateTime now = QDateTime::currentDateTime();

        QCOMPARE(replacementRules({learned, anywhere, forWork, forSlack}, inSlack, now),
                 (QList<BindingRule>{{QStringLiteral("sig"), QStringLiteral("cheers")}}));
        QCOMPARE(replacementRules({learned, anywhere, forWork}, inSlack, now),
                 (QList<BindingRule>{{QStringLiteral("sig"), QStringLiteral("Regards")}}));
        QCOMPARE(replacementRules({learned, anywhere}, {WritingProfile::Personal, slack, {}}, now),
                 (QList<BindingRule>{{QStringLiteral("sig"), QStringLiteral("Best")}}));
        QCOMPARE(replacementRules({learned}, inSlack, now), (QList<BindingRule>{{QStringLiteral("SIG"), QStringLiteral("SIG")}}));
    }

    // Each old list moves into the one Vocabulary once, as the release before
    // loaded it, and the old keys stay for a downgrade.
    void migrationMovesEachOldList()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        settings.setValue(SettingsKeys::VocabularyEntries,
                          QByteArray(R"([{"term":"Kubernetes","source":"manual","starred":true,"frequency":4,)"
                                     R"("lastUsedMs":1000,"context":"The platform.","profiles":["work"]},)"
                                     R"({"term":"Lúcia","source":"csv","keyTerm":false}])"));
        settings.setValue(SettingsKeys::BindingRules,
                          QByteArray(R"([{"phrase":"my email","replacement":"efox@example.com"},)"
                                     R"({"phrase":"MY, email","replacement":"a duplicate"},)"
                                     R"({"phrase":"...","replacement":"no words"}])"));
        settings.setValue(SettingsKeys::LearnedCorrections,
                          QByteArray(R"([{"id":"c1","original":"cute","corrected":"Qt","applicationId":"org.kde.kate",)"
                                     R"("createdAtMs":10,"confidence":0.75,"enabled":false,"evidenceCount":3,"lastObservedAtMs":20},)"
                                     R"({"id":"c2","original":"open ai","corrected":"OpenAI","applicationId":"",)"
                                     R"("createdAtMs":30,"confidence":0.98},)"
                                     R"({"id":"","original":"no id","corrected":"dropped"}])"));

        migrateVocabulary(settings);

        const QList<VocabularyEntry> migrated = VocabularySettingsCodec::load(settings);
        QCOMPARE(migrated.size(), 5);
        VocabularyEntry kubernetes{QStringLiteral("Kubernetes"), QStringLiteral("manual"), true, 4, 1000,
                                   QStringLiteral("The platform."), {QStringLiteral("work")}};
        kubernetes.id = migrated[0].id;
        QCOMPARE(migrated[0], kubernetes);
        QCOMPARE(migrated[1].term, QStringLiteral("Lúcia"));
        QCOMPARE(migrated[1].source, QStringLiteral("csv"));
        QVERIFY(!migrated[1].keyTerm);
        VocabularyEntry email = test::replacementEntry(QStringLiteral("my email"), QStringLiteral("efox@example.com"));
        email.id = migrated[2].id;
        QCOMPARE(migrated[2], email);
        VocabularyEntry cute = test::learnedEntry(QStringLiteral("cute"), QStringLiteral("Qt"), QStringLiteral("org.kde.kate"),
                                                  QStringLiteral("c1"));
        cute.confidence = 0.75;
        cute.enabled = false;
        cute.evidenceCount = 3;
        cute.createdAtMs = 10;
        cute.lastObservedAtMs = 20;
        QCOMPARE(migrated[3], cute);
        VocabularyEntry openAi = test::learnedEntry(QStringLiteral("open ai"), QStringLiteral("OpenAI"), {}, QStringLiteral("c2"));
        openAi.confidence = 0.98;
        openAi.createdAtMs = 30;
        openAi.lastObservedAtMs = 30;
        QCOMPARE(migrated[4], openAi);
        for (const VocabularyEntry &entry : migrated) {
            QVERIFY(!entry.id.isEmpty());
        }
        QVERIFY(settings.contains(SettingsKeys::VocabularyEntries));
        QVERIFY(settings.contains(SettingsKeys::BindingRules));
        QVERIFY(settings.contains(SettingsKeys::LearnedCorrections));

        // Once: a list edited since stays as it is.
        VocabularySettingsCodec::store(settings, {{QStringLiteral("Speecher")}});
        migrateVocabulary(settings);
        QCOMPARE(vocabularyTermsOf(VocabularySettingsCodec::load(settings)), QStringList{QStringLiteral("Speecher")});
    }

    void migrationReadsTheOldestTermsAndStoresNothingWhereThereWereNone()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        migrateVocabulary(settings);
        QVERIFY(!settings.contains(SettingsKeys::Vocabulary));

        QSettings older(dir.filePath(QStringLiteral("older.ini")), QSettings::IniFormat);
        older.setValue(SettingsKeys::LegacyVocabulary, QStringList{QStringLiteral("Wayland"), QStringLiteral("KWin")});
        migrateVocabulary(older);
        const QList<VocabularyEntry> migrated = VocabularySettingsCodec::load(older);
        QCOMPARE(vocabularyTermsOf(migrated), (QStringList{QStringLiteral("KWin"), QStringLiteral("Wayland")}));
        QCOMPARE(migrated.first().source, QStringLiteral("legacy"));
    }

    // Removing an App keeps a snippet limited to it limited to the ids it
    // matched, rather than writing it everywhere.
    void removingAnAppKeepsTheLimitsByItsIds()
    {
        AppSettings settings;
        settings.apps.apps = {
            {QStringLiteral("app:slack"), QStringLiteral("Slack"), {}, {{AppMatchKind::Id, QStringLiteral("com.slack.Slack")}}},
            {QStringLiteral("app:gmail"), QStringLiteral("Gmail"), {}, {{AppMatchKind::WindowTitle, QStringLiteral("Gmail")}}},
        };
        VocabularyEntry inSlack = test::replacementEntry(QStringLiteral("sig"), QStringLiteral("cheers"));
        QCOMPARE(addVocabularyEntry(settings, inSlack), EditRefusal::None);
        settings.vocabulary.first().apps = {QStringLiteral("app:slack"), QStringLiteral("app:gmail")};

        QVERIFY(removeApp(settings, QStringLiteral("app:slack")));
        QCOMPARE(settings.vocabulary.first().apps, (QStringList{QStringLiteral("app:gmail"), QStringLiteral("com.slack.Slack")}));
        QVERIFY(removeApp(settings, QStringLiteral("app:gmail")));
        QCOMPARE(settings.vocabulary.first().apps, QStringList{QStringLiteral("com.slack.Slack")});
    }
};

int runVocabularyTests(int argc, char **argv)
{
    VocabularyTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_vocabulary.moc"
