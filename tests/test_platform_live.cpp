#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_auth.h"

using namespace speecher::test;


class PlatformLiveTests : public QObject {
    Q_OBJECT

private slots:
#ifdef SPEECHER_WITH_WAYLAND
    void liveAtSpiTargetCapture()
    {
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_ATSPI") != QStringLiteral("1")) {
            QSKIP("Live Plasma AT-SPI check is opt-in");
        }
        AtSpiTargetProvider provider;
        const Target target = provider.capture();
        const AppSettings settings = SettingsStore().snapshot();
        const WritingProfile profile = resolveWritingProfile(
            target,
            settings.refinement.writingProfileOverrides,
            writingProfileFromName(settings.refinement.defaultWritingProfile));
        const PasteRule pasteRule = resolvePasteRule(settings.output.pasteRules, target);
        const bool directInsert = provider.canInsertText(target);
        qInfo().noquote()
            << QStringLiteral("target appId=%1 appName=%2 process=%3 role=%4 category=%5 profile=%6 accessible=%7 secure=%8 focused=%9 directInsert=%10 titleChars=%11 urlChars=%12 controlChars=%13 caret=%14 selectionStart=%15 selectionEnd=%16 selectedChars=%17 before=%18 after=%19 pasteScope=%20 pasteMethod=%21 correctionEligible=%22")
                   .arg(target.applicationId,
                        target.applicationName,
                        target.processName,
                        target.role,
                        appCategoryName(target.category),
                        writingProfileName(profile),
                        target.accessible ? QStringLiteral("yes") : QStringLiteral("no"),
                        target.secure ? QStringLiteral("yes") : QStringLiteral("no"),
                        provider.stillFocused(target) ? QStringLiteral("yes") : QStringLiteral("no"))
                   .arg(directInsert ? QStringLiteral("yes") : QStringLiteral("no"),
                        QString::number(target.windowTitle.size()),
                        QString::number(target.documentUrl.size()),
                        QString::number(target.controlName.size()),
                        QString::number(target.caretOffset),
                        QString::number(target.selectionStart),
                        QString::number(target.selectionEnd),
                        QString::number(target.selectedText.size()),
                        QString::number(target.nearbyTextBefore.size()),
                        QString::number(target.nearbyTextAfter.size()),
                        pasteRuleScopeName(pasteRule.scope),
                        pasteMethodName(pasteRule.method),
                        target.accessible && !target.secure && directInsert && target.caretOffset >= 0
                            ? QStringLiteral("yes")
                            : QStringLiteral("no"));
        QVERIFY2(target.hasIdentity(), "No focused AT-SPI target was found");
        QVERIFY(!target.applicationName.isEmpty() || !target.processName.isEmpty());
        QVERIFY(target.nearbyTextBefore.size() <= 240);
        QVERIFY(target.nearbyTextAfter.size() <= 240);
        if (target.secure) {
            QVERIFY(target.nearbyTextBefore.isEmpty());
            QVERIFY(target.nearbyTextAfter.isEmpty());
        }
    }

