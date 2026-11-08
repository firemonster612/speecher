// History's store and what a dictation keeps: search, deletes, retention and
// clearing through HistoryStore's interface, and historyKeep's table.

#include "common/test_suites.h"
#include "core/OwnerOnly.h"
#include "core/history/HistoryAudioWriter.h"
#include "core/history/HistoryPolicy.h"
#include "core/history/HistoryStore.h"

#include <QAudioDecoder>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace speecher;

Q_DECLARE_METATYPE(std::optional<speecher::HistoryKeep>)

namespace {

const QDateTime kNow(QDate(2026, 11, 8), QTime(12, 0));

HistoryEntry dictation(const QString &text, const QString &app = QStringLiteral("Slack"), int minutesAgo = 0,
                       HistoryKeep kept = HistoryKeep::Text)
{
    HistoryEntry entry;
    entry.stats.finishedAt = kNow.addSecs(-60 * minutesAgo);
    entry.stats.audioMs = 4000;
    entry.stats.words = countWords(text);
    entry.stats.appName = app;
    entry.stats.applicationId = app.toLower();
    entry.stats.profile = WritingProfile::Work;
    entry.text = text;
    entry.rawText = text;
    entry.kept = kept;
    return entry;
}

QStringList textsOf(const QList<HistoryHit> &hits)
{
    QStringList texts;
    for (const HistoryHit &hit : hits) {
        texts.append(hit.entry.text.value_or(QString()));
    }
    return texts;
}

HistoryQuery searchFor(const QString &text)
{
    HistoryQuery query;
    query.text = text;
    return query;
}

} // namespace

class HistoryStoreTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString databasePath(const QString &name = QStringLiteral("history.sqlite3")) const
    {
        return m_dir.filePath(name);
    }

