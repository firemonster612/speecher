#include "common/test_suites.h"

#include "core/OutputMethod.h"
#include "core/settings/SettingsSchema.h"
#include "dictation/DictationPorts.h"
#include "output/ClipboardDelivery.h"
#include "output/TextDelivery.h"
#include "platform/win/WinCommunicationsAudioInput.h"
#include "platform/win/WinGlobalShortcutBinder.h"
#include "platform/win/WinInjectedInput.h"
#include "platform/win/WinSingleKeyShortcutBinder.h"
#include "platform/win/WinSystemAudioInput.h"
#include "platform/win/WinTargetProvider.h"
#include "recording/RecordingPresentation.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>

#include <windows.h>
#include <ks.h>
#include <ksmedia.h>

using namespace speecher;

namespace {

const SettingsRow &rowById(const SettingsPage &page, const QString &id)
{
    for (const SettingsSection &section : page.sections) {
        for (const SettingsRow &row : section.rows) {
            if (row.id == id) {
                return row;
            }
        }
    }
    qFatal("no row %s", qPrintable(id));
}

bool hasRow(const SettingsPage &page, const QString &id)
{
    for (const SettingsSection &section : page.sections) {
        for (const SettingsRow &row : section.rows) {
            if (row.id == id) {
                return true;
            }
        }
    }
    return false;
}

bool interactiveWindowStation()
{
    USEROBJECTFLAGS station{};
    return GetUserObjectInformationW(GetProcessWindowStation(), UOI_FLAGS,
                                     &station, sizeof(station), nullptr)
        && (station.dwFlags & WSF_VISIBLE);
}

SchemaContext context()
{
    return {
        {{QStringLiteral("claude"), QStringLiteral("Claude Voice")}},
        {{QStringLiteral("openai"), QStringLiteral("OpenAI"), true}},
        [] { return QList<RowOption>{}; },
    };
}

} // namespace

