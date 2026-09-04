#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_auth.h"
#include "output/HelperPath.h"
#include "setup/YdotoolSetupTransaction.h"

#include <QScopeGuard>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

using namespace speecher::test;

class FakeBackend final : public DeliveryBackend {
public:
    FakeBackend(QString method, QList<QString> *attempts, QHash<QString, bool> *results)
        : m_method(std::move(method))
        , m_attempts(attempts)
        , m_results(results)
    {
    }

    bool deliver(const DeliveryContent &content, bool *htmlAvailable, QString *error) override
    {
        m_attempts->append(m_method);
        if (htmlAvailable) {
            *htmlAvailable = content.html.has_value() && m_method == QString::fromLatin1(OutputMethod::QtClipboard);
        }
        const bool ok = m_results->value(m_method, false);
        if (!ok && error) {
            *error = m_method + QStringLiteral(" failed");
        }
        return ok;
    }

private:
    QString m_method;
    QList<QString> *m_attempts = nullptr;
    QHash<QString, bool> *m_results = nullptr;
};


// The synthetic-input method TextDelivery::orderedMethods emits first on this
// platform. The fake backend factory serves whichever name it is handed, so
// the delivery-flow tests only need the right name in expectations.
static QString virtualKeyboardMethod()
{
#ifdef Q_OS_MACOS
    return QString::fromLatin1(OutputMethod::MacPaste);
#else
    return QString::fromLatin1(OutputMethod::Ydotool);
#endif
}

class DeliveryTests : public QObject {
    Q_OBJECT

private slots:
#ifdef SPEECHER_WITH_WAYLAND
    void portalResponseTrackerKeepsResponseUntilActualHandleArrives()
    {
        PortalResponseTracker tracker;
        tracker.begin(QDBusObjectPath(QStringLiteral("/predicted")));

        QVariantMap results;
        results.insert(QStringLiteral("uri"), QStringLiteral("file:///tmp/screenshot.png"));
        QVERIFY(!tracker.observe(QStringLiteral("/actual"), 0, results));

        const auto response = tracker.resolve(QDBusObjectPath(QStringLiteral("/actual")));
        QVERIFY(response);
        QCOMPARE(response->status, 0U);
        QCOMPARE(response->results, results);
    }

    void portalScreenshotTimeoutFailsCapture()
    {
        PortalScreenshotContextProvider provider;
        QSignalSpy failed(&provider, &PortalScreenshotContextProvider::failed);

        QVERIFY(QMetaObject::invokeMethod(&provider, "handleTimeout"));

        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.first().first().toString(), QStringLiteral("Screenshot capture timed out"));
    }
