#include "common/test_suites.h"
#include "common/audio_input_waits.h"
#include "common/delivery_waits.h"
#include "app/PlatformComposition.h"

#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#include "dictation/DictationPorts.h"
#include "output/ClipboardDelivery.h"
#include "output/TextDelivery.h"
#include "platform/audio/QtAudioInput.h"
#include "platform/win/WinCaptureInput.h"
#include "platform/win/WinCommunicationsAudioInput.h"
#include "platform/win/WinGlobalShortcutBinder.h"
#include "platform/win/WinInjectedInput.h"
#include "platform/win/WinSingleKeyShortcutBinder.h"
#include "platform/win/WinSystemAudioInput.h"
#include "platform/win/WinTargetProvider.h"
#include "recording/SystemAudioPresentation.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <windows.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <atomic>
#include <cstring>
#include <memory>

using namespace speecher;

namespace {

bool interactiveWindowStation()
{
    USEROBJECTFLAGS station{};
    return GetUserObjectInformationW(GetProcessWindowStation(), UOI_FLAGS,
                                     &station, sizeof(station), nullptr)
        && (station.dwFlags & WSF_VISIBLE);
}

// Errors worded as the bare reason, for inputs whose wording no test reads.
QString bareReason(const QString &reason)
{
    return reason;
}

constexpr CaptureWording kBareWording{bareReason, bareReason};

// Hands over one byte, the count of reads so far, at each read.
class CountingCapture final : public WinCaptureStream {
public:
    explicit CountingCapture(std::atomic<int> *reads)
        : m_reads(reads)
    {
    }
    QString open() override { return {}; }
    QString read(QList<QByteArray> *chunks) override
    {
        chunks->append(QByteArray(1, char(++*m_reads)));
        return {};
    }

private:
    std::atomic<int> *m_reads;
};

// Hands over three chunks at each read, each of them its capture's number
// and its place in that capture.
class BatchCapture final : public WinCaptureStream {
public:
    explicit BatchCapture(char capture)
        : m_capture(capture)
    {
    }
    QString open() override { return {}; }
    QString read(QList<QByteArray> *chunks) override
    {
        for (int chunk = 0; chunk < 3; ++chunk) {
            chunks->append(QByteArray().append(m_capture).append(char(++m_sent)));
        }
        return {};
    }

private:
    char m_capture;
    int m_sent = 0;
};

// The default output of a StandInEnumerator.
class StandInOutput final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IMMDevice> {
public:
    STDMETHODIMP Activate(REFIID, DWORD, PROPVARIANT *, void **) override { return E_NOTIMPL; }
    STDMETHODIMP OpenPropertyStore(DWORD, IPropertyStore **) override { return E_NOTIMPL; }
    STDMETHODIMP GetId(LPWSTR *id) override
    {
        static constexpr wchar_t kId[] = L"output";
        *id = static_cast<LPWSTR>(CoTaskMemAlloc(sizeof(kId)));
        if (!*id) {
            return E_OUTOFMEMORY;
        }
        std::memcpy(*id, kId, sizeof(kId));
        return S_OK;
    }
    STDMETHODIMP GetState(DWORD *state) override
    {
        *state = DEVICE_STATE_ACTIVE;
        return S_OK;
    }
};

// Has one output, which never changes.
class StandInEnumerator final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          IMMDeviceEnumerator> {
public:
    STDMETHODIMP EnumAudioEndpoints(EDataFlow, DWORD, IMMDeviceCollection **) override { return E_NOTIMPL; }
    STDMETHODIMP GetDefaultAudioEndpoint(EDataFlow, ERole, IMMDevice **device) override
    {
        return Microsoft::WRL::Make<StandInOutput>().CopyTo(device);
    }
    STDMETHODIMP GetDevice(LPCWSTR, IMMDevice **) override { return E_NOTIMPL; }
    STDMETHODIMP RegisterEndpointNotificationCallback(IMMNotificationClient *) override { return S_OK; }
    STDMETHODIMP UnregisterEndpointNotificationCallback(IMMNotificationClient *) override { return S_OK; }
};

// An echo canceller that refuses its first failures references, and counts
// every try.
class StandInEchoControl final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          IAcousticEchoCancellationControl> {
public:
    explicit StandInEchoControl(int failures)
        : m_failures(failures)
    {
    }
    STDMETHODIMP SetEchoCancellationRenderEndpoint(LPCWSTR) override { return ++sets > m_failures ? S_OK : E_FAIL; }

