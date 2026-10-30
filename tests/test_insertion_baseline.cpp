#include "common/test_suites.h"
#include "platform/InsertionBaseline.h"

using namespace speecher;

namespace {

class InsertionBaselineTests : public QObject {
    Q_OBJECT

private slots:
    void verifiesOnlyTheReplacedSelection()
    {
        const QString before = QStringLiteral("before text cute after text");
        const auto baseline = InsertionBaseline::forSelection(before, 12, 16);
        QVERIFY(baseline);
        QVERIFY(!InsertionBaseline::forSelection(before, 12, 40));

        // A control that shows the write a turn late still verifies.
        QStringList reads{before, QStringLiteral("before text Qt after text")};
        QVERIFY(baseline->verify(QStringLiteral("Qt"), [&reads]() -> std::optional<QString> {
            return reads.size() > 1 ? reads.takeFirst() : reads.first();
        }));
        // So does nothing else: a change beyond the selection, text identical to
        // what it replaced, or a control that can no longer be read.
        QVERIFY(!baseline->verify(QStringLiteral("Qt"), []() -> std::optional<QString> {
            return QStringLiteral("before text Qt after text!");
        }));
        QVERIFY(!baseline->verify(QStringLiteral("cute"), [&before]() -> std::optional<QString> {
            return before;
        }));
        int readCount = 0;
        QVERIFY(!baseline->verify(QStringLiteral("Qt"), [&readCount]() -> std::optional<QString> {
            ++readCount;
            return std::nullopt;
        }));
        QCOMPARE(readCount, 1);

        const auto window = baseline->correctionWindow(Target{}, QStringLiteral("Qt"));
        QVERIFY(window);
        QCOMPARE(window->original, QStringLiteral("Qt"));
        QCOMPARE(window->prefix, QStringLiteral("before text "));
        QCOMPARE(window->suffix, QStringLiteral(" after text"));
        QVERIFY(!InsertionBaseline::forSelection(QStringLiteral("say cute after text"), 4, 8)
                     ->correctionWindow(Target{}, QStringLiteral("Qt")));
    }
};

} // namespace

int runInsertionBaselineTests(int argc, char **argv)
{
    InsertionBaselineTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_insertion_baseline.moc"