#endif

    void ydotoolSetupFailureReportsCompletedChanges()
    {
        YdotoolSetupTransaction transaction;
        transaction.record(QStringLiteral("wrote /etc/modules-load.d/speecher-uinput.conf"));
        transaction.record(QStringLiteral("added efox to speecher-uinput"));
        QString error = QStringLiteral("Could not write udev rule");

        transaction.appendToError(&error);

        QCOMPARE(error,
                 QStringLiteral("Could not write udev rule\nChanges left behind:\n"
                                "- wrote /etc/modules-load.d/speecher-uinput.conf\n"
                                "- added efox to speecher-uinput"));
    }

    void helperPathPrefersSiblingThenLibexecThenInstalled()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString applicationDir = dir.filePath(QStringLiteral("bin"));
        const QString sibling = applicationDir + QStringLiteral("/helper");
        const QString bundled = dir.filePath(QStringLiteral("libexec/speecher/helper"));
        const QString installed = QStringLiteral("/usr/libexec/speecher/helper");
        QVERIFY(QDir().mkpath(QFileInfo(sibling).dir().path()));
        QVERIFY(QDir().mkpath(QFileInfo(bundled).dir().path()));

        QCOMPARE(resolvedHelperPath("/usr/libexec/speecher/helper", applicationDir), installed);

        QFile bundledFile(bundled);
        QVERIFY(bundledFile.open(QIODevice::WriteOnly));
        bundledFile.close();
        QVERIFY(QFile::setPermissions(bundled,
                                      QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                          | QFileDevice::ExeOwner));
        QCOMPARE(resolvedHelperPath("/usr/libexec/speecher/helper", applicationDir),
                 QFileInfo(bundled).canonicalFilePath());

        QFile siblingFile(sibling);
        QVERIFY(siblingFile.open(QIODevice::WriteOnly));
        siblingFile.close();
        QCOMPARE(resolvedHelperPath("/usr/libexec/speecher/helper", applicationDir),
                 QFileInfo(bundled).canonicalFilePath());
        QVERIFY(QFile::setPermissions(sibling,
                                      QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                          | QFileDevice::ExeOwner));
        QCOMPARE(resolvedHelperPath("/usr/libexec/speecher/helper", applicationDir),
                 QFileInfo(sibling).canonicalFilePath());

        QVERIFY(QFile::remove(sibling));
        QCOMPARE(resolvedHelperPath("/usr/libexec/speecher/helper", applicationDir),
                 QFileInfo(bundled).canonicalFilePath());

        QVERIFY(QFile::remove(bundled));
        QCOMPARE(resolvedHelperPath("/usr/libexec/speecher/helper", applicationDir), installed);
    }

    void outputDeliverySelection()
    {
        OutputSettings settings;
        settings.method = QString::fromLatin1(OutputMethod::Automatic);
        settings.ydotoolEnabled = true;
#ifdef Q_OS_MACOS
        QCOMPARE(TextDelivery::orderedMethods(settings),
                 QStringList({QString::fromLatin1(OutputMethod::MacPaste),
                              QString::fromLatin1(OutputMethod::QtClipboard)}));

        // The ydotool toggle is a Linux setting; it has no mac effect.
        settings.ydotoolEnabled = false;
        QCOMPARE(TextDelivery::orderedMethods(settings),
                 QStringList({QString::fromLatin1(OutputMethod::MacPaste),
                              QString::fromLatin1(OutputMethod::QtClipboard)}));

        settings.method = QString::fromLatin1(OutputMethod::MacPaste);
        QCOMPARE(TextDelivery::orderedMethods(settings),
                 QStringList({QString::fromLatin1(OutputMethod::MacPaste)}));
#else
        QCOMPARE(TextDelivery::orderedMethods(settings),
                 QStringList({QString::fromLatin1(OutputMethod::Ydotool),
                              QString::fromLatin1(OutputMethod::WlCopy),
                              QString::fromLatin1(OutputMethod::QtClipboard)}));

        settings.ydotoolEnabled = false;
        QCOMPARE(TextDelivery::orderedMethods(settings),
                 QStringList({QString::fromLatin1(OutputMethod::WlCopy),
                              QString::fromLatin1(OutputMethod::QtClipboard)}));

        settings.method = QString::fromLatin1(OutputMethod::Ydotool);
        QCOMPARE(TextDelivery::orderedMethods(settings),
                 QStringList({
                     QString::fromLatin1(OutputMethod::WlCopy),
                     QString::fromLatin1(OutputMethod::QtClipboard),
                 }));
        settings.ydotoolEnabled = true;
        QCOMPARE(TextDelivery::orderedMethods(settings), QStringList({QString::fromLatin1(OutputMethod::Ydotool)}));
#endif

        settings.method = QString::fromLatin1(OutputMethod::QtClipboard);
        QCOMPARE(TextDelivery::orderedMethods(settings), QStringList({QString::fromLatin1(OutputMethod::QtClipboard)}));
    }

    void pasteRulesPreferApplicationThenCategoryThenGlobal()
    {
        const QList<PasteRule> rules{
            {PasteRuleScope::Global, QString(), PasteMethod::ClipboardOnly, true},
            {PasteRuleScope::Category, QStringLiteral("terminal"), PasteMethod::TerminalPaste, true},
            {PasteRuleScope::Application, QStringLiteral("org.kde.konsole"), PasteMethod::StandardPaste, true},
        };
        Target target;
        target.applicationId = QStringLiteral("ORG.KDE.KONSOLE");
        target.category = AppCategory::Terminal;
        QCOMPARE(resolvePasteRule(rules, target).method, PasteMethod::StandardPaste);

        target.applicationId = QStringLiteral("dev.warp.Warp");
        QCOMPARE(resolvePasteRule(rules, target).method, PasteMethod::TerminalPaste);

        target.category = AppCategory::General;
        QCOMPARE(resolvePasteRule(rules, target).method, PasteMethod::ClipboardOnly);
    }

    void accessibleTerminalWithoutApplicationIdUsesTerminalPaste()
    {
        PasteMethod attemptedMethod = PasteMethod::ClipboardOnly;
        QList<QString> attempts;
        QHash<QString, bool> results{{QString::fromLatin1(OutputMethod::Ydotool), true}};
        FakeTargetProvider targetProvider;
        TextDelivery delivery(
            [&attemptedMethod, &attempts, &results](const QString &method,
                                                   const OutputSettings &,
                                                   PasteMethod pasteMethod) {
                attemptedMethod = pasteMethod;
                return std::make_unique<FakeBackend>(method, &attempts, &results);
            },
            &targetProvider);
        OutputSettings settings;
        settings.method = QString::fromLatin1(OutputMethod::Ydotool);
        settings.ydotoolEnabled = true;
        settings.pasteRules = defaultPasteRules();
        Target target;
        target.accessible = true;
        target.role = QStringLiteral("terminal");
        target.category = classifyTarget(target);

        delivery.deliver(settings,
                         makeDeliveryContent(QStringLiteral("hello"), OutputFormat::PlainText),
                         target);

        QCOMPARE(attemptedMethod, PasteMethod::TerminalPaste);
    }

    void aiCodingAgentInTerminalKeepsTerminalPasteBehavior()
    {
        const QList<PasteRule> rules{
            {PasteRuleScope::Category, QStringLiteral("terminal"), PasteMethod::TerminalPaste, true},
            {PasteRuleScope::Category, QStringLiteral("ai_coding"), PasteMethod::StandardPaste, true},
            {PasteRuleScope::Global, QString(), PasteMethod::ClipboardOnly, true},
        };
        Target target;
        target.terminalHost = true;
        target.aiCodingToolActive = true;
        target.category = classifyTarget(target);

        QCOMPARE(target.category, AppCategory::AiCoding);
        QVERIFY(isTerminalTarget(target));
        QCOMPARE(resolvePasteRule(rules, target).method, PasteMethod::TerminalPaste);

        target.aiCodingToolActive = false;
        target.category = AppCategory::General;
        QVERIFY(isTerminalTarget(target));
        QCOMPARE(resolvePasteRule(rules, target).method, PasteMethod::TerminalPaste);
    }

    void recognitionRulesRejectEmptyMatchesAndRespectBuiltInBoundaries()
    {
        Target target;
        target.applicationId = QStringLiteral("org.example.barcode");
        QCOMPARE(classifyTarget(target), AppCategory::General);

        target.applicationId = QStringLiteral("org.example.!!!");
        const QList<AppRecognitionRule> punctuationRule{
            {QStringLiteral("!!!"), AppCategory::Terminal, WritingProfile::Work},
        };
        QCOMPARE(classifyTarget(target, punctuationRule), AppCategory::General);

        target.applicationId = QStringLiteral("code");
        QCOMPARE(classifyTarget(target), AppCategory::CodeEditor);
    }

    void writingProfilesAndPromptUseBoundedUntrustedContext()
    {
        Target target;
        target.applicationId = QStringLiteral("org.mozilla.Thunderbird");
        target.windowTitle = QStringLiteral("Write: Project update");
        target.documentUrl = QStringLiteral("https://mail.example.test/compose");
        target.category = AppCategory::Email;
        target.role = QStringLiteral("text");
        target.nearbyTextBefore = QStringLiteral("Hello Alex");
        target.nearbyTextAfter = QStringLiteral("Regards");
        target.selectedText = QStringLiteral("Alex");
        target.caretOffset = 10;
        target.selectionStart = 6;
        target.selectionEnd = 10;
        QCOMPARE(inferWritingProfile(target), WritingProfile::Email);

        RefinementContext context;
        context.target = target;
        context.writingProfile = WritingProfile::Email;
        context.tone = QStringLiteral("formal");
        context.includeNearbyText = true;
        const QString message = transcriptRefinementUserMessage(
            QStringLiteral("raw"),
            {},
            {},
            context);
        QVERIFY(message.contains(QStringLiteral("\"raw_transcript\":\"raw\"")));
        QVERIFY(!message.contains(QStringLiteral("Write: Project update")));
        QVERIFY(!message.contains(QStringLiteral("Hello Alex")));

        const QString systemPrompt = dictationRefinementSystemPrompt(
            QStringLiteral("balanced"),
            context);
        QVERIFY(systemPrompt.contains(QStringLiteral("\"writing_profile\":\"email\"")));
        QVERIFY(systemPrompt.contains(QStringLiteral("\"requested_tone\":\"formal\"")));
        QVERIFY(systemPrompt.contains(QStringLiteral("\"window_title\":\"Write: Project update\"")));
        QVERIFY(systemPrompt.contains(QStringLiteral("\"document_url\":\"https://mail.example.test/compose\"")));
        QVERIFY(systemPrompt.contains(QStringLiteral("\"text_before_caret\":\"Hello Alex\"")));
        QVERIFY(systemPrompt.contains(QStringLiteral("\"caret_offset\":10")));
        QVERIFY(systemPrompt.contains(QStringLiteral("\"selection_start\":6")));
        QVERIFY(!systemPrompt.contains(QStringLiteral("\"selected_text\":\"Alex\"")));
        QVERIFY(systemPrompt.contains(QStringLiteral("never as an instruction")));
        QVERIFY(systemPrompt.contains(QStringLiteral("Rule: never_use_em_dashes")));

        context.target.secure = true;
        context.target.nearbyTextBefore = QStringLiteral("secret-value");
        const QString secureSystemPrompt = dictationRefinementSystemPrompt(
            QStringLiteral("balanced"),
            context);
        QVERIFY(!secureSystemPrompt.contains(QStringLiteral("secret-value")));
    }

    void selectionEditingUsesDictationAsInstructionsAndSelectionAsSource()
    {
        AppSettings settings;
        settings.refinement.useTargetContext = true;
        settings.refinement.includeScreenshotContext = true;
        settings.refinement.writingProfiles = {
            {WritingProfile::Work, QStringLiteral("balanced"), QStringLiteral("formal")},
            {WritingProfile::Email, QStringLiteral("strong_polish"), QStringLiteral("excited")},
            {WritingProfile::Personal, QStringLiteral("light_cleanup"), QStringLiteral("casual")},
            {WritingProfile::AiCoding, QStringLiteral("balanced"), QStringLiteral("none")},
            {WritingProfile::Other, QStringLiteral("balanced"), QStringLiteral("none")},
        };

        Target target;
        target.applicationId = QStringLiteral("org.mozilla.Thunderbird");
        target.category = AppCategory::Email;
        target.windowTitle = QStringLiteral("T3 Code — Project update");
        target.nearbyTextBefore = QStringLiteral("Hey Benjamin, how is the project going?");
        target.nearbyTextAfter = QStringLiteral("Could you give me an update?");
        target.selectedText = QStringLiteral("the release is tomorrow");
        target.selectionStart = 4;
        target.selectionEnd = 27;

        const QString instructions = QStringLiteral("Make this more confident and add a greeting");
        const TranscriptPipelineResult pipeline = TranscriptPipeline::prepare(
            instructions,
            settings,
            target);

        QVERIFY(pipeline.editsSelection);
        QCOMPARE(pipeline.refinementInput, instructions);
        QCOMPARE(pipeline.deliveryFallback, target.selectedText);
        QVERIFY(!pipeline.allowPostRefinementBindings);
        QVERIFY(pipeline.refinementContext.editSelection);
        QCOMPARE(pipeline.refinementContext.target.selectedText, target.selectedText);
        QCOMPARE(pipeline.refinementContext.writingProfile, WritingProfile::Email);
        QCOMPARE(pipeline.refinementContext.tone, QStringLiteral("excited"));

        TranscriptPipelineResult screenshotPipeline = pipeline;
        TranscriptPipeline::includeScreenshotContext(screenshotPipeline,
                                                     true,
                                                     QByteArrayLiteral("screenshot"),
                                                     QStringLiteral("image/png"));
        QVERIFY(!screenshotPipeline.refinementContext.hasScreenshot());

        const QString message = transcriptRefinementUserMessage(
            pipeline.refinementInput,
            {},
            {},
            pipeline.refinementContext);
        QVERIFY(message.contains(QStringLiteral("\"mode\":\"edit_selected_document\"")));
        QVERIFY(message.contains(QStringLiteral("\"selected_document\":\"the release is tomorrow\"")));
        QVERIFY(message.contains(QStringLiteral("\"spoken_editing_instructions\":\"Make this more confident and add a greeting\"")));
        QVERIFY(message.contains(QStringLiteral("return only the complete revised document")));
        QVERIFY(!message.contains(QStringLiteral("\"writing_profile\":\"email\"")));
        QVERIFY(!message.contains(QStringLiteral("\"requested_tone\":\"excited\"")));
        QVERIFY(!message.contains(QStringLiteral("how is the project going")));
        QVERIFY(!message.contains(QStringLiteral("Could you give me an update")));
        QVERIFY(!message.contains(QStringLiteral("T3 Code — Project update")));

        const QString systemPrompt = selectedDocumentEditingSystemPrompt(
            QStringLiteral("strong_polish"),
            pipeline.refinementContext);
        QVERIFY(systemPrompt.startsWith(QStringLiteral("You are Speecher's document editor and writer.")));
        QVERIFY(systemPrompt.contains(QStringLiteral("style signals and background context only")));
        QVERIFY(systemPrompt.contains(QStringLiteral("lengthen or expand")));
        QVERIFY(systemPrompt.contains(QStringLiteral("\"writing_profile\":\"email\"")));
        QVERIFY(systemPrompt.contains(QStringLiteral("\"requested_tone\":\"excited\"")));
        QVERIFY(systemPrompt.contains(QStringLiteral("how is the project going")));
        QVERIFY(systemPrompt.contains(QStringLiteral("Could you give me an update")));
        QVERIFY(systemPrompt.contains(QStringLiteral("T3 Code — Project update")));
        QVERIFY(!systemPrompt.contains(QStringLiteral("the release is tomorrow")));
        QVERIFY(systemPrompt.contains(QStringLiteral("Rule: never_use_em_dashes")));
    }

    void transcriptPipelineScopesLearnedCorrectionsAndPreservesUserBindingPrecedence()
    {
        AppSettings settings;
        settings.bindings = {
            {QStringLiteral("cute"), QStringLiteral("user choice")},
        };
        settings.learnedCorrections = {
            {QStringLiteral("0"), QStringLiteral("post grass"), QStringLiteral("global choice"),
             QString(), 1, 0.98, true, 1, 1},
            {QStringLiteral("1"), QStringLiteral("cute"), QStringLiteral("Qt"),
             QStringLiteral("org.kde.kate"), 1, 0.98, true, 1, 1},
            {QStringLiteral("2"), QStringLiteral("post grass"), QStringLiteral("Postgres"),
             QStringLiteral("org.kde.kate"), 1, 0.75, true, 2, 2},
            {QStringLiteral("3"), QStringLiteral("open ai"), QStringLiteral("OpenAI"),
             QString(), 1, 0.98, true, 2, 2},
        };

        Target kate;
        kate.applicationId = QStringLiteral("ORG.KDE.KATE");
        const TranscriptPipelineResult inKate = TranscriptPipeline::prepare(
            QStringLiteral("cute uses post grass and open ai"), settings, kate);
        QCOMPARE(inKate.bindingResult.boundText,
                 QStringLiteral("user choice uses Postgres and OpenAI"));
        QVERIFY(inKate.refinementVocabulary.contains(QStringLiteral("Qt")));
        QVERIFY(inKate.refinementVocabulary.contains(QStringLiteral("Postgres")));
        QVERIFY(inKate.refinementVocabulary.contains(QStringLiteral("OpenAI")));

        Target firefox;
        firefox.applicationId = QStringLiteral("org.mozilla.firefox");
        const TranscriptPipelineResult inFirefox = TranscriptPipeline::prepare(
            QStringLiteral("cute uses post grass and open ai"), settings, firefox);
        QCOMPARE(inFirefox.bindingResult.boundText,
                 QStringLiteral("user choice uses global choice and OpenAI"));
    }

    void applicationMatrixClassifiesWritingProfiles()
    {
        const auto classified = [](const QString &applicationId) {
            Target target;
            target.applicationId = applicationId;
            target.category = classifyTarget(target);
            return target;
        };

        QCOMPARE(classified(QStringLiteral("kate")).category, AppCategory::CodeEditor);
        QCOMPARE(classified(QStringLiteral("konsole")).category, AppCategory::Terminal);
        QCOMPARE(classified(QStringLiteral("com.mitchellh.ghostty")).category, AppCategory::Terminal);
        QCOMPARE(classified(QStringLiteral("firefox")).category, AppCategory::Browser);
        QCOMPARE(classified(QStringLiteral("google-chrome")).category, AppCategory::Browser);
        QCOMPARE(classified(QStringLiteral("org.chromium.Chromium")).category, AppCategory::Browser);
        QCOMPARE(classified(QStringLiteral("helium")).category, AppCategory::Browser);
        QCOMPARE(classified(QStringLiteral("thunderbird")).category, AppCategory::Email);
        QCOMPARE(classified(QStringLiteral("soffice.bin")).category, AppCategory::Office);
        for (const QString &application : {
                 QStringLiteral("t3-code"),
                 QStringLiteral("chatgpt"),
                 QStringLiteral("codex"),
                 QStringLiteral("cursor"),
                 QStringLiteral("windsurf"),
                 QStringLiteral("kiro"),
                 QStringLiteral("opencode"),
                 QStringLiteral("aider"),
                 QStringLiteral("goose"),
                 QStringLiteral("qwen-code"),
                 QStringLiteral("mistral-vibe"),
                 QStringLiteral("cline"),
             }) {
            QCOMPARE(classified(application).category, AppCategory::AiCoding);
        }

        QCOMPARE(inferWritingProfile(classified(QStringLiteral("kate"))), WritingProfile::Work);
        QCOMPARE(inferWritingProfile(classified(QStringLiteral("konsole"))), WritingProfile::Work);
        QCOMPARE(inferWritingProfile(classified(QStringLiteral("thunderbird"))), WritingProfile::Email);
        QCOMPARE(inferWritingProfile(classified(QStringLiteral("t3-code"))), WritingProfile::AiCoding);
        QCOMPARE(inferWritingProfile(classified(QStringLiteral("libreoffice"))), WritingProfile::Work);
        QCOMPARE(inferWritingProfile(classified(QStringLiteral("org.signal.Signal"))), WritingProfile::Personal);
        QCOMPARE(inferWritingProfile(classified(QStringLiteral("firefox")), WritingProfile::Other), WritingProfile::Other);

        const QList<WritingProfileOverride> overrides{
            {QStringLiteral("firefox"), WritingProfile::Personal, true},
            {QStringLiteral("kate"), WritingProfile::Other, false},
        };
        QCOMPARE(resolveWritingProfile(classified(QStringLiteral("firefox")), overrides, WritingProfile::Other),
                 WritingProfile::Personal);
        QCOMPARE(resolveWritingProfile(classified(QStringLiteral("kate")), overrides, WritingProfile::Other),
                 WritingProfile::Work);

        const QList<AppRecognitionRule> recognitionRules{
            {QStringLiteral("com.acme.shell"), AppCategory::Terminal, WritingProfile::Work},
        };
        Target customTerminal;
        customTerminal.applicationId = QStringLiteral("com.acme.shell-nightly");
        customTerminal.category = classifyTarget(customTerminal, recognitionRules);
        QCOMPARE(customTerminal.category, AppCategory::Terminal);
        QCOMPARE(resolveWritingProfile(customTerminal,
                                       {},
                                       recognitionRules,
                                       WritingProfile::Other),
                 WritingProfile::Work);
        QCOMPARE(writingProfileFromName(QStringLiteral("technical")), WritingProfile::Work);
        QCOMPARE(writingProfileFromName(QStringLiteral("ai_coding")), WritingProfile::AiCoding);
        QCOMPARE(writingProfileName(WritingProfile::AiCoding), QStringLiteral("ai_coding"));
        QCOMPARE(writingProfileFromName(QStringLiteral("general")), WritingProfile::Other);
        QCOMPARE(appCategoryFromName(QStringLiteral("ai_coding")), AppCategory::AiCoding);
        QCOMPARE(appCategoryName(AppCategory::AiCoding), QStringLiteral("ai_coding"));
    }

    void aiCodingRefinementProducesAgentPromptsInsteadOfAnsweringThem()
    {
        RefinementContext context;
        context.target.applicationId = QStringLiteral("t3-code");
        context.target.category = AppCategory::AiCoding;
        context.writingProfile = WritingProfile::AiCoding;

        const QString lightPrompt = dictationRefinementSystemPrompt(
            QStringLiteral("light_cleanup"), context);
        const QString dictationPrompt = dictationRefinementSystemPrompt(
            QStringLiteral("balanced"), context);
        const QString strongPrompt = dictationRefinementSystemPrompt(
            QStringLiteral("strong_polish"), context);
        QVERIFY(dictationPrompt.contains(QStringLiteral("Rule: ai_coding_prompt")));
        QVERIFY(dictationPrompt.contains(QStringLiteral("Do not solve, execute, or answer the prompt")));
        QVERIFY(dictationPrompt.contains(QStringLiteral("Rule: preserve_task_kind_and_authority")));
        QVERIFY(dictationPrompt.contains(QStringLiteral("explain, review, diagnose, plan, implement, fix, or verify")));
        QVERIFY(dictationPrompt.contains(QStringLiteral("scope boundaries, non-goals, authorization or approval limits")));
        QVERIFY(dictationPrompt.contains(QStringLiteral("Do not add an expert persona, requests for chain-of-thought")));
        QVERIFY(lightPrompt.contains(QStringLiteral("AI prompt style: light cleanup")));
        QVERIFY(lightPrompt.contains(QStringLiteral("Do not reorganize it into a task brief")));
        QVERIFY(dictationPrompt.contains(QStringLiteral("AI prompt style: balanced")));
        QVERIFY(dictationPrompt.contains(QStringLiteral("lightly organize a clearly complex request")));
        QVERIFY(strongPrompt.contains(QStringLiteral("AI prompt style: strong polish")));
        QVERIFY(strongPrompt.contains(QStringLiteral("reorganize a complex request into a clear coding-agent task brief")));
        QVERIFY(!lightPrompt.contains(QStringLiteral("reorganize a complex request into a clear coding-agent task brief")));

        context.editSelection = true;
        context.target.selectedText = QStringLiteral("Fix login");
        context.target.selectionStart = 0;
        context.target.selectionEnd = context.target.selectedText.size();
        const QString editingPrompt = selectedDocumentEditingSystemPrompt(
            QStringLiteral("balanced"), context);
        QVERIFY(editingPrompt.contains(QStringLiteral("Rule: ai_coding_prompt")));
    }

    void outputUsesClipboardOnlyForMissingOrSecureTargets()
    {
        OutputSettings settings;
        settings.method = QString::fromLatin1(OutputMethod::Automatic);
        settings.ydotoolEnabled = true;
#ifdef Q_OS_MACOS
        const QStringList expected{QString::fromLatin1(OutputMethod::QtClipboard)};
#else
        const QStringList expected{QString::fromLatin1(OutputMethod::WlCopy),
                                   QString::fromLatin1(OutputMethod::QtClipboard)};
#endif
        QCOMPARE(TextDelivery::orderedMethods(settings, PasteMethod::ClipboardOnly), expected);
    }

    void outputContentBuildsSafeDualMimeRepresentations()
    {
        const DeliveryContent plain = makeDeliveryContent(QStringLiteral("<b>Hello</b>"), OutputFormat::PlainText);
        QCOMPARE(plain.plainText, QStringLiteral("<b>Hello</b>"));
        QVERIFY(!plain.html);

        const DeliveryContent rich = makeDeliveryContent(
            QStringLiteral("<b>Hello</b>\nline two\n\nnext"),
            OutputFormat::Html);
        QCOMPARE(rich.plainText, QStringLiteral("<b>Hello</b>\nline two\n\nnext"));
        QVERIFY(rich.html);
        QCOMPARE(*rich.html,
                 QStringLiteral("<p>&lt;b&gt;Hello&lt;/b&gt;<br>line two</p>\n<p>next</p>"));
        QVERIFY(!rich.html->contains(QStringLiteral("<b>Hello</b>")));
    }

    void outputAutomaticFallbackOrder()
    {
        QApplication::clipboard()->setText(QStringLiteral("previous clipboard"));

        QList<QString> attempts;
        QList<bool> restoreFlags;
        QHash<QString, bool> results;
        results.insert(virtualKeyboardMethod(), false);
        results.insert(QString::fromLatin1(OutputMethod::WlCopy), true);
        results.insert(QString::fromLatin1(OutputMethod::QtClipboard), true);

        FakeTargetProvider targetProvider;
        targetProvider.directInsertionAvailable = true;
        targetProvider.inserted = false;
        TextDelivery delivery([&attempts, &restoreFlags, &results](
                                  const QString &method,
                                  const OutputSettings &settings,
                                  PasteMethod) {
            restoreFlags.append(settings.restoreClipboardAfterTyping);
            return std::make_unique<FakeBackend>(method, &attempts, &results);
        }, &targetProvider);

        OutputSettings settings;
        settings.method = QString::fromLatin1(OutputMethod::Automatic);
        settings.ydotoolEnabled = true;
        settings.restoreClipboardAfterTyping = true;
        Target target;
        target.applicationId = QStringLiteral("org.kde.kate");
        target.category = AppCategory::CodeEditor;
        const DeliveryResult result = delivery.deliver(
            settings,
            makeDeliveryContent(QStringLiteral("hello"), OutputFormat::PlainText),
            target);
        QVERIFY(result.ok);
        QCOMPARE(result.receipt, DeliveryReceipt::Copied);
        QCOMPARE(targetProvider.insertCalls, 1);
        QCOMPARE(attempts, QList<QString>({virtualKeyboardMethod()}));
        QCOMPARE(restoreFlags, QList<bool>({true}));
        QCOMPARE(result.receipt, DeliveryReceipt::Copied);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("hello"));
    }

    void outputAutomaticAndExplicitAccessibilityUseDirectInsertion()
    {
        for (const QString &method : {QStringLiteral("automatic"),
                                      QStringLiteral("direct_insert")}) {
            QList<QString> attempts;
            QHash<QString, bool> results{{virtualKeyboardMethod(), true}};
            FakeTargetProvider targetProvider;
            targetProvider.directInsertionAvailable = true;
            targetProvider.inserted = true;
            targetProvider.verified = true;
            TextDelivery delivery([&attempts, &results](
                                      const QString &backend,
                                      const OutputSettings &,
                                      PasteMethod) {
                return std::make_unique<FakeBackend>(backend, &attempts, &results);
            }, &targetProvider);

            OutputSettings settings;
            settings.method = method;
            settings.ydotoolEnabled = true;
            Target target;
            target.applicationId = QStringLiteral("org.kde.kate");
            target.category = AppCategory::CodeEditor;

            const DeliveryResult result = delivery.deliver(
                settings,
                makeDeliveryContent(QStringLiteral("insert me"), OutputFormat::PlainText),
                target);

            QCOMPARE(result.receipt, DeliveryReceipt::VerifiedInTarget);
            QCOMPARE(targetProvider.insertCalls, 1);
            QCOMPARE(targetProvider.insertedText, QStringLiteral("insert me"));
            QVERIFY(attempts.isEmpty());
        }
    }

    void outputUsesExplicitGlobalPasteRuleWithoutCapturedTarget()
    {
        QList<QString> attempts;
        QHash<QString, bool> results{{virtualKeyboardMethod(), true}};
        FakeTargetProvider targetProvider;
        targetProvider.focused = false;
        TextDelivery delivery([&attempts, &results](
                                  const QString &method,
                                  const OutputSettings &,
                                  PasteMethod) {
            return std::make_unique<FakeBackend>(method, &attempts, &results);
        }, &targetProvider);

        OutputSettings settings;
        settings.method = QString::fromLatin1(OutputMethod::Automatic);
        settings.ydotoolEnabled = true;
        settings.pasteRules = {
            {PasteRuleScope::Global, QString(), PasteMethod::StandardPaste, true},
        };

        const DeliveryResult result = delivery.deliver(
            settings,
            makeDeliveryContent(QStringLiteral("hello"), OutputFormat::PlainText),
            {});

        QCOMPARE(attempts, QList<QString>({virtualKeyboardMethod()}));
        QCOMPARE(result.receipt, DeliveryReceipt::InputSent);
        QCOMPARE(result.message, QStringLiteral("Copied • Input sent"));
    }

    void outputExplicitMethodDoesNotFallback()
    {
        QList<QString> attempts;
        QHash<QString, bool> results;
        results.insert(QString::fromLatin1(OutputMethod::WlCopy), false);
        results.insert(QString::fromLatin1(OutputMethod::QtClipboard), true);

        TextDelivery delivery([&attempts, &results](
                                  const QString &method,
                                  const OutputSettings &,
                                  PasteMethod) {
            return std::make_unique<FakeBackend>(method, &attempts, &results);
        });

        OutputSettings settings;
        settings.method = QString::fromLatin1(OutputMethod::WlCopy);
        settings.ydotoolEnabled = true;
        const DeliveryResult result = delivery.deliver(
            settings,
            makeDeliveryContent(QStringLiteral("hello"), OutputFormat::PlainText),
            {});
        QVERIFY(result.ok);
        QCOMPARE(result.receipt, DeliveryReceipt::Copied);
        QCOMPARE(attempts, QList<QString>({QString::fromLatin1(OutputMethod::WlCopy)}));
    }

    void outputVerifiedYdotoolReportsVirtualKeyboardOutcome_data()
    {
        QTest::addColumn<bool>("restoreClipboard");
        QTest::addColumn<QString>("expectedMessage");

        QTest::newRow("clipboard kept")
            << false << QStringLiteral("Copied • Input sent");
        QTest::newRow("clipboard restored")
            << true << QStringLiteral("Input sent");
    }

    void outputVerifiedYdotoolReportsVirtualKeyboardOutcome()
    {
        QFETCH(bool, restoreClipboard);
        QFETCH(QString, expectedMessage);

        QApplication::clipboard()->setText(QStringLiteral("previous clipboard"));
        QList<QString> attempts;
        QHash<QString, bool> results{{virtualKeyboardMethod(), true}};
        FakeTargetProvider targetProvider;
        targetProvider.verified = true;
        TextDelivery delivery([&attempts, &results](
                                  const QString &method,
                                  const OutputSettings &,
                                  PasteMethod) {
            return std::make_unique<FakeBackend>(method, &attempts, &results);
        }, &targetProvider);

        OutputSettings settings;
        settings.ydotoolEnabled = true;
        settings.restoreClipboardAfterTyping = restoreClipboard;
        Target target;
        target.applicationId = QStringLiteral("org.kde.kate");
        target.category = AppCategory::CodeEditor;
        const DeliveryResult result = delivery.deliver(
            settings,
            makeDeliveryContent(QStringLiteral("hello"), OutputFormat::PlainText),
            target);

        QVERIFY(result.ok);
        QCOMPARE(result.receipt, DeliveryReceipt::VerifiedInTarget);
        QCOMPARE(result.message, expectedMessage);
        QCOMPARE(attempts, QList<QString>({virtualKeyboardMethod()}));
    }

    void outputDoesNotPasteWhenTargetChanged()
    {
        QList<QString> attempts;
        QHash<QString, bool> results{
            {QString::fromLatin1(OutputMethod::Ydotool), true},
            {QString::fromLatin1(OutputMethod::WlCopy), true},
        };
        FakeTargetProvider targetProvider;
        targetProvider.focused = false;
        TextDelivery delivery([&attempts, &results](
                                  const QString &method,
                                  const OutputSettings &,
                                  PasteMethod) {
            return std::make_unique<FakeBackend>(method, &attempts, &results);
        }, &targetProvider);

        OutputSettings settings;
        settings.ydotoolEnabled = true;
        Target target;
        target.applicationId = QStringLiteral("org.kde.kate");
        target.category = AppCategory::CodeEditor;
        const DeliveryResult result = delivery.deliver(
            settings,
            makeDeliveryContent(QStringLiteral("hello"), OutputFormat::PlainText),
            target);

        QCOMPARE(result.receipt, DeliveryReceipt::Copied);
        QVERIFY(attempts.isEmpty());
    }

    void outputUsesSavedAccessibleTargetAfterFocusChanges()
    {
        QList<QString> attempts;
        QHash<QString, bool> results;
        FakeTargetProvider targetProvider;
        targetProvider.focused = false;
        targetProvider.directInsertionAvailable = true;
        targetProvider.inserted = true;
        targetProvider.verified = true;
        TextDelivery delivery([&attempts, &results](
                                  const QString &method,
                                  const OutputSettings &,
                                  PasteMethod) {
            return std::make_unique<FakeBackend>(method, &attempts, &results);
        }, &targetProvider);

        OutputSettings settings;
        settings.pasteRules = {
            {PasteRuleScope::Application,
             QStringLiteral("org.kde.kate"),
             PasteMethod::DirectInsert,
             true},
            {PasteRuleScope::Global, QString(), PasteMethod::StandardPaste, true},
        };
        Target target;
        target.applicationId = QStringLiteral("org.kde.kate");
        target.category = AppCategory::CodeEditor;
        const DeliveryResult result = delivery.deliver(
            settings,
            makeDeliveryContent(QStringLiteral("insert me"), OutputFormat::PlainText),
            target);

        QCOMPARE(result.receipt, DeliveryReceipt::VerifiedInTarget);
        QCOMPARE(result.message, QStringLiteral("Verified in Target"));
        QCOMPARE(targetProvider.insertCalls, 1);
        QCOMPARE(targetProvider.insertedText, QStringLiteral("insert me"));
        QVERIFY(attempts.isEmpty());
    }

    void outputKeepsCopiedTextWhenAccessibleTargetRejectsInsertion()
    {
        QList<QString> attempts;
        QHash<QString, bool> results;
        FakeTargetProvider targetProvider;
        targetProvider.focused = false;
        targetProvider.directInsertionAvailable = true;
        targetProvider.inserted = false;
        TextDelivery delivery([&attempts, &results](
                                  const QString &method,
                                  const OutputSettings &,
                                  PasteMethod) {
            return std::make_unique<FakeBackend>(method, &attempts, &results);
        }, &targetProvider);

        OutputSettings settings;
        settings.pasteRules = {
            {PasteRuleScope::Application,
             QStringLiteral("org.kde.kate"),
             PasteMethod::DirectInsert,
             true},
            {PasteRuleScope::Global, QString(), PasteMethod::StandardPaste, true},
        };
        Target target;
        target.applicationId = QStringLiteral("org.kde.kate");
        const DeliveryResult result = delivery.deliver(
            settings,
            makeDeliveryContent(QStringLiteral("keep copied"), OutputFormat::PlainText),
            target);

        QCOMPARE(result.receipt, DeliveryReceipt::Copied);
        QCOMPARE(targetProvider.insertCalls, 1);
        QVERIFY(attempts.isEmpty());
    }

    void selectedTextDeliveryRestoresClipboardAfterVerifiedInsertion()
    {
        auto *previous = new QMimeData;
        previous->setText(QStringLiteral("previous clipboard"));
        previous->setHtml(QStringLiteral("<b>previous clipboard</b>"));
        previous->setData(QStringLiteral("image/png"), QByteArrayLiteral("fake-image"));
        QApplication::clipboard()->setMimeData(previous);

        QList<QString> attempts;
        QHash<QString, bool> results{{virtualKeyboardMethod(), true}};
        FakeTargetProvider targetProvider;
        targetProvider.verified = true;
        TextDelivery delivery([&attempts, &results](
                                  const QString &method,
                                  const OutputSettings &,
                                  PasteMethod) {
            return std::make_unique<FakeBackend>(method, &attempts, &results);
        }, &targetProvider);

        OutputSettings settings;
        settings.ydotoolEnabled = true;
        settings.restoreClipboardAfterTyping = true;
        Target target;
        target.applicationId = QStringLiteral("org.kde.kate");
        target.category = AppCategory::CodeEditor;
        target.selectedText = QStringLiteral("replace me");
        target.selectionStart = 7;
        target.selectionEnd = 17;

        const DeliveryResult result = delivery.deliver(
            settings,
            makeDeliveryContent(QStringLiteral("new text"), OutputFormat::Html),
            target);
        QCOMPARE(result.receipt, DeliveryReceipt::VerifiedInTarget);
        const QMimeData *restored = QApplication::clipboard()->mimeData();
        QCOMPARE(restored->text(), QStringLiteral("previous clipboard"));
        QCOMPARE(restored->html(), QStringLiteral("<b>previous clipboard</b>"));
        QCOMPARE(restored->data(QStringLiteral("image/png")), QByteArrayLiteral("fake-image"));
    }

    void outputRestoresClipboardAfterDelayWhenVirtualKeyboardInputCannotBeVerified()
    {
        auto *previous = new QMimeData;
        previous->setText(QStringLiteral("previous clipboard"));
        previous->setHtml(QStringLiteral("<b>previous clipboard</b>"));
        previous->setData(QStringLiteral("application/x-speecher-test"), QByteArrayLiteral("custom-data"));
        QApplication::clipboard()->setMimeData(previous);

        QList<QString> attempts;
        QHash<QString, bool> results{{virtualKeyboardMethod(), true}};
        FakeTargetProvider targetProvider;
        targetProvider.verified = false;
        TextDelivery delivery([&attempts, &results](
                                  const QString &method,
                                  const OutputSettings &,
                                  PasteMethod) {
            return std::make_unique<FakeBackend>(method, &attempts, &results);
        }, &targetProvider);

        OutputSettings settings;
        settings.ydotoolEnabled = true;
        settings.restoreClipboardAfterTyping = true;
        Target target;
        target.applicationId = QStringLiteral("org.kde.kate");
        target.category = AppCategory::CodeEditor;

        QString consumedText;
        QTimer::singleShot(0, [&consumedText] {
            consumedText = QApplication::clipboard()->text();
        });
        const DeliveryResult result = delivery.deliver(
            settings,
            makeDeliveryContent(QStringLiteral("new text"), OutputFormat::Html),
            target);
        QCoreApplication::processEvents();
        QCOMPARE(result.receipt, DeliveryReceipt::InputSent);
        QCOMPARE(result.message, QStringLiteral("Input sent"));
        QCOMPARE(consumedText, QStringLiteral("new text"));
        const QMimeData *clipboard = QApplication::clipboard()->mimeData();
        QCOMPARE(clipboard->text(), QStringLiteral("previous clipboard"));
        QCOMPARE(clipboard->html(), QStringLiteral("<b>previous clipboard</b>"));
        QCOMPARE(clipboard->data(QStringLiteral("application/x-speecher-test")),
                 QByteArrayLiteral("custom-data"));
    }

