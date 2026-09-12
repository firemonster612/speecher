#include "common/test_suites.h"

#include <QApplication>
#include <QDebug>
#include <QScopeGuard>
#include <QStandardPaths>
#ifdef Q_OS_MACOS
#include <QSettings>
#include <QTemporaryDir>
#include "core/SettingsStore.h"
#endif

#ifdef Q_OS_WIN
#include <QUuid>
#include <windows.h>
#endif

#ifdef SPEECHER_WITH_WINUI
#include "frontend/win/WinUiHost.h"
#include <winrt/base.h>
#endif

int main(int argc, char **argv)
{
#ifdef Q_OS_MACOS
    // Test mode alone does not redirect macOS CFPreferences. Keep every test
    // QSettings instance away from the user's native preferences.
    QTemporaryDir preferences;
    if (!preferences.isValid()) {
        qCritical() << "Could not create isolated macOS test preferences";
        return 1;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, preferences.path());
#endif
#ifdef SPEECHER_WITH_WINUI
    std::unique_ptr<speecher::WinUiHost> winUiHost;
    try {
        winUiHost = std::make_unique<speecher::WinUiHost>();
    } catch (const winrt::hresult_error &error) {
        qCritical() << "WinUI test host failed:" << QString::fromWCharArray(error.message().c_str());
        return 1;
    }
#endif
#ifdef Q_OS_WIN
    // Bootstrap WinUI against the real profile first. Redirect later registry
    // access, including native Startup Apps writes, for this test process only.
    const std::wstring testRegistryPath =
        (QStringLiteral("Software\\SpeecherTests-")
         + QUuid::createUuid().toString(QUuid::Id128)).toStdWString();
    HKEY testRegistry = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, testRegistryPath.c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &testRegistry,
                        nullptr) != ERROR_SUCCESS) {
        qCritical() << "Could not create isolated Windows test registry";
        return 1;
    }
    const auto registryCleanup = qScopeGuard([&] {
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        RegCloseKey(testRegistry);
        RegDeleteTreeW(HKEY_CURRENT_USER, testRegistryPath.c_str());
    });
    if (RegOverridePredefKey(HKEY_CURRENT_USER, testRegistry) != ERROR_SUCCESS) {
        qCritical() << "Could not isolate Windows test registry";
        return 1;
    }
#endif
    QApplication app(argc, argv);
#ifdef SPEECHER_WITH_WINUI
    if (winUiHost) {
        winUiHost->installNativeEventFilter();
    }
    const auto winUiShutdown = qScopeGuard([&winUiHost] {
        if (winUiHost) {
            winUiHost->shutdown();
        }
    });
#endif
    QStandardPaths::setTestModeEnabled(true);
#ifdef Q_OS_MACOS
    // Refuse to run mutating suites if the production store ignores isolation.
    speecher::SettingsStore isolatedSettings;
    if (isolatedSettings.raw().format() != QSettings::IniFormat) {
        qCritical() << "Tests must not use native macOS preferences";
        return 1;
    }
#endif
    QString selectedSuite;
    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]) == "--suite" && i + 1 < argc) {
            selectedSuite = QString::fromLocal8Bit(argv[i + 1]);
            for (int j = i; j + 2 <= argc; ++j) argv[j] = argv[j + 2];
            argc -= 2;
            break;
        }
    }
    setTestArguments(argc, argv);

    if (qEnvironmentVariable("SPEECHER_TEST_ONLY_PLATFORM_LIVE") == QStringLiteral("1")) {
        return runPlatformLiveTests(argc, argv);
    }
#ifdef Q_OS_WIN
    if (qEnvironmentVariable("SPEECHER_TEST_ONLY_WIN_PLATFORM") == QStringLiteral("1")) {
        return runWinPlatformTests(argc, argv);
    }
#endif
#ifdef SPEECHER_WITH_WINUI
    if (qEnvironmentVariable("SPEECHER_TEST_ONLY_WIN_FRONTEND") == QStringLiteral("1")) {
        if (!winUiHost) {
            return 1;
        }
        return runWinFrontEndTests(argc, argv, std::move(winUiHost));
    }
#endif

    int result = 0;
    bool matched = selectedSuite.isEmpty();
    const auto selected = [&](const char *name) {
        if (!selectedSuite.isEmpty() && selectedSuite != QLatin1String(name)) return false;
        matched = true;
        return true;
    };
    if (selected("ui")) result |= runUiTests(argc, argv);
    if (selected("app_window")) result |= runAppWindowTests(argc, argv);
    if (selected("transcript_state")) result |= runTranscriptStateTests(argc, argv);
    if (selected("bindings")) result |= runBindingsTests(argc, argv);
    if (selected("settings")) result |= runSettingsTests(argc, argv);
    if (selected("settings_schema")) result |= runSettingsSchemaTests(argc, argv);
    if (selected("provider_registry")) result |= runProviderRegistryTests(argc, argv);
    if (selected("platform_composition")) result |= runPlatformCompositionTests(argc, argv);
#ifdef Q_OS_LINUX
    if (selected("linux_style_choice")) result |= runLinuxStyleChoiceTests(argc, argv);
    if (selected("linux_tray")) result |= runLinuxTrayTests(argc, argv);
    if (selected("keywatch")) result |= runKeywatchTests(argc, argv);
    if (selected("helper_install")) result |= runHelperInstallTests(argc, argv);
#endif
    if (selected("platform_live")) result |= runPlatformLiveTests(argc, argv);
    if (selected("single_instance_ipc")) result |= runSingleInstanceIpcTests(argc, argv);
    if (selected("delivery")) result |= runDeliveryTests(argc, argv);
    if (selected("dictation_session_lifecycle")) result |= runDictationSessionLifecycleTests(argc, argv);
    if (selected("dictation_session_refinement")) result |= runDictationSessionRefinementTests(argc, argv);
    if (selected("refiners")) result |= runRefinersTests(argc, argv);
    if (selected("vocabulary")) result |= runVocabularyTests(argc, argv);
    if (selected("provider_auth")) result |= runProviderAuthTests(argc, argv);
    if (selected("claude_voice")) result |= runClaudeVoiceTests(argc, argv);
    if (selected("codex_dictation")) result |= runCodexDictationTests(argc, argv);
    if (selected("audio_pcm_converter")) result |= runAudioPcmConverterTests(argc, argv);
#ifdef Q_OS_UNIX
    if (selected("update_controller")) result |= runUpdateControllerTests(argc, argv);
#endif
#ifdef SPEECHER_WITH_SWIFT_UI
    if (selected("mac_front_end")) result |= runMacFrontEndTests(argc, argv);
#endif
#ifdef Q_OS_WIN
    if (selected("win_platform")) result |= runWinPlatformTests(argc, argv);
#endif
#ifdef SPEECHER_WITH_WINUI
    if (winUiHost) {
        if (selected("win_front_end")) result |= runWinFrontEndTests(argc, argv, std::move(winUiHost));
    }
#endif
    if (!matched) qCritical() << "Unknown test suite:" << selectedSuite;
    return matched ? result : 1;
}
