#include "common/test_prelude.h"

#include "platform/audio/AudioPcmConverter.h"
#include "providers/PcmWav.h"

#include <QAudioSink>
#include <QBuffer>
#include <QMediaDevices>
#include <QScopeGuard>

#include <cmath>
#include <memory>

#ifdef SPEECHER_WITH_PULSE
#include <QProcess>

#include <pulse/pulseaudio.h>

#include <optional>
#endif

#ifdef Q_OS_WIN
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>

#include <string>
#endif

using namespace speecher;

namespace {

// Plays a second of 440 Hz at half scale, an RMS of about 0.35 before the
// output's volume, through output.
bool playTone(const QAudioDevice &output)
{
    // WASAPI takes only the output's own rate and channels, in float.
    QAudioFormat format = output.preferredFormat();
    format.setSampleFormat(QAudioFormat::Float);
    if (!output.isFormatSupported(format)) {
        return false;
    }
    QByteArray tone;
    for (int frame = 0; frame < format.sampleRate(); ++frame) {
        const auto value = float(0.5 * std::sin(2 * M_PI * 440 * frame / format.sampleRate()));
        for (int channel = 0; channel < format.channelCount(); ++channel) {
            tone.append(reinterpret_cast<const char *>(&value), sizeof(value));
        }
    }
    QBuffer toneBuffer(&tone);
    toneBuffer.open(QIODevice::ReadOnly);
    QAudioSink sink(output, format);
    sink.start(&toneBuffer);
    return QTest::qWaitFor([&] { return sink.state() == QAudio::IdleState; }, 5000);
}

// The loudest 100 ms of 16 kHz PCM.
float loudestRms(const QByteArray &pcm)
{
    float loudest = 0.0f;
    for (qsizetype at = 0; at < pcm.size(); at += 3200) {
        loudest = qMax(loudest, rmsForPcm16(pcm.mid(at, 3200)));
    }
    return loudest;
}

} // namespace

#ifdef Q_OS_WIN
namespace {

// Windows has no public API to change the default output, so this declares
// the interface its Sound settings use, as far as SetDefaultEndpoint.
struct __declspec(uuid("f8679f50-850a-41cf-9c72-430f290290c8")) IPolicyConfig : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX **) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX **) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX *, WAVEFORMATEX *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, INT, const PROPERTYKEY &, PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, INT, const PROPERTYKEY &, PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR deviceId, ERole role) = 0;
};
class __declspec(uuid("870af99c-171d-4f9e-af0d-e63df40c2bc9")) PolicyConfigClient;

// Makes the output Qt names by id the default for every role, as Sound
// settings does.
bool setDefaultOutput(const QByteArray &id)
{
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto uninitialize = qScopeGuard([apartment] {
        if (SUCCEEDED(apartment)) {
            CoUninitialize();
        }
    });
    Microsoft::WRL::ComPtr<IPolicyConfig> policy;
    if (FAILED(CoCreateInstance(__uuidof(PolicyConfigClient), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&policy)))) {
        return false;
    }
    const std::wstring device = QString::fromUtf8(id).toStdWString();
    for (const ERole role : {eConsole, eMultimedia, eCommunications}) {
        if (FAILED(policy->SetDefaultEndpoint(device.c_str(), role))) {
            return false;
        }
    }
    return true;
}

} // namespace
#endif