#endif // SPEECHER_WITH_WAYLAND

    void liveAudioCaptureUsesDefaultWhenSavedDeviceIsMissing()
    {
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_AUDIO") != QStringLiteral("1")) {
            QSKIP("Live Plasma audio-capture check is opt-in");
        }

        SettingsStore settings;
        settings.raw().clear();
        AudioCaptureSettings audio = settings.audioCaptureSettings();
        audio.deviceId = QStringLiteral("missing-live-test-device");
        audio.mode = QStringLiteral("on_demand");
        audio.vadEnabled = false;
        settings.setAudioCaptureSettings(audio);

        QtAudioInput capture(audio);
        QSignalSpy chunks(&capture, &AudioInput::audioChunk);
        QSignalSpy failed(&capture, &AudioInput::failed);
        QString error;
        QVERIFY2(capture.start(&error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(!chunks.isEmpty(), 2000);
        QCOMPARE(failed.count(), 0);
        const QByteArray pcm = chunks.first().first().toByteArray();
        QVERIFY(!pcm.isEmpty());
        QCOMPARE(pcm.size() % int(sizeof(qint16)), 0);
        capture.stop();
        QVERIFY(!capture.isActive());
    }

#ifdef SPEECHER_WITH_WAYLAND
    void liveAtSpiDirectInsertionIntoSavedUnfocusedControl()
    {
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_ATSPI_EDIT") != QStringLiteral("1")) {
            QSKIP("Live Plasma AT-SPI direct-edit check is opt-in");
        }

        AtSpiTargetProvider provider;
        const Target target = provider.capture();
        QVERIFY2(target.hasIdentity(), "No focused external AT-SPI target was found");
        QVERIFY2(target.role.contains(QStringLiteral("text"), Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("Unexpected focused target: %1 / %2 / %3")
                                .arg(target.applicationName, target.processName, target.role)));
        QVERIFY2(provider.canInsertText(target), "The focused external target is not directly editable");

        qInfo().noquote() << "captured external edit target; change focus now";
        QTest::qWait(2500);
        QVERIFY(!provider.stillFocused(target));

        QString error;
        QVERIFY2(provider.insertText(target, QStringLiteral("inserted"), &error), qPrintable(error));
        QVERIFY(provider.verifyInsertion(target, QStringLiteral("inserted")));
    }

    void livePlasmaDeliveryToFocusedControl()
    {
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_DELIVERY") != QStringLiteral("1")) {
            QSKIP("Live Plasma delivery check is opt-in");
        }

        AtSpiTargetProvider provider;
        const Target target = provider.capture();
        QVERIFY2(target.hasIdentity(), "No focused Plasma target was found");
        QVERIFY2(!target.secure, "Live delivery refuses secure targets");

        OutputSettings output = SettingsStore().snapshot().output;
        output.method = QString::fromLatin1(OutputMethod::Automatic);
        output.ydotoolEnabled = true;
        output.restoreClipboardAfterTyping = true;
        output.pasteRules = defaultPasteRules();
        const PasteRule rule = resolvePasteRule(output.pasteRules, target);

        ClipboardSnapshot before;
        const bool capturedBefore = WlClipboardDelivery::capture(&before);
        TextDelivery delivery(&provider);
        const DeliveryResult result = delivery.deliver(
            output,
            makeDeliveryContent(QStringLiteral(" Speecher matrix insertion "), OutputFormat::Html),
            target);
        QVERIFY2(result.ok, qPrintable(result.message));
        QVERIFY(result.receipt != DeliveryReceipt::None);

        ClipboardSnapshot after;
        const bool capturedAfter = WlClipboardDelivery::capture(&after);
        const bool restored = capturedBefore
            && capturedAfter
            && result.receipt == DeliveryReceipt::VerifiedInTarget
            && before.hasData == after.hasData
            && std::all_of(
                before.parts.cbegin(),
                before.parts.cend(),
                [&after](const ClipboardMimePart &expected) {
                    return std::any_of(
                        after.parts.cbegin(),
                        after.parts.cend(),
                        [&expected](const ClipboardMimePart &actual) {
                            return actual.mimeType == expected.mimeType
                                && actual.data == expected.data;
                        });
                });
        const bool plainAvailable = std::any_of(
            after.parts.cbegin(),
            after.parts.cend(),
            [](const ClipboardMimePart &part) {
                return part.mimeType.startsWith(QStringLiteral("text/plain"));
            });
        const bool htmlAvailable = std::any_of(
            after.parts.cbegin(),
            after.parts.cend(),
            [](const ClipboardMimePart &part) {
                return part.mimeType == QStringLiteral("text/html");
            });
        qInfo().noquote()
            << QStringLiteral("delivery appId=%1 category=%2 pasteScope=%3 pasteMethod=%4 receipt=%5 downgraded=%6 clipboardRestored=%7 clipboardPlain=%8 clipboardHtml=%9")
                   .arg(target.applicationId,
                        appCategoryName(target.category),
                        pasteRuleScopeName(rule.scope),
                        pasteMethodName(rule.method),
                        result.message,
                        result.formatDowngraded ? QStringLiteral("yes") : QStringLiteral("no"),
                        restored ? QStringLiteral("yes") : QStringLiteral("no"),
                        plainAvailable ? QStringLiteral("yes") : QStringLiteral("no"),
                        htmlAvailable ? QStringLiteral("yes") : QStringLiteral("no"));
    }

    void liveWaylandClipboardOffersDistinctFormatsAndRestores()
    {
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_CLIPBOARD") != QStringLiteral("1")) {
            QSKIP("Live Wayland clipboard check is opt-in");
        }

        ClipboardSnapshot original;
        QString error;
        QVERIFY2(WlClipboardDelivery::capture(&original, &error), qPrintable(error));
        struct OriginalClipboardRestorer {
            ClipboardSnapshot snapshot;
            ~OriginalClipboardRestorer()
            {
                QString ignored;
                WlClipboardDelivery::restore(snapshot, &ignored);
            }
        } restorer{original};

        const DeliveryContent content{
            QStringLiteral("Speecher plain clipboard probe"),
            QStringLiteral("<p><strong>Speecher HTML clipboard probe</strong></p>"),
        };
        WlClipboardDelivery clipboard;
        bool htmlAvailable = false;
        QVERIFY2(clipboard.copy(content, &htmlAvailable, &error), qPrintable(error));
        QVERIFY(htmlAvailable);

        ClipboardSnapshot published;
        QVERIFY2(WlClipboardDelivery::capture(&published, &error), qPrintable(error));
        const auto part = [&published](const QString &mimeType) {
            return std::find_if(
                published.parts.cbegin(),
                published.parts.cend(),
                [&mimeType](const ClipboardMimePart &candidate) {
                    return candidate.mimeType == mimeType;
                });
        };
        const auto plain = part(QStringLiteral("text/plain;charset=utf-8"));
        const auto html = part(QStringLiteral("text/html"));
        QVERIFY(plain != published.parts.cend());
        QVERIFY(html != published.parts.cend());
        QCOMPARE(plain->data, content.plainText.toUtf8());
        QCOMPARE(html->data, content.html->toUtf8());
        QVERIFY(plain->data != html->data);

        QVERIFY2(WlClipboardDelivery::restore(original, &error), qPrintable(error));
        ClipboardSnapshot restored;
        QVERIFY2(WlClipboardDelivery::capture(&restored, &error), qPrintable(error));
        QCOMPARE(restored.hasData, original.hasData);
        for (const ClipboardMimePart &expected : std::as_const(original.parts)) {
            const bool matched = std::any_of(
                restored.parts.cbegin(),
                restored.parts.cend(),
                [&expected](const ClipboardMimePart &actual) {
                    return actual.mimeType == expected.mimeType
                        && actual.data == expected.data;
                });
            if (!matched) {
                QStringList actualParts;
                for (const ClipboardMimePart &actual : std::as_const(restored.parts)) {
                    actualParts.append(QStringLiteral("%1:%2")
                                           .arg(actual.mimeType)
                                           .arg(actual.data.size()));
                }
                qWarning().noquote()
                    << QStringLiteral("clipboard restore mismatch expected=%1:%2 actual=%3")
                           .arg(expected.mimeType)
                           .arg(expected.data.size())
                           .arg(actualParts.join(QLatin1Char(',')));
            }
            QVERIFY(matched);
        }
    }

    void liveWaylandClipboardFallbackCopiesPlainText()
    {
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_CLIPBOARD_FALLBACK")
            != QStringLiteral("1")) {
            QSKIP("Live Wayland clipboard fallback check is opt-in");
        }

        WlClipboardDelivery clipboard;
        bool htmlAvailable = true;
        QString error;
        QVERIFY2(clipboard.copy(makeDeliveryContent(QStringLiteral("Speecher clipboard probe"),
                                                    OutputFormat::PlainText),
                                &htmlAvailable,
                                &error),
                 qPrintable(error));
        QVERIFY(!htmlAvailable);

        QString copiedText;
        QVERIFY2(WlClipboardDelivery::readText(&copiedText, &error), qPrintable(error));
        QCOMPARE(copiedText, QStringLiteral("Speecher clipboard probe"));
    }

    void liveAtSpiPasswordTargetIsPrivate()
    {
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_ATSPI_PASSWORD") != QStringLiteral("1")) {
            QSKIP("Live Plasma AT-SPI password check is opt-in");
        }

        AtSpiTargetProvider provider;
        const Target target = provider.capture();
        QVERIFY2(target.hasIdentity(), "No focused password target was found");
        QVERIFY(target.secure);
        QVERIFY(target.nearbyTextBefore.isEmpty());
        QVERIFY(target.nearbyTextAfter.isEmpty());
        QVERIFY(!provider.canInsertText(target));
    }

    void liveSecureTargetUsesClipboardOnly()
    {
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_ATSPI_PASSWORD") != QStringLiteral("1")) {
            QSKIP("Live Plasma secure-delivery check is opt-in");
        }

        AtSpiTargetProvider provider;
        const Target target = provider.capture();
        QVERIFY2(target.hasIdentity(), "No focused password target was found");
        QVERIFY(target.secure);

        OutputSettings output;
        output.method = QString::fromLatin1(OutputMethod::Automatic);
        output.ydotoolEnabled = true;
        output.restoreClipboardAfterTyping = true;
        output.pasteRules = defaultPasteRules();
        QSignalSpy corrections(&provider, &TargetProvider::correctionObserved);
        TextDelivery delivery(&provider);
        const DeliveryResult result = delivery.deliver(
            output,
            makeDeliveryContent(QStringLiteral("Speecher secure-target probe"),
                                OutputFormat::Html),
            target);

        QVERIFY2(result.ok, qPrintable(result.message));
        QCOMPARE(result.receipt, DeliveryReceipt::Copied);
        QCOMPARE(result.message, QStringLiteral("Copied"));
        QCOMPARE(corrections.count(), 0);
        QVERIFY(!provider.canInsertText(target));
    }
#endif // SPEECHER_WITH_WAYLAND
};

int runPlatformLiveTests(int argc, char **argv)
{
    PlatformLiveTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_platform_live.moc"