private slots:
    void cleanup()
    {
        qunsetenv("SPEECHER_HISTORY_WITHOUT_TRIGRAM");
    }

    // The schema is made once; what was added is there after a reopen, in a
    // file only its owner can read.
    void entriesOutliveTheStore()
    {
        const QString path = databasePath(QStringLiteral("reopen.sqlite3"));
        {
            HistoryStore store(path, m_dir.filePath(QStringLiteral("audio")));
            QVERIFY(store.isOpen());
            QVERIFY(store.searchesByTrigram());
            QVERIFY(store.add(dictation(QStringLiteral("hello there"))) > 0);
        }
#ifdef Q_OS_UNIX
        QVERIFY(isOwnerOnly(path));
#endif
        HistoryStore reopened(path, QString());
        const QList<HistoryHit> hits = reopened.search({});
        QCOMPARE(hits.size(), 1);
        const HistoryEntry &entry = hits.first().entry;
        QCOMPARE(entry.text, std::optional<QString>(QStringLiteral("hello there")));
        QCOMPARE(entry.stats.finishedAt, kNow);
        QCOMPARE(entry.stats.appName, QStringLiteral("Slack"));
        QCOMPARE(entry.stats.applicationId, QStringLiteral("slack"));
        QCOMPARE(entry.stats.profile, WritingProfile::Work);
        QCOMPARE(entry.kept, HistoryKeep::Text);
    }

    // An entry never holds more than it was allowed to keep.
    void anEntryKeepsOnlyWhatItsLevelAllows()
    {
        HistoryStore store(QStringLiteral(":memory:"), QString());
        HistoryEntry stats = dictation(QStringLiteral("a secret"), QStringLiteral("KeePassXC"), 0, HistoryKeep::Stats);
        stats.audioPath = QStringLiteral("1.m4a");
        const qint64 id = store.add(stats);
        const std::optional<HistoryEntry> kept = store.entry(id);
        QVERIFY(kept);
        QVERIFY(!kept->text);
        QVERIFY(!kept->rawText);
        QVERIFY(kept->stats.applicationId.isEmpty());
        QVERIFY(kept->audioPath.isEmpty());
        QCOMPARE(kept->stats.appName, QStringLiteral("KeePassXC"));
        // Stats only: counted on Home, never listed.
        QCOMPARE(store.records().size(), 1);
        QVERIFY(store.search({}).isEmpty());
    }

    void searchMatchesAnyPartOfTheWords_data()
    {
        QTest::addColumn<bool>("trigram");
        QTest::newRow("trigram index") << true;
        QTest::newRow("instr scan") << false;
    }

    // Any substring, either case, of the text, the app's name or the title;
    // words in any order, a quoted phrase as written.
    void searchMatchesAnyPartOfTheWords()
    {
        QFETCH(bool, trigram);
        if (!trigram) {
            qputenv("SPEECHER_HISTORY_WITHOUT_TRIGRAM", "1");
        }
        HistoryStore store(QStringLiteral(":memory:"), QString());
        QCOMPARE(store.searchesByTrigram(), trigram);
        const QString deck = QStringLiteral("Can you send me the Q3 deck before the call tomorrow?");
        const QString commit = QStringLiteral("git commit -m \"fix the retry loop in the uploader\"");
        const QString tokyo = QStringLiteral("東京駅に行きます");
        HistoryEntry file = dictation(QStringLiteral("We spoke about the budget."), QString(), 3);
        file.kind = HistoryKind::File;
        file.title = QStringLiteral("interview-03.m4a");
        store.add(dictation(deck, QStringLiteral("Slack"), 2));
        store.add(dictation(commit, QStringLiteral("Ghostty"), 1));
        store.add(dictation(tokyo, QStringLiteral("Kate"), 0));
        store.add(file);

        QCOMPARE(textsOf(store.search(searchFor(QStringLiteral("DECK")))), QStringList{deck});
        QCOMPARE(textsOf(store.search(searchFor(QStringLiteral("loop retry")))), QStringList{commit});
        QCOMPARE(textsOf(store.search(searchFor(QStringLiteral("\"retry loop\"")))), QStringList{commit});
        QVERIFY(store.search(searchFor(QStringLiteral("\"loop retry\""))).isEmpty());
        QCOMPARE(textsOf(store.search(searchFor(QStringLiteral("ghost")))), QStringList{commit});
        QCOMPARE(textsOf(store.search(searchFor(QStringLiteral("interview")))), QStringList{file.text.value()});
        QCOMPARE(textsOf(store.search(searchFor(QStringLiteral("東京駅")))), QStringList{tokyo});
        // Under three characters the trigrams can't help; it scans.
        QCOMPARE(textsOf(store.search(searchFor(QStringLiteral("q3")))), QStringList{deck});
        QCOMPARE(store.count(searchFor(QStringLiteral("the"))), 3);
        QVERIFY(store.search(searchFor(QStringLiteral("nowhere"))).isEmpty());

        const HistoryHit hit = store.search(searchFor(QStringLiteral("the deck"))).first();
        QCOMPARE(hit.highlighted,
                 QStringLiteral("Can you send me \x02the\x03 Q3 \x02" "deck\x03 before \x02the\x03 call tomorrow?"));
    }

    // Kind, app, profile and time narrow together, newest first, a page at a
    // time.
    void filtersAndPagesNarrowTheTimeline()
    {
        HistoryStore store(QStringLiteral(":memory:"), QString());
        HistoryEntry email = dictation(QStringLiteral("dear team"), QStringLiteral("Thunderbird"), 60 * 30);
        email.stats.profile = WritingProfile::Email;
        store.add(email);
        for (int i = 4; i >= 0; --i) {
            store.add(dictation(QStringLiteral("slack note %1").arg(i), QStringLiteral("Slack"), 10 * i));
        }

        HistoryQuery firstPage;
        firstPage.limit = 2;
        const QList<HistoryHit> first = store.search(firstPage);
        QCOMPARE(textsOf(first), (QStringList{QStringLiteral("slack note 0"), QStringLiteral("slack note 1")}));
        HistoryQuery nextPage = firstPage;
        nextPage.beforeId = first.last().entry.id;
        QCOMPARE(textsOf(store.search(nextPage)), (QStringList{QStringLiteral("slack note 2"), QStringLiteral("slack note 3")}));

        HistoryQuery byApp;
        byApp.app = QStringLiteral("Thunderbird");
        QCOMPARE(textsOf(store.search(byApp)), QStringList{QStringLiteral("dear team")});
        HistoryQuery byProfile;
        byProfile.profile = WritingProfile::Work;
        QCOMPARE(store.count(byProfile), 5);
        HistoryQuery lastHalfHour;
        lastHalfHour.from = kNow.addSecs(-30 * 60);
        QCOMPARE(store.count(lastHalfHour), 4);
        HistoryQuery files;
        files.kinds = {HistoryKind::File};
        QCOMPARE(store.count(files), 0);
        QCOMPARE(store.appNames(), (QStringList{QStringLiteral("Slack"), QStringLiteral("Thunderbird")}));
        QCOMPARE(store.profiles(), (QStringList{WritingProfile::Work, WritingProfile::Email}));
    }

    // A delete hides the entry until it is restored, or purged with its audio.
    void aDeleteWaitsForItsUndo()
    {
        QTemporaryDir audio;
        HistoryStore store(databasePath(QStringLiteral("undo.sqlite3")), audio.path());
        HistoryEntry entry = dictation(QStringLiteral("delete me"), QStringLiteral("Slack"), 0, HistoryKeep::Audio);
        entry.audioPath = QStringLiteral("delete-me.m4a");
        QFile file(audio.filePath(entry.audioPath));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const qint64 id = store.add(entry);
        QSignalSpy changed(&store, &HistoryStore::changed);

        QVERIFY(store.markDeleted(id));
        QVERIFY(store.search({}).isEmpty());
        QVERIFY(!store.entry(id));
        QVERIFY(store.records().isEmpty());
        QVERIFY(store.restore(id));
        QCOMPARE(store.search({}).size(), 1);
        QCOMPARE(store.records().size(), 1);
        QCOMPARE(changed.count(), 2);

        QVERIFY(store.markDeleted(id));
        store.purgeDeleted();
        QVERIFY(!store.restore(id));
        QVERIFY(!QFile::exists(file.fileName()));
    }

    // Past their days, dictations and files keep their stats, so Home's
    // numbers stay; audio goes sooner; a recording's text stays.
    void retentionLeavesTheStats()
    {
        QTemporaryDir audio;
        HistoryStore store(QStringLiteral(":memory:"), audio.path());
        const auto withAudio = [&audio](HistoryEntry entry, const QString &name) {
            entry.kept = HistoryKeep::Audio;
            entry.audioPath = name;
            QFile file(audio.filePath(name));
            [[maybe_unused]] const bool made = file.open(QIODevice::WriteOnly);
            return entry;
        };
        const int day = 24 * 60;
        HistoryEntry call = dictation(QStringLiteral("[00:00:04] me: hello"), QString(), 200 * day);
        call.kind = HistoryKind::Recording;
        store.add(call);
        store.add(withAudio(dictation(QStringLiteral("last week"), QStringLiteral("Slack"), 10 * day), QStringLiteral("week.m4a")));
        store.add(withAudio(dictation(QStringLiteral("today"), QStringLiteral("Slack"), 60), QStringLiteral("today.m4a")));
        store.add(withAudio(dictation(QStringLiteral("old")), QStringLiteral("old.m4a")));

        // 100 days later for "old", which was added at kNow.
        HistorySettings settings;
        settings.textDays = 90;
        settings.audioDays = 7;
        store.prune(kNow.addDays(1), settings);
        QCOMPARE(textsOf(store.search({})), (QStringList{QStringLiteral("old"), QStringLiteral("today"),
                                                         QStringLiteral("last week"), call.text.value()}));
        QVERIFY(QFile::exists(audio.filePath(QStringLiteral("old.m4a"))));
        QVERIFY(!QFile::exists(audio.filePath(QStringLiteral("week.m4a"))));

        store.prune(kNow.addDays(100), settings);
        QCOMPARE(textsOf(store.search({})), QStringList{call.text.value()});
        QVERIFY(!QFile::exists(audio.filePath(QStringLiteral("old.m4a"))));
        QVERIFY(!QFile::exists(audio.filePath(QStringLiteral("today.m4a"))));
        QCOMPARE(store.records().size(), 3);
    }

    // Clearing deletes the words of a range; with the stats, the entries too.
    void clearingARange()
    {
        HistoryStore store(QStringLiteral(":memory:"), QString());
        store.add(dictation(QStringLiteral("yesterday"), QStringLiteral("Slack"), 24 * 60));
        store.add(dictation(QStringLiteral("this morning"), QStringLiteral("Slack"), 3 * 60));
        store.add(dictation(QStringLiteral("just now"), QStringLiteral("Slack"), 5));
        const QDateTime lastHour = kNow.addSecs(-3600);
        QCOMPARE(store.countIn(lastHour, {}), 1);

        QVERIFY(store.clear(lastHour, {}, false));
        QCOMPARE(textsOf(store.search({})), (QStringList{QStringLiteral("this morning"), QStringLiteral("yesterday")}));
        QCOMPARE(store.records().size(), 3);

        QVERIFY(store.clear(QDateTime(kNow.date(), QTime()), {}, true));
        QCOMPARE(store.records().size(), 1);
        QVERIFY(store.clear({}, {}, true));
        QVERIFY(store.records().isEmpty());
        QCOMPARE(store.countIn({}, {}), 0);
    }

    // A file written where SQLite had no trigram tokenizer is indexed again
    // the next time one is there.
    void theIndexCatchesUpWithEntriesAddedWithoutIt()
    {
        const QString path = databasePath(QStringLiteral("catch-up.sqlite3"));
        {
            HistoryStore store(path, QString());
            store.add(dictation(QStringLiteral("indexed first")));
        }
        qputenv("SPEECHER_HISTORY_WITHOUT_TRIGRAM", "1");
        {
            HistoryStore store(path, QString());
            QVERIFY(!store.searchesByTrigram());
            store.add(dictation(QStringLiteral("added without the index")));
            QCOMPARE(store.count(searchFor(QStringLiteral("without"))), 1);
        }
        qunsetenv("SPEECHER_HISTORY_WITHOUT_TRIGRAM");
        HistoryStore store(path, QString());
        QVERIFY(store.searchesByTrigram());
        QCOMPARE(store.count(searchFor(QStringLiteral("without"))), 1);
        QCOMPARE(store.count(searchFor(QStringLiteral("index"))), 2);
    }

    // A page of results is ready within a frame, over a long history.
    void aSearchPageIsFastOverAHundredThousandEntries()
    {
        HistoryStore store(databasePath(QStringLiteral("large.sqlite3")), QString());
        const QStringList apps{QStringLiteral("Slack"), QStringLiteral("Ghostty"), QStringLiteral("Kate"),
                               QStringLiteral("Firefox")};
        QList<HistoryEntry> entries;
        entries.reserve(100000);
        for (int i = 0; i < 100000; ++i) {
            entries.append(dictation(QStringLiteral("Dictation number %1 about the %2 and the retry loop %3")
                                         .arg(i)
                                         .arg(i % 7 == 0 ? QStringLiteral("uploader") : QStringLiteral("deck"))
                                         .arg(i % 13),
                                     apps.at(i % apps.size()), i));
        }
        QVERIFY(store.addAll(entries));
        const auto fastest = [&store](const HistoryQuery &query) {
            qint64 best = std::numeric_limits<qint64>::max();
            for (int run = 0; run < 5; ++run) {
                QElapsedTimer timer;
                timer.start();
                const QList<HistoryHit> hits = store.search(query);
                best = std::min(best, timer.elapsed());
                if (hits.size() != query.limit) {
                    return std::numeric_limits<qint64>::max();
                }
            }
            return best;
        };
        QVERIFY2(fastest(searchFor(QStringLiteral("uploader"))) < 16, "a word's page took a frame or longer");
        QVERIFY2(fastest(searchFor(QStringLiteral("retry loop 12"))) < 16, "several words' page took a frame or longer");
        QVERIFY2(fastest({}) < 16, "the first page took a frame or longer");
    }

    // A second of tone goes in as it is heard and comes out a second long,
    // under its final name once the encoder is done.
    void audioRoundTrips()
    {
        QTemporaryDir folder;
        auto *writer = new HistoryAudioWriter(folder.path());
        constexpr int rate = 16000;
        for (int chunk = 0; chunk < 10; ++chunk) {
            QByteArray pcm;
            for (int i = 0; i < rate / 10; ++i) {
                const auto sample = qint16(8000 * std::sin(2 * M_PI * 440 * (chunk * rate / 10 + i) / rate));
                pcm.append(reinterpret_cast<const char *>(&sample), 2);
            }
            writer->write(pcm);
        }
        const QString name = writer->finish();
        QVERIFY(!name.isEmpty());
        const QString path = QDir(folder.path()).filePath(name);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path), 10000);
        QTRY_VERIFY(QDir(folder.path()).entryList({QStringLiteral("*.part")}, QDir::Files).isEmpty());