#ifdef SPEECHER_WITH_PULSE
namespace {

// pactl's output, or nothing when it fails, as it does with no sound server.
std::optional<QString> pactl(const QStringList &arguments)
{
    QProcess process;
    process.start(QStringLiteral("pactl"), arguments);
    if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return std::nullopt;
    }
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

QStringList pactlLines(const QStringList &arguments)
{
    return pactl(arguments).value_or(QString()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

// The name of the source each recording stream reads, by stream index.
QHash<QString, QString> recordedSources()
{
    QHash<QString, QString> sourceNames;
    for (const QString &line : pactlLines({QStringLiteral("list"), QStringLiteral("short"), QStringLiteral("sources")})) {
        const QStringList fields = line.split(QLatin1Char('\t'));
        sourceNames.insert(fields.value(0), fields.value(1));
    }
    QHash<QString, QString> sources;
    for (const QString &line :
         pactlLines({QStringLiteral("list"), QStringLiteral("short"), QStringLiteral("source-outputs")})) {
        const QStringList fields = line.split(QLatin1Char('\t'));
        sources.insert(fields.value(0), sourceNames.value(fields.value(1)));
    }
    return sources;
}

bool isRecorded(const QString &source)
{
    return recordedSources().values().contains(source);
}

// pactl cannot end a single stream, so this asks the server itself.
bool killSourceOutput(uint32_t index)
{
    pa_mainloop *mainloop = pa_mainloop_new();
    pa_context *context = pa_context_new(pa_mainloop_get_api(mainloop), "Speecher test");
    const auto release = qScopeGuard([&] {
        pa_context_disconnect(context);
        pa_context_unref(context);
        pa_mainloop_free(mainloop);
    });
    if (pa_context_connect(context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
        return false;
    }
    for (pa_context_state_t state; (state = pa_context_get_state(context)) != PA_CONTEXT_READY;) {
        if (!PA_CONTEXT_IS_GOOD(state) || pa_mainloop_iterate(mainloop, 1, nullptr) < 0) {
            return false;
        }
    }
    bool killed = false;
    pa_operation *operation = pa_context_kill_source_output(
        context,
        index,
        [](pa_context *, int success, void *killed) { *static_cast<bool *>(killed) = success; },
        &killed);
    if (!operation) {
        return false;
    }
    while (pa_operation_get_state(operation) == PA_OPERATION_RUNNING && pa_mainloop_iterate(mainloop, 1, nullptr) >= 0) {
    }
    pa_operation_unref(operation);
    return killed;
}

// Two null sinks to move the default output between, the first of them the
// default until they are unloaded and the previous default restored.
class NullSinks {
public:
    NullSinks()
        : m_previousDefault(pactl({QStringLiteral("get-default-sink")}).value_or(QString()))
    {
        for (const QString &name : {first, second}) {
            if (const std::optional<QString> module = pactl(
                    {QStringLiteral("load-module"), QStringLiteral("module-null-sink"), QStringLiteral("sink_name=") + name})) {
                m_modules.append(*module);
            }
        }
        m_ready = m_modules.size() == 2 && pactl({QStringLiteral("set-default-sink"), first});
    }
    ~NullSinks()
    {
        if (!m_previousDefault.isEmpty()) {
            pactl({QStringLiteral("set-default-sink"), m_previousDefault});
        }
        for (const QString &module : std::as_const(m_modules)) {
            pactl({QStringLiteral("unload-module"), module});
        }
    }
    Q_DISABLE_COPY_MOVE(NullSinks)

    bool isReady() const { return m_ready; }

    const QString first = QStringLiteral("speecher_test_%1_first").arg(QCoreApplication::applicationPid());
    const QString second = QStringLiteral("speecher_test_%1_second").arg(QCoreApplication::applicationPid());
    // PulseAudio and pipewire-pulse both name a sink's monitor source this way.
    const QString firstMonitor = first + QStringLiteral(".monitor");
    const QString secondMonitor = second + QStringLiteral(".monitor");

private:
    QString m_previousDefault;
    QStringList m_modules;
    bool m_ready = false;
};

} // namespace
#endif


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
        const QString profile = resolveWritingProfile(
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
                        profile,
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

#ifdef SPEECHER_WITH_PULSE
    void systemAudioCaptureFailsWithoutSoundServer()
    {
        QTemporaryDir dir;
        const QByteArray previousServer = qgetenv("PULSE_SERVER");
        qputenv("PULSE_SERVER", "unix:" + QFile::encodeName(dir.filePath(QStringLiteral("missing"))));
        const auto restoreServer = qScopeGuard([&] {
            previousServer.isNull() ? qunsetenv("PULSE_SERVER") : qputenv("PULSE_SERVER", previousServer);
        });

        std::unique_ptr<AudioInput> capture(platformComposition()->createSystemAudioInput(nullptr));
        QVERIFY(capture);
        QSignalSpy failed(capture.get(), &AudioInput::failed);
        QString error;
        QVERIFY(!capture->start(&error));
        QVERIFY(error.startsWith(QStringLiteral("Could not connect to the sound server")));
        QVERIFY(!capture->isActive());
        // The failed start is reported once, by its return value.
        QTest::qWait(100);
        QCOMPARE(failed.count(), 0);
    }

    void systemAudioCaptureFollowsDefaultOutput()
    {
        // It moves the real default output, so it never runs on a desktop by accident.
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_PULSE") != QStringLiteral("1")) {
            QSKIP("Live default-output switching check is opt-in");
        }
        QVERIFY2(pactl({QStringLiteral("info")}), "No sound server, or no pactl to change its default output");
        NullSinks sinks;
        QVERIFY(sinks.isReady());
        std::unique_ptr<AudioInput> capture(platformComposition()->createSystemAudioInput(nullptr));
        QSignalSpy failed(capture.get(), &AudioInput::failed);
        QString error;
        QVERIFY2(capture->start(&error), qPrintable(error));
        QTRY_VERIFY(isRecorded(sinks.firstMonitor));

        QVERIFY(pactl({QStringLiteral("set-default-sink"), sinks.second}));
        QTRY_VERIFY(isRecorded(sinks.secondMonitor));
        QVERIFY(!isRecorded(sinks.firstMonitor));
        QVERIFY(capture->isActive());
        QCOMPARE(failed.count(), 0);
    }

    void systemAudioCaptureIgnoresFailureOfReplacedStream()
    {
        // It moves the real default output, so it never runs on a desktop by accident.
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_PULSE") != QStringLiteral("1")) {
            QSKIP("Live default-output switching check is opt-in");
        }
        QVERIFY2(pactl({QStringLiteral("info")}), "No sound server, or no pactl to change its default output");
        NullSinks sinks;
        QVERIFY(sinks.isReady());
        std::unique_ptr<AudioInput> capture(platformComposition()->createSystemAudioInput(nullptr));
        QSignalSpy failed(capture.get(), &AudioInput::failed);
        QString error;
        QVERIFY2(capture->start(&error), qPrintable(error));
        QTRY_VERIFY(isRecorded(sinks.firstMonitor));

        // Nothing here handles events, so the killed stream's failure reaches
        // the input only after the new default output's stream replaced it.
        QVERIFY(killSourceOutput(recordedSources().key(sinks.firstMonitor).toUInt()));
        QVERIFY(pactl({QStringLiteral("set-default-sink"), sinks.second}));
        QDeadlineTimer deadline(5000);
        while (!isRecorded(sinks.secondMonitor) && !deadline.hasExpired()) {
            QThread::msleep(50);
        }
        QVERIFY(isRecorded(sinks.secondMonitor));

        QTest::qWait(500);
        QCOMPARE(failed.count(), 0);
        QVERIFY(capture->isActive());
        QVERIFY(isRecorded(sinks.secondMonitor));
    }
#endif

    // Records SPEECHER_TEST_LIVE_SYSTEM_AUDIO_SECONDS (default 5) of what the
    // speakers play to the WAV that SPEECHER_TEST_LIVE_SYSTEM_AUDIO names.
    void liveSystemAudioCapture()
    {
        const QString wavPath = qEnvironmentVariable("SPEECHER_TEST_LIVE_SYSTEM_AUDIO");
        if (wavPath.isEmpty()) {
            QSKIP("Live system audio capture is opt-in");
        }
        bool secondsSet = false;
        int seconds = qEnvironmentVariableIntValue("SPEECHER_TEST_LIVE_SYSTEM_AUDIO_SECONDS", &secondsSet);
        if (!secondsSet) {
            seconds = 5;
        }

        std::unique_ptr<AudioInput> capture(platformComposition()->createSystemAudioInput(nullptr));
        QVERIFY2(capture, "This platform has no system audio capture");
        QByteArray pcm;
        connect(capture.get(), &AudioInput::audioChunk, capture.get(), [&](const QByteArray &chunk) { pcm += chunk; });
        QSignalSpy failed(capture.get(), &AudioInput::failed);
        QString error;
        QVERIFY2(capture->start(&error), qPrintable(error));
        QTest::qWait(seconds * 1000);
        capture->stop();

        QCOMPARE(failed.count(), 0);
        QFile wav(wavPath);
        QVERIFY(wav.open(QIODevice::WriteOnly));
        wav.write(wavFromPcm16Mono(pcm, 16000));
        // Allow for the server's buffering at either end.
        QVERIFY2(pcm.size() >= (seconds - 1) * 16000 * 2, qPrintable(QString::number(pcm.size())));
    }

    // Plays a second of 440 Hz at half scale through the default output and
    // checks system audio heard it.
    void liveSystemAudioHearsTone()
    {
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_SYSTEM_AUDIO_TONE") != QStringLiteral("1")) {
            QSKIP("Live system audio tone check is opt-in");
        }
        const QAudioDevice output = QMediaDevices::defaultAudioOutput();
        QVERIFY2(!output.isNull(), "There is no sound output to play the tone through");

        std::unique_ptr<AudioInput> capture(platformComposition()->createSystemAudioInput(nullptr));
        QVERIFY2(capture, "This platform has no system audio capture");
        QByteArray pcm;
        connect(capture.get(), &AudioInput::audioChunk, capture.get(), [&](const QByteArray &chunk) { pcm += chunk; });
        QSignalSpy failed(capture.get(), &AudioInput::failed);
        QString error;
        QVERIFY2(capture->start(&error), qPrintable(error));
        QVERIFY(playTone(output));
        // Allow for the capture's poll and the output's latency.
        QTest::qWait(500);
        capture->stop();

        QCOMPARE(failed.count(), 0);
        QVERIFY2(loudestRms(pcm) > 0.05f, qPrintable(QString::number(loudestRms(pcm))));
    }

#ifdef Q_OS_WIN
    void liveSystemAudioFollowsDefaultOutput()
    {
        // It moves the real default output, so it never runs on a desktop by accident.
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_SYSTEM_AUDIO_SWITCH") != QStringLiteral("1")) {
            QSKIP("Live default-output switching check is opt-in");
        }
        const QAudioDevice first = QMediaDevices::defaultAudioOutput();
        QAudioDevice second;
        for (const QAudioDevice &output : QMediaDevices::audioOutputs()) {
            if (output.id() != first.id()) {
                second = output;
            }
        }
        QVERIFY2(!first.isNull() && !second.isNull(), "Switching needs two sound outputs");
        const auto restore = qScopeGuard([&] { setDefaultOutput(first.id()); });

