#include "common/test_suites.h"
#include "core/Vocabulary.h"
#include "core/VocabularyLimit.h"
#include "core/SettingsStore.h"

#include <QSignalSpy>

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
        settings.setCustomVocabulary(tooManyTerms);
        QCOMPARE(settings.customVocabulary().size(), VocabularyLimit::maxKeyterms);
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
        settings.setVocabularyEntries(many);
        QCOMPARE(settings.vocabularyEntries().size(), 183);
        const QStringList sent = settings.customVocabulary();
        QCOMPARE(sent.size(), VocabularyLimit::maxKeyterms);
        QCOMPARE(sent.first(), QStringLiteral("zzz starred"));

        QCOMPARE(VocabularyLimit::summary(vocabularyTermsOf(settings.vocabularyEntries()), QStringLiteral("claude")),
                 QStringLiteral("183 terms. 100 are key terms, and all are used for refinement."));
        // Only key terms go to the speech service, and one is said in the singular.
        QCOMPARE(VocabularyLimit::summary({QStringLiteral("Speecher"), QStringLiteral("KWin")},
                                          {QStringLiteral("KWin")}, QStringLiteral("claude")),
                 QStringLiteral("2 terms. 1 is a key term, and all are used for refinement."));
        QCOMPARE(VocabularyLimit::summary({QStringLiteral("Speecher"), QStringLiteral("KWin")}, QStringLiteral("claude")),
                 QStringLiteral("2 of 100 key terms"));
        QCOMPARE(VocabularyLimit::summary({QStringLiteral("Speecher"), QStringLiteral("KWin")}, QStringLiteral("codex")),
                 QStringLiteral("2 terms, all are used for refinement"));
        QStringList tooMany;
        for (int index = 0; index < 1001; ++index) {
            tooMany << QStringLiteral("term%1").arg(index);
        }
        QCOMPARE(VocabularyLimit::summary(tooMany, QStringLiteral("claude")),
                 QStringLiteral("1001 terms. 100 are key terms, and the first 1000 are used for refinement."));
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
        QCOMPARE(VocabularyLimit::summary(terms, QStringLiteral("claude")),
                 QStringLiteral("91 terms. 79 are key terms, and all are used for refinement."));
    }

    void learnedCorrectionsRespectTheSendCap()
    {
        SettingsStore settings;
        settings.raw().clear();
        QStringList terms;
        for (int i = 0; i < VocabularyLimit::maxKeyterms; ++i) {
            terms << QStringLiteral("term%1").arg(i);
        }
        settings.setCustomVocabulary(terms);
        settings.setLearnedCorrections({{QStringLiteral("one"), QStringLiteral("githab"),
                                         QStringLiteral("GitHub"), QStringLiteral("editor"),
                                         100, 0.8, true, 1, 100}});

        // At the cap, the correction must not push the request over it; the
        // person's own terms win.
        const QStringList sent = settings.snapshot().speech.vocabulary;
        QCOMPARE(sent.size(), VocabularyLimit::maxKeyterms);
        QVERIFY(!sent.contains(QStringLiteral("GitHub")));

        // Under the cap the correction rides along.
        settings.setCustomVocabulary({QStringLiteral("Speecher")});
        QVERIFY(settings.snapshot().speech.vocabulary.contains(QStringLiteral("GitHub")));

        // The token cap holds too: fewer than 100 terms but exactly 500
        // tokens, so the correction is what would push the request over.
        QStringList wordyTerms;
        for (int i = 0; i < 50; ++i) {
            wordyTerms << QStringLiteral("term%1 two three four five six seven eight nine ten").arg(i);
        }
        QCOMPARE(VocabularyLimit::tokenCount(wordyTerms), VocabularyLimit::maxTokens);
        settings.setCustomVocabulary(wordyTerms);
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
        settings.setVocabularyEntries({{QStringLiteral("KWin"), QStringLiteral("csv"), false, 3, 10}, limited});
        QSignalSpy added(&settings, &SettingsStore::vocabularyAdded);

        // A term limited to some Writing Profiles is already listed.
        QCOMPARE(settings.addVocabularyTerms({QStringLiteral("kwin"), QStringLiteral("  File   Session "),
                                              QStringLiteral("file session"), QStringLiteral("PNPM")}),
                 QStringList({QStringLiteral("kwin"), QStringLiteral("file session"), QStringLiteral("PNPM")}));
        QCOMPARE(added.count(), 1);
        const QList<VocabularyEntry> entries = settings.vocabularyEntries();
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

    void vocabularyMetadataPersistsImportsDeduplicatesAndTracksUsage()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setVocabularyEntries({
            {QStringLiteral("KWin"), QStringLiteral("manual"), true, 2, 10},
            {QStringLiteral("kwin"), QStringLiteral("csv"), false, 7, 20},
            {QStringLiteral("Wayland"), QStringLiteral("manual"), false, 0, 0},
        });

        QList<VocabularyEntry> entries = settings.vocabularyEntries();
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.first().term, QStringLiteral("KWin"));
        QVERIFY(entries.first().starred);
        QCOMPARE(entries.first().frequency, 7);
        QCOMPARE(entries.first().lastUsedMs, 20);

        settings.recordVocabularyUsage(QStringLiteral("KWin works on Wayland."));
        entries = settings.vocabularyEntries();
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
        settings.setWritingProfileSettings(profiles);
        VocabularyEntry kubernetes{QStringLiteral("Kubernetes")};
        kubernetes.context = QStringLiteral("  The container platform.\n");
        kubernetes.profiles = {QStringLiteral("work"), QStringLiteral("custom_standup")};
        // As an imported row naming the term would arrive.
        VocabularyEntry lowercase{QStringLiteral("kubernetes")};
        VocabularyEntry grafana{QStringLiteral("Grafana")};
        grafana.profiles = {QStringLiteral("custom_standup")};
        settings.setVocabularyEntries({kubernetes, lowercase, grafana});

        QList<VocabularyEntry> entries = settings.vocabularyEntries();
        QCOMPARE(vocabularyTermsOf(entries), (QStringList{QStringLiteral("Grafana"), QStringLiteral("Kubernetes")}));
        QCOMPARE(entries.at(1).context, QStringLiteral("The container platform."));
        QCOMPARE(entries.at(1).profiles, (QStringList{QStringLiteral("work"), QStringLiteral("custom_standup")}));

        // Every term saved before Key term existed is one; one turned off
        // stays off, even when an import names it again.
        QVERIFY(entries.at(0).keyTerm);
        entries[0].keyTerm = false;
        settings.setVocabularyEntries(entries + QList<VocabularyEntry>{{QStringLiteral("grafana")}});
        QVERIFY(!settings.vocabularyEntries().at(0).keyTerm);

        settings.setWritingProfileSettings(defaultWritingProfileSettings());
        entries = settings.vocabularyEntries();
        QCOMPARE(entries.at(0).profiles, QStringList());
        // Recreated under the same id, the profile starts with no terms.
        settings.setWritingProfileSettings(profiles);
        entries = settings.vocabularyEntries();
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
        settings.setVocabularyEntries({
            {QStringLiteral("cat"), QStringLiteral("manual"), false, 0, 0},
            {QStringLiteral("go"), QStringLiteral("manual"), false, 0, 0},
            {QStringLiteral("New York"), QStringLiteral("manual"), false, 0, 0},
            {QStringLiteral("東京"), QStringLiteral("manual"), false, 0, 0},
        });

        settings.recordVocabularyUsage(QStringLiteral("Education is ongoing near 東京駅 and New\nYork."));
        const QList<VocabularyEntry> entries = settings.vocabularyEntries();
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
        const QList<VocabularyEntry> updated = settings.vocabularyEntries();
        const auto updatedFrequency = [&updated](const QString &term) {
            const auto entry = std::find_if(updated.cbegin(), updated.cend(), [&term](const auto &candidate) {
                return candidate.term == term;
            });
            return entry == updated.cend() ? -1 : entry->frequency;
        };
        QCOMPARE(updatedFrequency(QStringLiteral("cat")), 1);
        QCOMPARE(updatedFrequency(QStringLiteral("go")), 1);
    }
};

int runVocabularyTests(int argc, char **argv)
{
    VocabularyTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_vocabulary.moc"