#ifdef Q_OS_UNIX
        QVERIFY(isOwnerOnly(folder.path()));
#endif

        QAudioDecoder decoder;
        decoder.setSource(QUrl::fromLocalFile(path));
        qint64 decodedUs = 0;
        connect(&decoder, &QAudioDecoder::bufferReady, &decoder, [&] { decodedUs += decoder.read().duration(); });
        bool done = false;
        connect(&decoder, &QAudioDecoder::finished, &decoder, [&done] { done = true; });
        connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &decoder, [&done] { done = true; });
        decoder.start();
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY2(std::abs(decodedUs / 1000 - 1000) < 150, qPrintable(QStringLiteral("decoded %1 ms").arg(decodedUs / 1000)));
    }

    // Nothing heard, nothing written; thrown away, nothing left.
    void audioThatIsntKeptLeavesNoFile()
    {
        QTemporaryDir folder;
        QVERIFY((new HistoryAudioWriter(folder.path()))->finish().isEmpty());
        auto *discarded = new HistoryAudioWriter(folder.path());
        discarded->write(QByteArray(3200, '\0'));
        discarded->discard();
        QTRY_VERIFY(QDir(folder.path()).entryList(QDir::Files).isEmpty());
    }

    void historyKeepTakesTheLeast_data()
    {
        QTest::addColumn<HistoryKeep>("global");
        QTest::addColumn<std::optional<HistoryKeep>>("app");
        QTest::addColumn<std::optional<HistoryKeep>>("type");
        QTest::addColumn<bool>("secure");
        QTest::addColumn<HistoryKeep>("expected");
        const std::optional<HistoryKeep> unset;
        QTest::newRow("global text") << HistoryKeep::Text << unset << unset << false << HistoryKeep::Text;
        QTest::newRow("global audio") << HistoryKeep::Audio << unset << unset << false << HistoryKeep::Audio;
        QTest::newRow("global nothing") << HistoryKeep::Nothing << unset << unset << false << HistoryKeep::Nothing;
        QTest::newRow("app stats") << HistoryKeep::Audio << std::optional(HistoryKeep::Stats) << unset << false
                                   << HistoryKeep::Stats;
        QTest::newRow("type nothing") << HistoryKeep::Text << unset << std::optional(HistoryKeep::Nothing) << false
                                      << HistoryKeep::Nothing;
        QTest::newRow("app over type") << HistoryKeep::Text << std::optional(HistoryKeep::Stats)
                                       << std::optional(HistoryKeep::Nothing) << false << HistoryKeep::Stats;
        QTest::newRow("app never raises") << HistoryKeep::Nothing << std::optional(HistoryKeep::Stats) << unset << false
                                          << HistoryKeep::Nothing;
        QTest::newRow("password field") << HistoryKeep::Audio << unset << unset << true << HistoryKeep::Stats;
        QTest::newRow("password field, nothing") << HistoryKeep::Nothing << unset << unset << true << HistoryKeep::Nothing;
    }

    // What Speecher Keeps, lowered by the App, else its App Type, and a
    // password field, through the same resolver dictation uses.
    void historyKeepTakesTheLeast()
    {
        QFETCH(HistoryKeep, global);
        QFETCH(std::optional<HistoryKeep>, app);
        QFETCH(std::optional<HistoryKeep>, type);
        QFETCH(bool, secure);
        QFETCH(HistoryKeep, expected);
        AppsSettings apps;
        AppEntry thunderbird{QStringLiteral("app:1"), QStringLiteral("Thunderbird"), {},
                             {{AppMatchKind::Id, QStringLiteral("org.mozilla.thunderbird")}}};
        thunderbird.history = app;
        apps.apps.append(thunderbird);
        for (AppTypeSettings &each : apps.types) {
            if (each.type == AppCategory::Email) {
                each.history = type;
            }
        }
        Target target;
        target.applicationId = QStringLiteral("org.mozilla.thunderbird");
        target.applicationName = QStringLiteral("Thunderbird");
        target.secure = secure;
        HistorySettings settings;
        settings.keeps = global;
        QCOMPARE(historyKeep(settings, resolveApp(target, apps), target), expected);
    }
};

int runHistoryStoreTests(int argc, char **argv)
{
    HistoryStoreTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_history_store.moc"