        std::unique_ptr<AudioInput> capture(platformComposition()->createSystemAudioInput(nullptr));
        QByteArray pcm;
        connect(capture.get(), &AudioInput::audioChunk, capture.get(), [&](const QByteArray &chunk) { pcm += chunk; });
        QSignalSpy failed(capture.get(), &AudioInput::failed);
        QString error;
        QVERIFY2(capture->start(&error), qPrintable(error));

        // Only the default output is captured.
        QVERIFY(playTone(second));
        QTest::qWait(500);
        QVERIFY2(loudestRms(pcm) < 0.01f, qPrintable(QString::number(loudestRms(pcm))));

        QVERIFY(setDefaultOutput(second.id()));
        QTest::qWait(500);
        pcm.clear();
        QVERIFY(playTone(second));
        QTest::qWait(500);
        QVERIFY2(loudestRms(pcm) > 0.05f, qPrintable(QString::number(loudestRms(pcm))));
        QVERIFY(capture->isActive());
        QCOMPARE(failed.count(), 0);
    }
#endif

#ifdef SPEECHER_WITH_WAYLAND
    void liveAtSpiVerificationRequiresActualInsertion()
    {
        if (qEnvironmentVariable("SPEECHER_TEST_LIVE_ATSPI_VERIFY") != QStringLiteral("1")) {
            QSKIP("Live AT-SPI verification fixture is opt-in");
        }
        AtSpiTargetProvider provider;
        provider.setCorrectionObservationEnabled(false);
        const Target target = provider.capture();
        QVERIFY(target.hasIdentity());
        QVERIFY(provider.canInsertText(target));
        QVERIFY(!provider.verifyInsertion(target, QStringLiteral("text")));
        const QString inserted = QString::fromUtf8("🌍 text ");
        QString error;
        QVERIFY2(provider.insertText(target, inserted, &error), qPrintable(error));
        QVERIFY(provider.verifyInsertion(target, inserted));
        // Another matching occurrence and a different value at the anchor
        // cannot stand in for the requested insertion.
        QVERIFY(!provider.verifyInsertion(target, QStringLiteral("suffix text")));
        QVERIFY(!provider.verifyInsertion(target, QStringLiteral("text")));
    }

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
