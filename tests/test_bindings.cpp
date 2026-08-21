#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_auth.h"

using namespace speecher::test;


class BindingsTests : public QObject {
    Q_OBJECT

private slots:
    void bindingNormalizationAndValidation()
    {
        QCOMPARE(BindingProcessor::normalizedPhrase(QStringLiteral(" My, EMAIL! C++ repo_path ")),
                 QStringLiteral("my email c repo path"));
        QCOMPARE(BindingProcessor::normalizedTokens(QStringLiteral("alpha.beta/gamma")),
                 QStringList({QStringLiteral("alpha"), QStringLiteral("beta"), QStringLiteral("gamma")}));

        const BindingValidationResult validation = BindingProcessor::validateRules({
            {QStringLiteral("my,email"), QStringLiteral("one")},
            {QStringLiteral("MY email"), QStringLiteral("two")},
            {QStringLiteral("++"), QStringLiteral("symbols only")},
            {QStringLiteral("empty replacement"), QStringLiteral("   ")},
        });
        QVERIFY(!validation.ok());
        QCOMPARE(validation.issues.size(), 3);
        QVERIFY(validation.issues.at(0).type == BindingValidationIssue::Type::DuplicatePhrase);
        QVERIFY(validation.issues.at(1).type == BindingValidationIssue::Type::EmptyPhrase);
        QVERIFY(validation.issues.at(2).type == BindingValidationIssue::Type::EmptyReplacement);
        QCOMPARE(validation.rules.size(), 1);
        QCOMPARE(validation.rules.at(0).phrase, QStringLiteral("my,email"));

        QCOMPARE(BindingProcessor::refinementVocabulary({
                     {QStringLiteral("my,email"), QStringLiteral("efox@example.com")},
                     {QStringLiteral("speecher repo"), QStringLiteral("/home/efox/projects/speecher3")},
                 }),
                 QStringList({QStringLiteral("my,email"),
                              QStringLiteral("my email"),
                              QStringLiteral("speecher repo")}));
        QCOMPARE(BindingProcessor::explicitNoBindPhrases(
                     QStringLiteral("please write my email but don't turn that into a binding"),
                     {{QStringLiteral("my email"), QStringLiteral("efox@example.com")}}),
                 QStringList({QStringLiteral("my email")}));
        QCOMPARE(BindingProcessor::explicitNoBindPhrases(
                     QStringLiteral("please write my email and my phone but don't turn that into a binding"),
                     {
                         {QStringLiteral("my email"), QStringLiteral("efox@example.com")},
                         {QStringLiteral("my phone"), QStringLiteral("+1 555 0100")},
                     }),
                 QStringList({QStringLiteral("my phone")}));
        QCOMPARE(BindingProcessor::explicitNoBindPhrases(
                     QStringLiteral("write my email, don't bind that, then write my phone"),
                     {
                         {QStringLiteral("my email"), QStringLiteral("efox@example.com")},
                         {QStringLiteral("my phone"), QStringLiteral("+1 555 0100")},
                     }),
                 QStringList({QStringLiteral("my email")}));
        QVERIFY(BindingProcessor::hasExplicitNoBindDirective(
            QStringLiteral("please write my evil but don't turn that into a binding")));
        QCOMPARE(BindingProcessor::explicitNoBindPhrases(
                     QStringLiteral("write my phone, but don't bind my evil"),
                     {
                         {QStringLiteral("my phone"), QStringLiteral("+1 555 0100")},
                         {QStringLiteral("my email"), QStringLiteral("efox@example.com")},
                     }),
                 QStringList());
    }

    void snippetJsonImportSupportsArraysAndMappings()
    {
        QString error;
        QList<BindingRule> rules = BindingProcessor::parseJsonImport(
            QByteArrayLiteral(R"({"snippets":[{"trigger":"sign off","expansion":"Regards,\nEfox"}]})"),
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(rules, QList<BindingRule>({
                            {QStringLiteral("sign off"), QStringLiteral("Regards,\nEfox")},
                        }));

        rules = BindingProcessor::parseJsonImport(
            QByteArrayLiteral(R"({"home address":"123 Main Street","email me":"efox@example.com"})"),
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(rules.size(), 2);
    }

    void bindingProcessorMatchesCasePunctuationAndSkipsCoveredText()
    {
        const BindingProcessingResult result = BindingProcessor::process(
            QStringLiteral("My, email! my phone"),
            {
                {QStringLiteral("my email"), QStringLiteral("efox@example.com")},
                {QStringLiteral("my phone"), QStringLiteral("+1 555 0100")},
            });

        QCOMPARE(result.boundText, QStringLiteral("efox@example.com! +1 555 0100"));
        QCOMPARE(result.placeholderText, QStringLiteral("SPEECHER_BINDING_0! SPEECHER_BINDING_1"));
        QCOMPARE(result.canSkipRefinement, true);
        QCOMPARE(result.placeholders.size(), 2);
    }

    void bindingProcessorRejectsGluedWordsAndPrefersLongestMatch()
    {
        const BindingProcessingResult result = BindingProcessor::process(
            QStringLiteral("open main repo and repo mainrepo"),
            {
                {QStringLiteral("repo"), QStringLiteral("R")},
                {QStringLiteral("main repo"), QStringLiteral("M")},
            });

        QCOMPARE(result.boundText, QStringLiteral("open M and R mainrepo"));
        QCOMPARE(result.placeholderText, QStringLiteral("open SPEECHER_BINDING_0 and SPEECHER_BINDING_1 mainrepo"));
        QCOMPARE(result.canSkipRefinement, false);
    }

    void bindingProcessorRestoresPlaceholdersAndAllowsMissingOnes()
    {
        const BindingProcessingResult result = BindingProcessor::process(
            QStringLiteral("my email and my email"),
            {{QStringLiteral("my email"), QStringLiteral("efox@example.com")}});

        QCOMPARE(result.boundText, QStringLiteral("efox@example.com and efox@example.com"));
        QCOMPARE(result.placeholderText, QStringLiteral("SPEECHER_BINDING_0 and SPEECHER_BINDING_1"));
        QCOMPARE(result.placeholders.size(), 2);

        BindingRestoreResult restored = BindingProcessor::restorePlaceholders(
            QStringLiteral("Please send SPEECHER_BINDING_0."),
            result.placeholders);
        QVERIFY(restored.ok);
        QCOMPARE(restored.text, QStringLiteral("Please send efox@example.com."));

        restored = BindingProcessor::restorePlaceholders(QStringLiteral("Please send Alex."), result.placeholders);
        QVERIFY(restored.ok);
        QCOMPARE(restored.text, QStringLiteral("Please send Alex."));

        restored = BindingProcessor::restorePlaceholders(
            QStringLiteral("Please send SPEECHER_BINDING_99."),
            result.placeholders);
        QVERIFY(!restored.ok);

        restored = BindingProcessor::restorePlaceholders(
            QStringLiteral("Please send Speecher binding 0."),
            result.placeholders);
        QVERIFY(!restored.ok);

        restored = BindingProcessor::restorePlaceholders(
            QStringLiteral("Please send Speecher binding zero."),
            result.placeholders);
        QVERIFY(!restored.ok);

        QCOMPARE(BindingProcessor::applyBindingsOutsidePlaceholders(
                     QStringLiteral("SPEECHER_BINDING_0 and speecher binding"),
                     {{QStringLiteral("speecher binding"), QStringLiteral("bound")}}),
                 QStringLiteral("SPEECHER_BINDING_0 and bound"));
    }
};

int runBindingsTests(int argc, char **argv)
{
    BindingsTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_bindings.moc"
