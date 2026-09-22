#include "common/test_suites.h"
#include "core/Vocabulary.h"
#include "core/VocabularyLimit.h"
#include "core/SettingsStore.h"

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

        QCOMPARE(VocabularyLimit::summary(vocabularyTermsOf(settings.vocabularyEntries())),
                 QStringLiteral("183 terms, the 100 highest priority are sent"));
        QCOMPARE(VocabularyLimit::summary({QStringLiteral("Speecher"), QStringLiteral("KWin")}),
                 QStringLiteral("2 of 100 terms, using 2 of 500 tokens"));
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