#ifdef SPEECHER_WITH_WAYLAND
    void wlClipboardSnapshotIncludesPrivateFormats()
    {
        const QStringList offered{
            QStringLiteral("text/plain"),
            QStringLiteral("application/x-libreoffice-embed-source-xml"),
            QStringLiteral("application/x-qt-image"),
        };

        QCOMPARE(WlClipboardDelivery::snapshotMimeTypes(offered), offered);
    }

    void wlClipboardSnapshotCapturesAndRestoresEveryMimeType()
    {
        auto *original = new QMimeData;
        original->setText(QStringLiteral("old clipboard"));
        original->setHtml(QStringLiteral("<i>old clipboard</i>"));
        original->setData(QStringLiteral("image/png"), QByteArrayLiteral("png-bytes"));
        QApplication::clipboard()->setMimeData(original);

        ClipboardSnapshot snapshot;
        QString error;
        QVERIFY2(WlClipboardDelivery::capture(&snapshot, &error), qPrintable(error));
        QVERIFY(snapshot.hasData);
        QVERIFY(snapshot.parts.size() >= 3);

        QApplication::clipboard()->setText(QStringLiteral("replacement"));
        QVERIFY2(WlClipboardDelivery::restore(snapshot, &error), qPrintable(error));
        const QMimeData *restored = QApplication::clipboard()->mimeData();
        QCOMPARE(restored->text(), QStringLiteral("old clipboard"));
        QCOMPARE(restored->html(), QStringLiteral("<i>old clipboard</i>"));
        QCOMPARE(restored->data(QStringLiteral("image/png")), QByteArrayLiteral("png-bytes"));
    }

    void wlClipboardSnapshotRestoresEmptyClipboard()
    {
        QApplication::clipboard()->setMimeData(new QMimeData);
        QCoreApplication::processEvents();

        ClipboardSnapshot snapshot;
        QString error;
        QVERIFY2(WlClipboardDelivery::capture(&snapshot, &error), qPrintable(error));
        QVERIFY(!snapshot.hasData);

        QApplication::clipboard()->setText(QStringLiteral("replacement"));
        QVERIFY2(WlClipboardDelivery::restore(snapshot, &error), qPrintable(error));
        QVERIFY(QApplication::clipboard()->text().isEmpty());
    }

    void ydotoolDeliveryBuildsTypeAndPasteCommands()
    {
        const QString text = QStringLiteral("hello\nworld\n \t");
        const QStringList args = YdotoolDelivery::commandArguments(text);
        QCOMPARE(args,
                 QStringList({QStringLiteral("type"),
                              QStringLiteral("--key-delay=1"),
                              QStringLiteral("--key-hold=2"),
                              QStringLiteral("--escape=0"),
                              QStringLiteral("--"),
                              QStringLiteral("hello\nworld")}));
        QCOMPARE(YdotoolDelivery::withoutTrailingWhitespace(QStringLiteral("one\n\n")), QStringLiteral("one"));
        QCOMPARE(YdotoolDelivery::withoutTrailingWhitespace(QStringLiteral("one\n \t")), QStringLiteral("one"));
        QCOMPARE(YdotoolDelivery::withoutTrailingWhitespace(QStringLiteral(" one\ntwo")), QStringLiteral(" one\ntwo"));
        QVERIFY(!args.contains(QStringLiteral("--file=-")));
        QVERIFY(std::none_of(args.cbegin(), args.cend(), [](const QString &arg) {
            return arg.startsWith(QStringLiteral("--file="));
        }));

        QCOMPARE(YdotoolDelivery::pasteShortcutArguments(PasteMethod::TerminalPaste),
                 QStringList({QStringLiteral("key"),
                              QStringLiteral("--key-delay=2"),
                              QStringLiteral("29:1"),
                              QStringLiteral("42:1"),
                              QStringLiteral("47:1"),
                              QStringLiteral("47:0"),
                              QStringLiteral("42:0"),
                              QStringLiteral("29:0")}));
        QCOMPARE(YdotoolDelivery::pasteShortcutArguments(PasteMethod::StandardPaste),
                 QStringList({QStringLiteral("key"),
                              QStringLiteral("--key-delay=2"),
                              QStringLiteral("29:1"),
                              QStringLiteral("47:1"),
                              QStringLiteral("47:0"),
                              QStringLiteral("29:0")}));
        QCOMPARE(YdotoolDelivery::copyShortcutArguments(PasteMethod::StandardPaste),
                 QStringList({QStringLiteral("key"),
                              QStringLiteral("--key-delay=2"),
                              QStringLiteral("29:1"),
                              QStringLiteral("46:1"),
                              QStringLiteral("46:0"),
                              QStringLiteral("29:0")}));
        QCOMPARE(YdotoolDelivery::copyShortcutArguments(PasteMethod::TerminalPaste),
                 QStringList({QStringLiteral("key"),
                              QStringLiteral("--key-delay=2"),
                              QStringLiteral("29:1"),
                              QStringLiteral("42:1"),
                              QStringLiteral("46:1"),
                              QStringLiteral("46:0"),
                              QStringLiteral("42:0"),
                              QStringLiteral("29:0")}));
    }

    void ydotoolDeliversIdenticalTextTwice()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString executablePath = dir.filePath(QStringLiteral("ydotool"));
        const QString logPath = dir.filePath(QStringLiteral("ydotool.log"));
        QFile executable(executablePath);
        QVERIFY(executable.open(QIODevice::WriteOnly | QIODevice::Text));
        executable.write("#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$SPEECHER_TEST_YDOTOOL_LOG\"\n");
        executable.close();
        QVERIFY(QFile::setPermissions(executablePath,
                                      QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                          | QFileDevice::ExeOwner));

        const QByteArray oldPath = qgetenv("PATH");
        const QByteArray oldRuntimeDir = qgetenv("XDG_RUNTIME_DIR");
        qputenv("PATH", QFile::encodeName(dir.path()) + ':' + oldPath);
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(dir.path()));
        qputenv("SPEECHER_TEST_YDOTOOL_LOG", QFile::encodeName(logPath));
        const QString text = QStringLiteral("repeat-%1").arg(QUuid::createUuid().toString());
        QString error;
        QVERIFY2(YdotoolDelivery().type(text, &error), qPrintable(error));
        QVERIFY2(YdotoolDelivery().type(text, &error), qPrintable(error));
        qputenv("PATH", oldPath);
        qputenv("XDG_RUNTIME_DIR", oldRuntimeDir);
        qunsetenv("SPEECHER_TEST_YDOTOOL_LOG");

        QFile log(logPath);
        QVERIFY(log.open(QIODevice::ReadOnly | QIODevice::Text));
        const QList<QByteArray> calls = log.readAll().split('\n');
        QCOMPARE(std::count_if(calls.cbegin(), calls.cend(), [](const QByteArray &call) {
                     return call.startsWith(QByteArrayLiteral("type "));
                 }),
                 2);
    }

    void ydotoolStatusEvaluation()
    {
        YdotoolProbeFacts facts;
        QCOMPARE(YdotoolSetup::evaluate(facts).state, YdotoolSetupState::NotInstalled);

        facts.ydotoolInstalled = true;
        facts.ydotooldInstalled = true;
        facts.uinputExists = true;
        QCOMPARE(YdotoolSetup::evaluate(facts).state, YdotoolSetupState::NeedsUinputPermission);

        facts.userInConfiguredGroup = true;
        facts.currentSessionInConfiguredGroup = false;
        QCOMPARE(YdotoolSetup::evaluate(facts).state, YdotoolSetupState::NeedsSignOut);

        facts.currentSessionInConfiguredGroup = true;
        facts.uinputReadWrite = true;
        QCOMPARE(YdotoolSetup::evaluate(facts).state, YdotoolSetupState::DaemonNotRunning);

        facts.socketExists = true;
        facts.socketWritable = true;
        QCOMPARE(YdotoolSetup::evaluate(facts).state, YdotoolSetupState::Disabled);

        facts.enabledInSpeecher = true;
        const YdotoolSetupStatus ready = YdotoolSetup::evaluate(facts);
        QCOMPARE(ready.state, YdotoolSetupState::Ready);
        QVERIFY(ready.ready());
    }

    void appImageYdotoolHelperUsesStableVerifiedCopy()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString bundled = QCoreApplication::applicationDirPath()
            + QStringLiteral("/speecher-ydotool-setup");
        QVERIFY2(QFileInfo::exists(bundled), qPrintable(bundled));

        const QByteArray oldAppImage = qgetenv("APPIMAGE");
        const QByteArray oldDataHome = qgetenv("XDG_DATA_HOME");
        const auto restoreEnvironment = qScopeGuard([oldAppImage, oldDataHome] {
            oldAppImage.isNull() ? qunsetenv("APPIMAGE") : qputenv("APPIMAGE", oldAppImage);
            oldDataHome.isNull() ? qunsetenv("XDG_DATA_HOME")
                                 : qputenv("XDG_DATA_HOME", oldDataHome);
        });
        qputenv("APPIMAGE", directory.filePath(QStringLiteral("Speecher.AppImage")).toUtf8());
        qputenv("XDG_DATA_HOME", directory.filePath(QStringLiteral("data")).toUtf8());

        const QString expected = directory.filePath(
            QStringLiteral("data/speecher/libexec/speecher-ydotool-setup"));
        QString error;
        const QString actual = YdotoolSetup::helperPath(&error);
        QVERIFY2(actual == expected, qPrintable(error));
        QFile bundledFile(bundled);
        QFile stableFile(expected);
        QVERIFY(bundledFile.open(QIODevice::ReadOnly));
        QVERIFY(stableFile.open(QIODevice::ReadOnly));
        QCOMPARE(stableFile.readAll(), bundledFile.readAll());
        struct stat helperStatus {};
        struct stat directoryStatus {};
        QCOMPARE(::stat(QFile::encodeName(expected).constData(), &helperStatus), 0);
        QCOMPARE(::stat(QFile::encodeName(QFileInfo(expected).dir().absolutePath()).constData(),
                        &directoryStatus),
                 0);
        QCOMPARE(helperStatus.st_mode & 0777, mode_t(0500));
        QCOMPARE(directoryStatus.st_mode & 0777, mode_t(0700));
    }
#endif // SPEECHER_WITH_WAYLAND
};

int runDeliveryTests(int argc, char **argv)
{
    DeliveryTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_delivery.moc"
