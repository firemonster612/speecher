#include "common/test_suites.h"

#include "core/settings/SettingsSchema.h"

using namespace speecher;

namespace {

SchemaContext fakeContext()
{
    return {
        {{QStringLiteral("claude"), QStringLiteral("Claude Voice")}},
        {{QStringLiteral("openai"), QStringLiteral("OpenAI")}},
        [] {
            return QList<RowOption>{{QStringLiteral("mic-1"), QStringLiteral("Desk microphone")}};
        },
        QStringLiteral("Fake clipboard path"),
    };
}

const SettingsRow &rowById(const SettingsPage &page, const QString &id)
{
    for (const SettingsSection &section : page.sections) {
        for (const SettingsRow &row : section.rows) {
            if (row.id == id) {
                return row;
            }
        }
    }
    qFatal("no row %s on page %s", qPrintable(id), qPrintable(page.id));
}

} // namespace

class SettingsSchemaTests : public QObject {
    Q_OBJECT

private slots:
    void rowsRoundTripAValueThroughAppSettings()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &previewWords = rowById(schema.page(QStringLiteral("general")),
                                                  QStringLiteral("previewWords"));
        const SettingsRow &captureMode = rowById(schema.page(QStringLiteral("audio")),
                                                 QStringLiteral("captureMode"));
        const SettingsRow &profiles = rowById(schema.page(QStringLiteral("refinement")),
                                              QStringLiteral("writingProfileBehavior"));

        AppSettings settings;
        previewWords.apply(settings, 21);
        captureMode.apply(settings, QStringLiteral("warm"));
        profiles.apply(settings,
                       QVariant::fromValue(QList<WritingProfileSettings>{
                           {WritingProfile::Email, QStringLiteral("strong_polish"), QStringLiteral("formal")}}));

        QCOMPARE(settings.ui.previewWords, 21);
        QCOMPARE(settings.audio.mode, QStringLiteral("warm"));
        QCOMPARE(previewWords.value(settings).toInt(), 21);
        QCOMPARE(captureMode.value(settings).toString(), QStringLiteral("warm"));
        QCOMPARE(profiles.value(settings).value<QList<WritingProfileSettings>>().size(), 1);
        QVERIFY(profiles.value(settings) != profiles.value(AppSettings{}));
    }

    void targetContextNeedsAccessibility()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        const SettingsRow &row = rowById(schema.page(QStringLiteral("refinement")),
                                         QStringLiteral("targetContextControl"));

        QVERIFY(!row.enabled(AppSettings{}, Capabilities{false}));
        QVERIFY(row.enabled(AppSettings{}, Capabilities{true}));
        QVERIFY(!row.disabledHelp.isEmpty());
    }

    void onlyTheMicrophoneListIsExpensive()
    {
        const SettingsSchema schema = buildSettingsSchema(fakeContext());
        QStringList expensive;
        for (const SettingsPage &page : schema.pages) {
            for (const SettingsSection &section : page.sections) {
                for (const SettingsRow &row : section.rows) {
                    if (row.expensive) {
                        expensive.append(row.id);
                    }
                }
            }
        }
        QCOMPARE(expensive, QStringList{QStringLiteral("audioDevice")});
    }

    void aSavedMicrophoneSurvivesGoingMissing()
    {
        const QList<RowOption> present{{QStringLiteral("mic-1"), QStringLiteral("Desk microphone")}};

        const QList<RowOption> withMissing =
            audioDeviceOptions(present, QStringLiteral("mic-gone"));
        QCOMPARE(withMissing.size(), 3);
        QCOMPARE(withMissing.last().id, QStringLiteral("mic-gone"));
        QVERIFY(!withMissing.last().enabled);

        const QList<RowOption> withPresent = audioDeviceOptions(present, QStringLiteral("mic-1"));
        QCOMPARE(withPresent.size(), 2);
        QCOMPARE(withPresent.first().id, QString());
    }
};

int runSettingsSchemaTests(int argc, char **argv)
{
    SettingsSchemaTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_settings_schema.moc"