    std::atomic<int> sets = 0;

private:
    const int m_failures;
};

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
        SettingsStore settings;
        WinGlobalShortcutBinder shortcut(&settings);
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
        SettingsStore settings;
        WinGlobalShortcutBinder dictation(&settings);
        QVERIFY(!dictation.setShortcut(bare, &error));

        WinSessionShortcutBinder binder(&settings, GlobalShortcutBinder::actionFor(GlobalShortcutRole(role)));
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
        QCOMPARE(error, QStringLiteral("%1 is already a shortcut in another app.").arg(owned.displayText()));
        binder.suspend();
        QVERIFY(!binder.setShortcut(owned, &error));
        QCOMPARE(binder.resume(), QString());
        QCOMPARE(binder.shortcut().combination(), bare);
        QVERIFY(!binder.m_registered);
        // A key Windows cannot register at all says so, not that it is owned.
        QVERIFY(!binder.setShortcut(QKeySequence(Qt::Key_F12), &error));
        QVERIFY(!WinGlobalShortcutBinder::describesConflict(error));
        QVERIFY(!error.contains(QStringLiteral("already a shortcut in another app")));

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
        SettingsStore settings;
        for (const GlobalShortcutRole role : {GlobalShortcutRole::Cancel, GlobalShortcutRole::Pause}) {
            WinSingleKeyShortcutBinder binder(&settings, GlobalShortcutBinder::actionFor(role));
            QCOMPARE(binder.unsupportedBindingReason(letter),
                     QStringLiteral("Speecher can only watch %1, not take it from other apps, so it would "
                                    "still type while you dictate. Use another key or a key combination.")
                         .arg(letter.displayText()));
            for (const char *code : {"ControlRight", "F13", "CapsLock"}) {
                QCOMPARE(binder.unsupportedBindingReason(
                             ShortcutBinding::singleKey(QString::fromLatin1(code))),
                         QString());
            }
        }
        WinSingleKeyShortcutBinder dictation(&settings);
        QCOMPARE(dictation.unsupportedBindingReason(letter), QString());
    }

    // Test processes run side by side, so the Global Shortcut the platform
    // binds for them leaves the keys to the desktop.
    void testRunsLeaveTheGlobalShortcutKeysFree()
    {
        if (!interactiveWindowStation()) {
            QSKIP("Global Shortcut registration requires an interactive window station");
        }
        const auto keysFree = [] {
            constexpr int probeId = 0x5ee9;
            const bool free = RegisterHotKey(nullptr, probeId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'D');
            UnregisterHotKey(nullptr, probeId);
            return free;
        };
        if (!keysFree()) {
            QSKIP("Another app holds Ctrl+Alt+D");
        }
        // The settings outlast each test, and an earlier one saves another
        // combination.
        SettingsStore settings;
        settings.setGlobalShortcut(GlobalShortcutRole::Dictation, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D));
        const std::unique_ptr<GlobalShortcutBinder> binder(
            platformComposition()->createGlobalShortcutBinder(GlobalShortcutRole::Dictation, &settings, nullptr));
        binder->bind();
        QCOMPARE(binder->shortcut().combination(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D));
        QVERIFY(keysFree());
    }

    void keyboardBreakReleasesSuspendedShortcutOnce()
    {
        SettingsStore settings;
        WinGlobalShortcutBinder shortcut(&settings);
        QSignalSpy deactivated(&shortcut, &GlobalShortcutBinder::deactivated);
        shortcut.m_pressedKey = 'D';
        shortcut.m_pressed = true;
        shortcut.suspend();

        RAWINPUT input{};
        input.header.dwType = RIM_TYPEKEYBOARD;
        input.data.keyboard.VKey = 'D';
        input.data.keyboard.Flags = RI_KEY_BREAK;
        shortcut.handleRawInput(input, 0);

        QCOMPARE(deactivated.count(), 1);
        shortcut.handleRawInput(input, 0);
        QCOMPARE(deactivated.count(), 1);
        QCOMPARE(shortcut.resume(), QString());
    }

    // A combination's press and release dispatched late, as when the event
    // loop was busy opening the microphone, still report the hold between
    // the WM_HOTKEY and the release's WM_INPUT, across the tick count's wrap.
    void combinationShortcutTimesTheHoldByItsMessages()
    {
        SettingsStore settings;
        WinGlobalShortcutBinder shortcut(&settings);
        QSignalSpy deactivated(&shortcut, &GlobalShortcutBinder::deactivated);
        shortcut.m_hotKeyId = 0x7000;

        MSG hotKey{};
        hotKey.message = WM_HOTKEY;
        hotKey.wParam = 0x7000;
        hotKey.lParam = MAKELPARAM(MOD_CONTROL | MOD_ALT, 'D');
        hotKey.time = 0xFFFFFFF0;
        shortcut.nativeEventFilter(QByteArrayLiteral("windows_generic_MSG"), &hotKey, nullptr);

        RAWINPUT input{};
        input.header.dwType = RIM_TYPEKEYBOARD;
        input.data.keyboard.VKey = 'D';
        input.data.keyboard.Flags = RI_KEY_BREAK;
        shortcut.handleRawInput(input, 0x40);

        QCOMPARE(deactivated.count(), 1);
        QCOMPARE(deactivated.first().at(0).toLongLong(), 80);
        shortcut.m_hotKeyId = 0;
    }

    // The physical identity is the scancode plus the E0 byte: on AltGr
    // layouts right Alt is preceded by a synthetic left-Ctrl make (0x1D),
    // which must not read as the bound key, and repeated makes without a
    // break are key repeat, not a second press.
    void singleKeyBinderMatchesScancodeIdentity()
    {
        SettingsStore settings;
        WinSingleKeyShortcutBinder binder(&settings);
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

    // A press and release dispatched late, as when the event loop was busy,
    // still report the hold between their WM_INPUT messages.
    void singleKeyBinderTimesTheHoldByItsMessages()
    {
        SettingsStore settings;
        WinSingleKeyShortcutBinder binder(&settings);
        QString error;
        QVERIFY2(binder.setShortcut(ShortcutBinding::singleKey(QStringLiteral("F13")), &error),
                 qPrintable(error));
        QSignalSpy deactivated(&binder, &GlobalShortcutBinder::deactivated);

        RAWINPUT input{};
        input.header.dwType = RIM_TYPEKEYBOARD;
        input.data.keyboard.MakeCode = 0x64;
        input.data.keyboard.VKey = VK_F13;
        binder.handleRawInput(input, 1000);
        input.data.keyboard.Flags = RI_KEY_BREAK;
        binder.handleRawInput(input, 1080);

        QCOMPARE(deactivated.count(), 1);
        QCOMPARE(deactivated.first().at(0).toLongLong(), 80);
        QVERIFY2(binder.setShortcut({}, &error), qPrintable(error));
    }

    // Raw input spells NumLock as a bare 0x45 and Pause as an E1-flagged
    // 0x1D plus a fake VKey-255 half; the binder normalizes both to the
    // vocabulary's message-level spelling.
    void singleKeyBinderNormalizesThePauseNumLockQuirk()
    {
        SettingsStore settings;
        WinSingleKeyShortcutBinder binder(&settings);
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
        SettingsStore settings;
        WinSingleKeyShortcutBinder binder(&settings);
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
        const ClipboardCaptured captured = test::captureAndWait(clipboard);
        QVERIFY2(captured.ok, qPrintable(captured.error));
        QApplication::clipboard()->setText(QStringLiteral("after"));
        const ClipboardRestored putBack = test::restoreAndWait(clipboard, captured.snapshot);
        QVERIFY2(putBack.ok, qPrintable(putBack.error));

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
        const bool started = test::startAndWait(capture, &error);
        if (started) {
            QVERIFY(capture.isActive());
            // With or without anything playing, the stream keeps time.
            QTRY_VERIFY(pcm.size() >= 16000 * 2 / 2);
            QVERIFY(test::stopAndWait(capture));
        } else {
            // Runners without a sound output end here.
            qInfo().noquote() << "system audio did not start:" << error;
            QVERIFY(!error.isEmpty());
        }
        QVERIFY(!capture.isActive());
        // A failed start is reported once.
        QTest::qWait(300);
        QCOMPARE(failed.count(), started ? 0 : 1);
    }

    // record stop ends the last utterance on what the microphone heard up to
    // the stop and its post-roll.
    void captureDeliversItsLastAudioBeforeStopped()
    {
        std::atomic<int> reads = 0;
        QByteArray heard;
        const auto counting = [&reads] { return std::make_unique<CountingCapture>(&reads); };

        WinCaptureInput capture(kBareWording, counting, 0);
        connect(&capture, &AudioInput::audioChunk, &capture, [&](const QByteArray &chunk) { heard += chunk; });
        QVERIFY(test::startAndWait(capture));
        // Stopped before its first poll, it still reads what the stream holds.
        QVERIFY(test::stopAndWait(capture));
        QVERIFY(reads >= 1);
        QCOMPARE(heard.size(), reads.load());
        QCOMPARE(heard.back(), char(reads.load()));

        reads = 0;
        heard.clear();
        WinCaptureInput rolling(kBareWording, counting, 250);
        connect(&rolling, &AudioInput::audioChunk, &rolling, [&](const QByteArray &chunk) { heard += chunk; });
        QVERIFY(test::startAndWait(rolling));
        QVERIFY(test::stopAndWait(rolling));
        // The post-roll's polls and the last read.
        QVERIFY2(reads >= 2, qPrintable(QString::number(reads)));
        QCOMPARE(heard.size(), reads.load());
        QCOMPARE(heard.back(), char(reads.load()));
        QTest::qWait(250);
        QCOMPARE(heard.size(), reads.load());
    }

    // A handler of the stop's last drained audio that aborts hears nothing
    // more from that capture, stopped() included.
    void captureAbortedByItsLastAudioSaysNoMore()
    {
        std::atomic<int> reads = 0;
        WinCaptureInput capture(kBareWording, [&reads] { return std::make_unique<CountingCapture>(&reads); }, 0);
        QSignalSpy stopped(&capture, &AudioInput::stopped);
        bool stopping = false;
        connect(&capture, &AudioInput::audioChunk, &capture, [&] {
            if (stopping) {
                capture.abort();
            }
        });
        QVERIFY(test::startAndWait(capture));
        stopping = true;
        // Before the first poll, so the drain is what the abort hears.
        capture.stop();
        QTest::qWait(200);

        QCOMPARE(stopped.count(), 0);
        QVERIFY(!capture.isActive());
    }

    // A handler that stops in the middle of a read's chunks gets the rest of
    // the old capture, in order, before stopped(); a start there goes on.
    void captureStoppedFromItsAudioDeliversTheRestFirst()
    {
        std::atomic<char> captures = 0;
        WinCaptureInput capture(kBareWording, [&captures] { return std::make_unique<BatchCapture>(++captures); }, 0);
        QList<QByteArray> heard;
        qsizetype heardBeforeRestart = 0;
        bool restarted = false;
        connect(&capture, &AudioInput::audioChunk, &capture, [&](const QByteArray &chunk) {
            heard.append(chunk);
            if (heard.size() == 1) {
                capture.stop();
            }
        });
        connect(&capture, &AudioInput::stopped, &capture, [&] {
            if (!std::exchange(restarted, true)) {
                heardBeforeRestart = heard.size();
                capture.start();
            }
        });
        QVERIFY(test::startAndWait(capture));
        QTRY_VERIFY(heardBeforeRestart > 0 && heard.size() >= heardBeforeRestart + 3);
        QVERIFY(test::stopAndWait(capture));

        QVERIFY(restarted);
        // The stop's last read follows the first read's three chunks.
        QVERIFY(heardBeforeRestart >= 6);
        for (qsizetype index = 0; index < heard.size(); ++index) {
            const bool first = index < heardBeforeRestart;
            QCOMPARE(heard[index][0], char(first ? 1 : 2));
            QCOMPARE(heard[index][1], char(first ? index + 1 : index - heardBeforeRestart + 1));
        }
    }

    void defaultOutputWatcherTakesConsoleOutputChanges()
    {
        const auto watcher = Microsoft::WRL::Make<DefaultOutputWatcher>();
        QVERIFY(watcher && watcher->changed());
        QVERIFY(!watcher->takeChange());
        watcher->OnDefaultDeviceChanged(eRender, eCommunications, nullptr);
        watcher->OnDefaultDeviceChanged(eCapture, eConsole, nullptr);
        QVERIFY(!watcher->takeChange());
        watcher->OnDefaultDeviceChanged(eRender, eConsole, nullptr);
        QVERIFY(watcher->takeChange());
        QVERIFY(!watcher->takeChange());
    }

    // A reference Windows refused is set again at the next update, without
    // waiting for the default output to change, and says why until then.
    void echoReferenceRetriesARefusedReference()
    {
        const auto enumerator = Microsoft::WRL::Make<StandInEnumerator>();
        const auto control = Microsoft::WRL::Make<StandInEchoControl>(1);
        EchoReferenceFollower follower(enumerator.Get(), control.Get());
        QCOMPARE(control->sets.load(), 1);
        QCOMPARE(follower.warning(), echoCancellationNoReferenceText(quint32(E_FAIL)));
        QVERIFY2(follower.warning().contains(QStringLiteral("0x80004005")), qPrintable(follower.warning()));

        follower.update();
        QCOMPARE(control->sets.load(), 2);
        QCOMPARE(follower.warning(), QString());
        follower.update();
        QCOMPARE(control->sets.load(), 2);
    }

    void recordingMicrophoneFindsEachListedMicrophone()
    {
        const QList<AudioInputDeviceInfo> microphones = QtAudioInput::availableInputDevices();
        if (microphones.isEmpty()) {
            QSKIP("There is no microphone");
        }
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const auto uninitialize = qScopeGuard([apartment] {
            if (SUCCEEDED(apartment)) {
                CoUninitialize();
            }
        });
        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
        QVERIFY(SUCCEEDED(CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enumerator))));
        for (const AudioInputDeviceInfo &microphone : microphones) {
            const std::wstring id = microphoneEndpointId(microphone.id).toStdWString();
            Microsoft::WRL::ComPtr<IMMDevice> device;
            QVERIFY2(SUCCEEDED(enumerator->GetDevice(id.c_str(), &device)), qPrintable(microphone.label));
        }
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

    // A Recording takes system audio as "them" beside the communications
    // microphone, whose echo Windows cancels, so Speecher adds no canceller
    // and has no warning of its own.
    void aRecordingTakesSystemAudioWithoutSpeechersCanceller()
    {
        QVERIFY(platformComposition()->recordsSystemAudio());
        QString warning;
        QVERIFY(!platformComposition()->createEchoCanceller(&warning));
        QCOMPARE(warning, QString());
    }

    void recordingMicrophoneStartsOrSaysWhy()
    {
        WinCommunicationsAudioInput microphone(AudioCaptureSettings{});
        QByteArray pcm;
        connect(&microphone, &AudioInput::audioChunk, &microphone, [&](const QByteArray &chunk) { pcm += chunk; });
        QSignalSpy failed(&microphone, &AudioInput::failed);
        QString error;
        const bool started = test::startAndWait(microphone, &error);
        if (started) {
            QVERIFY(microphone.isActive());
            qInfo().noquote() << "echo cancellation:"
                              << (microphone.echoCancellationWarning().isEmpty() ? QStringLiteral("on")
                                                                                 : microphone.echoCancellationWarning());
            // Half a second of it.
            QTRY_VERIFY(pcm.size() >= 16000 * 2 / 2);
            QVERIFY(test::stopAndWait(microphone));
        } else {
            // Runners without a microphone end here.
            qInfo().noquote() << "recording microphone did not start:" << error;
            QVERIFY(!error.isEmpty());
        }
        QVERIFY(!microphone.isActive());
        QTest::qWait(300);
        QCOMPARE(failed.count(), started ? 0 : 1);
    }

    // The recording microphone's reads keep its echo reference on the
    // default output, here by setting again one Windows refused at open.
    void recordingMicrophoneReadsUpdateItsEchoReference()
    {
        if (QMediaDevices::defaultAudioOutput().isNull()) {
            QSKIP("There is no sound output");
        }
        const auto control = Microsoft::WRL::Make<StandInEchoControl>(1);
        WinCommunicationsAudioInput microphone(AudioCaptureSettings{}, [control](IAudioClient *) {
            return Microsoft::WRL::ComPtr<IAcousticEchoCancellationControl>(control);
        });
        QString error;
        if (!test::startAndWait(microphone, &error)) {
            QSKIP(qPrintable(QStringLiteral("The recording microphone did not start: %1").arg(error)));
        }
        QTRY_COMPARE(control->sets.load(), 2);
        QVERIFY(test::stopAndWait(microphone));
    }

    void liveHotkeyTargetAndDeliveryToNotepad()
    {
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_WIN_DESKTOP") != QStringLiteral("1")) {
            QSKIP("Live Windows desktop check is opt-in");
        }

        SettingsStore settings;
        WinGlobalShortcutBinder shortcut(&settings);
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
        const Target target = test::captureAndWait(targetProvider);
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
        TextDelivery delivery(&targetProvider);
        const DeliveryResult result = test::deliverAndWait(
            delivery, output, makeDeliveryContent(inserted, OutputFormat::PlainText), target);
        QVERIFY2(result.ok, qPrintable(result.message));
        QVERIFY(result.receipt == DeliveryReceipt::VerifiedInTarget
                || result.receipt == DeliveryReceipt::InputSent);
        if (result.receipt == DeliveryReceipt::InputSent) {
            QTest::qWait(500);
            QVERIFY(test::verifyInsertionAndWait(targetProvider, target, inserted));
        }
        qInfo().noquote() << "windows live delivery text=" + inserted;
    }

    // A capture while verification waits starts the next dictation's
    // delivery, so the old verification gives up even when the new capture
    // is the same control and the text it inserts is the text expected.
    void liveCaptureDuringVerificationEndsIt()
    {
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_WIN_RECAPTURE") != QStringLiteral("1")) {
            QSKIP("Live Windows recapture check is opt-in; focus an editable control in another app");
        }
        WinTargetProvider provider;
        provider.setCorrectionObservationEnabled(false);
        const Target target = test::captureAndWait(provider);
        QVERIFY(provider.canInsertText(target));
        const QString inserted = QStringLiteral("late text ");
        QTimer::singleShot(0, &provider, [&provider, &inserted] {
            const Target next = test::captureAndWait(provider);
            provider.insertText(next, inserted);
        });
        QVERIFY(!test::verifyInsertionAndWait(provider, target, inserted));
    }
};

int runWinPlatformTests(int argc, char **argv)
{
    WinPlatformTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_win_platform.moc"