class WinPlatformTests : public QObject {
    Q_OBJECT

private slots:
    void globalShortcutMappingRoundTrips()
    {
        const QList<QKeySequence> shortcuts{
            QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D),
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Space),
            QKeySequence(Qt::ALT | Qt::Key_F5),
            // Punctuation maps through the active layout; comma sits on
            // VK_OEM_COMMA on effectively every layout.
            QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Comma),
            QKeySequence(Qt::CTRL | Qt::Key_Return),
            // Navigation keys, which session shortcuts may also use bare.
            QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Home),
            QKeySequence(Qt::SHIFT | Qt::Key_PageDown),
            QKeySequence(Qt::Key_Delete),
            QKeySequence(Qt::Key_Left),
        };
        for (const QKeySequence &shortcut : shortcuts) {
            QString error;
            const auto hotKey = WinGlobalShortcutBinder::nativeHotKey(shortcut, &error, true);
            QVERIFY2(hotKey.has_value(), qPrintable(error));
            QCOMPARE(WinGlobalShortcutBinder::keySequenceForHotKey(
                         hotKey->modifiers, hotKey->virtualKey),
                     shortcut);
        }
    }

    void suspendReleasesHotkeyUntilLastResume()
    {
        if (!interactiveWindowStation()) {
            QSKIP("Global Shortcut registration requires an interactive window station");
        }
        WinGlobalShortcutBinder shortcut;
        QString error;
        QVERIFY2(shortcut.setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F23), &error),
                 qPrintable(error));
        QVERIFY(shortcut.m_hotKeyId != 0);

        shortcut.suspend();
        shortcut.suspend();
        QCOMPARE(shortcut.m_hotKeyId, 0);

        // A rebind while suspended validates but leaves the keys available.
        QVERIFY2(shortcut.setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F24), &error),
                 qPrintable(error));
        QCOMPARE(shortcut.m_hotKeyId, 0);

        QCOMPARE(shortcut.resume(), QString());
        QCOMPARE(shortcut.m_hotKeyId, 0);
        QCOMPARE(shortcut.resume(), QString());
        QVERIFY(shortcut.m_hotKeyId != 0);
        QCOMPARE(shortcut.shortcut().combination(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F24));
        QVERIFY(!shortcut.setShortcut(ShortcutBinding::singleKey(QStringLiteral("AltRight")), &error));
        QVERIFY(!error.isEmpty());

        // The router parks the hot key while a single key holds the binding;
        // the registration must actually go, and a later resume must not
        // sneak it back.
        shortcut.suspend();
        QVERIFY(shortcut.removeRegistration());
        QCOMPARE(shortcut.resume(), QString());
        QCOMPARE(shortcut.m_hotKeyId, 0);
    }

    void sessionShortcutHoldsItsHotKeyOnlyWhileArmed_data()
    {
        QTest::addColumn<int>("role");
        QTest::newRow("cancel") << int(GlobalShortcutRole::Cancel);
        QTest::newRow("pause") << int(GlobalShortcutRole::Pause);
    }

    // The Cancel and Pause Shortcuts hold their hot key only while armed, so
    // a bare key types as usual between sessions. The dictation shortcut is
    // held for good and still needs a modifier.
    void sessionShortcutHoldsItsHotKeyOnlyWhileArmed()
    {
        if (!interactiveWindowStation()) {
            QSKIP("Global Shortcut registration requires an interactive window station");
        }
        QFETCH(int, role);
        const QKeySequence bare(Qt::Key_F22);
        QString error;
        WinGlobalShortcutBinder dictation;
        QVERIFY(!dictation.setShortcut(bare, &error));

        WinSessionShortcutBinder binder(GlobalShortcutBinder::actionFor(GlobalShortcutRole(role)));
        QVERIFY2(binder.setShortcut(bare, &error), qPrintable(error));
        QVERIFY(!binder.m_registered);
        binder.setArmed(true);
        QVERIFY(binder.m_registered);
        // Recording a replacement mid-session still needs the keys free.
        binder.suspend();
        QVERIFY(!binder.m_registered);
        QCOMPARE(binder.resume(), QString());
        QVERIFY(binder.m_registered);
        binder.setArmed(false);
        QVERIFY(!binder.m_registered);

        // A key another app owns is refused while it is being chosen,
        // although nothing would be held until the next session, and also
        // while suspended, as the recorder binds before it resumes.
        QVERIFY(RegisterHotKey(nullptr, 0x5ee8, MOD_NOREPEAT, VK_F21));
        const auto release = qScopeGuard([] { UnregisterHotKey(nullptr, 0x5ee8); });
        const ShortcutBinding owned{QKeySequence(Qt::Key_F21)};
        QVERIFY(!binder.setShortcut(owned, &error));
        QCOMPARE(error, globalShortcutOwnedElsewhereText(owned));
        binder.suspend();
        QVERIFY(!binder.setShortcut(owned, &error));
        QCOMPARE(binder.resume(), QString());
        QCOMPARE(binder.shortcut().combination(), bare);
        QVERIFY(!binder.m_registered);
        // A key Windows cannot register at all says so, not that it is owned.
        QVERIFY(!binder.setShortcut(QKeySequence(Qt::Key_F12), &error));
        QVERIFY(!WinGlobalShortcutBinder::describesConflict(error));
        QVERIFY(error != globalShortcutOwnedElsewhereText(ShortcutBinding{QKeySequence(Qt::Key_F12)}));

        // A single key took over through the router: arming must not bring
        // the replaced hot key back beside it.
        QVERIFY(binder.removeRegistration());
        binder.setArmed(true);
        QVERIFY(!binder.m_registered);
        QVERIFY(binder.shortcut().isEmpty());
    }

    // Raw input only watches, so a Cancel or Pause key that types would also
    // type into the dictation; keys that cannot type stay allowed.
    void singleKeySessionShortcutRefusesKeysThatType()
    {
        const ShortcutBinding letter = ShortcutBinding::singleKey(QStringLiteral("KeyC"));
        for (const GlobalShortcutRole role : {GlobalShortcutRole::Cancel, GlobalShortcutRole::Pause}) {
            WinSingleKeyShortcutBinder binder(GlobalShortcutBinder::actionFor(role));
            QCOMPARE(binder.unsupportedBindingReason(letter), watchedKeyStillTypesText(letter));
            for (const char *code : {"ControlRight", "F13", "CapsLock"}) {
                QCOMPARE(binder.unsupportedBindingReason(
                             ShortcutBinding::singleKey(QString::fromLatin1(code))),
                         QString());
            }
        }
        WinSingleKeyShortcutBinder dictation;
        QCOMPARE(dictation.unsupportedBindingReason(letter), QString());
    }

    void keyboardBreakReleasesSuspendedShortcutOnce()
    {
        WinGlobalShortcutBinder shortcut;
        QSignalSpy deactivated(&shortcut, &GlobalShortcutBinder::deactivated);
        shortcut.m_pressedKey = 'D';
        shortcut.m_pressed = true;
        shortcut.suspend();

        RAWINPUT input{};
        input.header.dwType = RIM_TYPEKEYBOARD;
        input.data.keyboard.VKey = 'D';
        input.data.keyboard.Flags = RI_KEY_BREAK;
        shortcut.handleRawInput(input);

        QCOMPARE(deactivated.count(), 1);
        shortcut.handleRawInput(input);
        QCOMPARE(deactivated.count(), 1);
        QCOMPARE(shortcut.resume(), QString());
    }

    // The physical identity is the scancode plus the E0 byte: on AltGr
    // layouts right Alt is preceded by a synthetic left-Ctrl make (0x1D),
    // which must not read as the bound key, and repeated makes without a
    // break are key repeat, not a second press.
    void singleKeyBinderMatchesScancodeIdentity()
    {
        WinSingleKeyShortcutBinder binder;
        QString error;
        QVERIFY2(binder.setShortcut(ShortcutBinding::singleKey(QStringLiteral("AltRight")), &error),
                 qPrintable(error));
        QSignalSpy activated(&binder, &GlobalShortcutBinder::activated);
        QSignalSpy deactivated(&binder, &GlobalShortcutBinder::deactivated);

        RAWINPUT input{};
        input.header.dwType = RIM_TYPEKEYBOARD;
        input.data.keyboard.MakeCode = 0x1D;
        input.data.keyboard.VKey = VK_CONTROL;
        binder.handleRawInput(input);
        QCOMPARE(activated.count(), 0);

        input.data.keyboard.MakeCode = 0x38;
        input.data.keyboard.VKey = VK_MENU;
        input.data.keyboard.Flags = RI_KEY_E0;
        binder.handleRawInput(input);
        binder.handleRawInput(input);
        QCOMPARE(activated.count(), 1);

        input.data.keyboard.Flags = RI_KEY_E0 | RI_KEY_BREAK;
        binder.handleRawInput(input);
        QCOMPARE(deactivated.count(), 1);

        // A left-Alt press (same make, no E0) is a different key.
        input.data.keyboard.Flags = 0;
        binder.handleRawInput(input);
        QCOMPARE(activated.count(), 1);
        QVERIFY2(binder.setShortcut({}, &error), qPrintable(error));
    }

    // Raw input spells NumLock as a bare 0x45 and Pause as an E1-flagged
    // 0x1D plus a fake VKey-255 half; the binder normalizes both to the
    // vocabulary's message-level spelling.
    void singleKeyBinderNormalizesThePauseNumLockQuirk()
    {
        WinSingleKeyShortcutBinder binder;
        QString error;
        QVERIFY2(binder.setShortcut(ShortcutBinding::singleKey(QStringLiteral("NumLock")), &error),
                 qPrintable(error));
        QSignalSpy activated(&binder, &GlobalShortcutBinder::activated);

        RAWINPUT input{};
        input.header.dwType = RIM_TYPEKEYBOARD;
        input.data.keyboard.MakeCode = 0x45;
        input.data.keyboard.VKey = VK_NUMLOCK;
        binder.handleRawInput(input);
        QCOMPARE(activated.count(), 1);

        // Pause's trailing half carries the same make code but VKey 255.
        input.data.keyboard.VKey = 0xFF;
        binder.handleRawInput(input);
        QCOMPARE(activated.count(), 1);

        QVERIFY2(binder.setShortcut(ShortcutBinding::singleKey(QStringLiteral("Pause")), &error),
                 qPrintable(error));
        input.data.keyboard.MakeCode = 0x1D;
        input.data.keyboard.VKey = VK_PAUSE;
        input.data.keyboard.Flags = RI_KEY_E1;
        binder.handleRawInput(input);
        QCOMPARE(activated.count(), 2);
        QVERIFY2(binder.setShortcut({}, &error), qPrintable(error));
    }

    // WinPasteDelivery tags its SendInput with dwExtraInfo, which raw input
    // hands back as ExtraInformation: a binding on V or Ctrl must not read
    // Speecher's own paste as the user's finger, while the e2e harness's
    // untagged SendInput must keep firing the binder.
    void singleKeyBinderDropsSpeechersOwnInjection()
    {
        WinSingleKeyShortcutBinder binder;
        QString error;
        QVERIFY2(binder.setShortcut(ShortcutBinding::singleKey(QStringLiteral("KeyV")), &error),
                 qPrintable(error));
        QSignalSpy activated(&binder, &GlobalShortcutBinder::activated);

        RAWINPUT input{};
        input.header.dwType = RIM_TYPEKEYBOARD;
        input.data.keyboard.MakeCode = 0x2F;
        input.data.keyboard.VKey = 'V';
        input.data.keyboard.ExtraInformation = injectedInputTag;
        binder.handleRawInput(input);
        QCOMPARE(activated.count(), 0);

        input.data.keyboard.ExtraInformation = 0;
        binder.handleRawInput(input);
        QCOMPARE(activated.count(), 1);
        QVERIFY2(binder.setShortcut({}, &error), qPrintable(error));
    }

    void qtClipboardSnapshotRestoresFormatsWithoutAManifest()
    {
        auto *original = new QMimeData;
        original->setText(QStringLiteral("before"));
        original->setHtml(QStringLiteral("<b>before</b>"));
        original->setData(QStringLiteral("application/x-speecher-test"),
                          QByteArrayLiteral("private"));
        QApplication::clipboard()->setMimeData(original);

        ClipboardDelivery clipboard;
        ClipboardSnapshot snapshot;
        QString error;
        QVERIFY2(clipboard.capture(&snapshot, &error), qPrintable(error));
        QApplication::clipboard()->setText(QStringLiteral("after"));
        QVERIFY2(clipboard.restore(snapshot, &error), qPrintable(error));

        const QMimeData *restored = QApplication::clipboard()->mimeData();
        QCOMPARE(restored->text(), QStringLiteral("before"));
        QCOMPARE(restored->html(), QStringLiteral("<b>before</b>"));
        QCOMPARE(restored->data(QStringLiteral("application/x-speecher-test")),
                 QByteArrayLiteral("private"));
    }

    void systemAudioReadsMixFormats()
    {
        // The shared mix format Windows gives almost every output.
        WAVEFORMATEXTENSIBLE floatStereo{};
        floatStereo.Format = {WAVE_FORMAT_EXTENSIBLE, 2, 48000, 48000 * 8, 8, 32, 22};
        floatStereo.Samples.wValidBitsPerSample = 32;
        floatStereo.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        QAudioFormat format = audioFormatForWave(floatStereo.Format);
        QCOMPARE(format.sampleFormat(), QAudioFormat::Float);
        QCOMPARE(format.sampleRate(), 48000);
        QCOMPARE(format.channelCount(), 2);

        // 24-bit samples left-justified in 32 bits read as 32-bit ones.
        WAVEFORMATEXTENSIBLE paddedPcm = floatStereo;
        paddedPcm.Samples.wValidBitsPerSample = 24;
        paddedPcm.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        QCOMPARE(audioFormatForWave(paddedPcm.Format).sampleFormat(), QAudioFormat::Int32);

        const WAVEFORMATEX pcm16{WAVE_FORMAT_PCM, 1, 44100, 44100 * 2, 2, 16, 0};
        format = audioFormatForWave(pcm16);
        QCOMPARE(format.sampleFormat(), QAudioFormat::Int16);
        QCOMPARE(format.sampleRate(), 44100);
        QCOMPARE(format.channelCount(), 1);

        const WAVEFORMATEX packed24{WAVE_FORMAT_PCM, 2, 48000, 48000 * 6, 6, 24, 0};
        QVERIFY(!audioFormatForWave(packed24).isValid());
    }

    void systemAudioStartsOrSaysWhy()
    {
        WinSystemAudioInput capture;
        QByteArray pcm;
        connect(&capture, &AudioInput::audioChunk, &capture, [&](const QByteArray &chunk) { pcm += chunk; });
        QSignalSpy failed(&capture, &AudioInput::failed);
        QString error;
        if (capture.start(&error)) {
            QVERIFY(capture.isActive());
            // With or without anything playing, the stream keeps time.
            QTRY_VERIFY(pcm.size() >= 16000 * 2 / 2);
            capture.stop();
        } else {
            // Runners without a sound output end here.
            qInfo().noquote() << "system audio did not start:" << error;
            QVERIFY(!error.isEmpty());
        }
        QVERIFY(!capture.isActive());
        // A failed start is reported once, by its return value.
        QTest::qWait(300);
        QCOMPARE(failed.count(), 0);
    }

    void communicationsWarningSaysWhyEchoIsNotCancelled()
    {
        const AUDIO_EFFECT noiseSuppression{AUDIO_EFFECT_TYPE_NOISE_SUPPRESSION, TRUE, AUDIO_EFFECT_STATE_ON};
        const AUDIO_EFFECT echoOn{AUDIO_EFFECT_TYPE_ACOUSTIC_ECHO_CANCELLATION, FALSE, AUDIO_EFFECT_STATE_ON};
        const AUDIO_EFFECT echoOff{AUDIO_EFFECT_TYPE_ACOUSTIC_ECHO_CANCELLATION, TRUE, AUDIO_EFFECT_STATE_OFF};

        QCOMPARE(communicationsEchoCancellationWarning(S_OK, S_OK, {noiseSuppression, echoOn}), QString());
        QCOMPARE(communicationsEchoCancellationWarning(S_OK, S_OK, {echoOff}), echoCancellationTurnedOffText());
        // Most microphones' drivers have no echo cancellation.
        QCOMPARE(communicationsEchoCancellationWarning(S_OK, S_OK, {}), echoCancellationNotOfferedText());
        QCOMPARE(communicationsEchoCancellationWarning(S_OK, S_OK, {noiseSuppression}),
                 echoCancellationNotOfferedText());
        // A Windows without the effects manager cannot say.
        QCOMPARE(communicationsEchoCancellationWarning(S_OK, E_NOINTERFACE, {}), echoCancellationUnknownText());
        // Without the communications category nothing cancels echo, whatever
        // the effects say.
        const QString noCategory = communicationsEchoCancellationWarning(E_NOINTERFACE, S_OK, {echoOn});
        QCOMPARE(noCategory, echoCancellationNoCallStreamText(quint32(E_NOINTERFACE)));
        QVERIFY2(noCategory.contains(QStringLiteral("0x80004002")), qPrintable(noCategory));
    }

    void recordingMicrophoneStartsOrSaysWhy()
    {
        WinCommunicationsAudioInput microphone(QString{});
        QByteArray pcm;
        connect(&microphone, &AudioInput::audioChunk, &microphone, [&](const QByteArray &chunk) { pcm += chunk; });
        QSignalSpy failed(&microphone, &AudioInput::failed);
        QString error;
        if (microphone.start(&error)) {
            QVERIFY(microphone.isActive());
            qInfo().noquote() << "echo cancellation:"
                              << (microphone.echoCancellationWarning().isEmpty() ? QStringLiteral("on")
                                                                                 : microphone.echoCancellationWarning());
            // Half a second of it.
            QTRY_VERIFY(pcm.size() >= 16000 * 2 / 2);
            microphone.stop();
        } else {
            // Runners without a microphone end here.
            qInfo().noquote() << "recording microphone did not start:" << error;
            QVERIFY(!error.isEmpty());
        }
        QVERIFY(!microphone.isActive());
        QTest::qWait(300);
        QCOMPARE(failed.count(), 0);
    }

    void schemaUsesWindowsCopyAndRows()
    {
        const SettingsSchema schema = buildSettingsSchema(context());
        const SettingsPage &general = schema.page(QStringLiteral("general"));
        QVERIFY(hasRow(general, QStringLiteral("launchAtLogin")));
        // The recorder is a custom row every front end draws, at the top of
        // Dictation.
        QVERIFY(hasRow(general, QStringLiteral("globalShortcut")));
        QVERIFY(!hasRow(general, QStringLiteral("removeSpeecher")));

        const SettingsRow &globalPaste = rowById(
            schema.page(QStringLiteral("output")), QStringLiteral("globalPasteRule"));
        const QList<RowOption> options = globalPaste.options(AppSettings{});
        QCOMPARE(options.at(0).label, QStringLiteral("Standard paste (Ctrl+V)"));
        QCOMPARE(options.at(1).label, QStringLiteral("Terminal paste (Ctrl+Shift+V)"));

        const SettingsRow &rules = rowById(
            schema.page(QStringLiteral("output")), QStringLiteral("applicationPasteRules"));
        QVERIFY(rules.help.contains(QStringLiteral("executable"), Qt::CaseInsensitive));
    }

    void liveHotkeyTargetAndDeliveryToNotepad()
    {
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_WIN_DESKTOP") != QStringLiteral("1")) {
            QSKIP("Live Windows desktop check is opt-in");
        }

        WinGlobalShortcutBinder shortcut;
        QString error;
        QVERIFY2(shortcut.setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D), &error),
                 qPrintable(error));
        QSignalSpy activated(&shortcut, &GlobalShortcutBinder::activated);
        shortcut.bind();

        INPUT input[6]{};
        const WORD keys[]{VK_CONTROL, VK_MENU, 'D', 'D', VK_MENU, VK_CONTROL};
        for (int index = 0; index < 6; ++index) {
            input[index].type = INPUT_KEYBOARD;
            input[index].ki.wVk = keys[index];
            if (index >= 3) {
                input[index].ki.dwFlags = KEYEVENTF_KEYUP;
            }
        }
        QCOMPARE(SendInput(6, input, sizeof(INPUT)), 6U);
        QTRY_COMPARE_WITH_TIMEOUT(activated.count(), 1, 2000);
        qInfo() << "windows live hotkey fired";

        const HWND notepad = FindWindowW(L"Notepad", nullptr);
        QVERIFY(notepad);
        QVERIFY(SetForegroundWindow(notepad));
        QTRY_VERIFY_WITH_TIMEOUT(GetForegroundWindow() == notepad, 2000);

        WinTargetProvider targetProvider;
        const Target target = targetProvider.capture();
        qInfo().noquote() << QStringLiteral("windows live target process=%1 appId=%2 role=%3")
                                 .arg(target.processName, target.applicationId, target.role);
        QCOMPARE(target.processName.toLower(), QStringLiteral("notepad.exe"));
        QVERIFY(target.role.contains(QStringLiteral("document"), Qt::CaseInsensitive));

        OutputSettings output;
        output.method = QString::fromLatin1(OutputMethod::Automatic);
        output.restoreClipboardAfterTyping = false;
        output.pasteRules = {
            {PasteRuleScope::Global, QString(), PasteMethod::StandardPaste, true},
        };
        const QString inserted = QStringLiteral("Speecher Windows delivery verified");
        const DeliveryResult result = TextDelivery(&targetProvider).deliver(
            output, makeDeliveryContent(inserted, OutputFormat::PlainText), target);
        QVERIFY2(result.ok, qPrintable(result.message));
        QVERIFY(result.receipt == DeliveryReceipt::VerifiedInTarget
                || result.receipt == DeliveryReceipt::InputSent);
        if (result.receipt == DeliveryReceipt::InputSent) {
            QTest::qWait(500);
            QVERIFY(targetProvider.verifyInsertion(target, inserted));
        }
        qInfo().noquote() << "windows live delivery text=" + inserted;
    }
};

int runWinPlatformTests(int argc, char **argv)
{
    WinPlatformTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_win_platform.moc"
