// The field table: every AppSettings field once, with how a snapshot reads it
// from the settings file and how a saved draft writes it back. It drives
// SettingsCodecs::snapshot and writeSnapshot, and tells SettingsStore::edit
// which fields it can't store.
#include "core/settings/SettingsCodecs.h"

#include "core/ProviderChain.h"
#include "core/VocabularyScope.h"
#include "core/settings/SettingsKeys.h"

#include <QDebug>

#include <functional>
#include <type_traits>

namespace speecher {

struct SettingsCodecs::Field {
    // Its path in AppSettings, for the message when an edit changes a field
    // that isn't stored.
    const char *name;
    // Fills the field of a snapshot from the file. Empty for the test hook,
    // which a snapshot leaves at its default.
    std::function<void(const SettingsCodecs &, AppSettings &)> read;
    // Saves the field of a draft. Empty for a field Speecher never stores.
    std::function<void(SettingsCodecs &, const AppSettings &)> write;
    std::function<bool(const AppSettings &, const AppSettings &)> same;
};

namespace {

// Reaches a field of AppSettings, or of one of its groups, in a draft or a
// snapshot alike.
template<typename T>
auto member(T AppSettings::*field)
{
    return [field](auto &settings) -> auto & { return settings.*field; };
}

template<typename Group, typename T>
auto member(Group AppSettings::*group, T Group::*field)
{
    return [group, field](auto &settings) -> auto & { return settings.*group.*field; };
}

// get is a codec getter, or a function of the codecs and the fields read
// before this one.
template<typename Member, typename Get>
std::function<void(const SettingsCodecs &, AppSettings &)> reader(Member member, Get get)
{
    return [member, get](const SettingsCodecs &codecs, AppSettings &settings) {
        if constexpr (std::is_invocable_v<Get, const SettingsCodecs &, const AppSettings &>) {
            member(settings) = get(codecs, settings);
        } else {
            member(settings) = std::invoke(get, codecs);
        }
    };
}

template<typename Member>
std::function<bool(const AppSettings &, const AppSettings &)> sameValue(Member member)
{
    return [member](const AppSettings &a, const AppSettings &b) { return member(a) == member(b); };
}

} // namespace

// In the order the setters need: custom
// tones and levels before the profiles that fall back from a deleted one,
// and the profiles before the apps and terms that name them.
// SettingsStore keeps the keyring keys and unreadSecretKeys itself.
const std::vector<SettingsCodecs::Field> &SettingsCodecs::fields()
{
    using C = SettingsCodecs;
    using A = AppSettings;
    const auto stored = [](const char *name, auto member, auto get, auto set) {
        return Field{name, reader(member, get),
                     [member, set](C &codecs, const A &draft) { std::invoke(set, codecs, member(draft)); },
                     sameValue(member)};
    };
    // Read into a snapshot but never saved from one: derived from other
    // fields, detected, a default only the file can change, or written by
    // the code that owns it.
    const auto readOnly = [](const char *name, auto member, auto get) {
        return Field{name, reader(member, get), {}, sameValue(member)};
    };
    // Neither read nor saved: tests set it on their own copy.
    const auto testHook = [](const char *name, auto member) { return Field{name, {}, {}, sameValue(member)}; };

    static const std::vector<Field> table{
        stored("setupCompleted", member(&A::setupCompleted), &C::setupCompleted, &C::setSetupCompleted),
        stored("launchAtLogin", member(&A::launchAtLogin), &C::launchAtLogin, &C::setLaunchAtLogin),
        stored("ui.theme", member(&A::ui, &UiSettings::theme), &C::theme, &C::setTheme),
        stored("ui.colorScheme", member(&A::ui, &UiSettings::colorScheme), &C::colorScheme, &C::setColorScheme),
        stored("ui.translucency", member(&A::ui, &UiSettings::translucency), &C::translucency, &C::setTranslucency),
        stored("ui.windowLayout", member(&A::ui, &UiSettings::windowLayout), &C::windowLayout, &C::setWindowLayout),
        stored("ui.roundedCorners", member(&A::ui, &UiSettings::roundedCorners), &C::roundedCorners,
               &C::setRoundedCorners),
        stored("ui.boringMode", member(&A::ui, &UiSettings::boringMode), &C::boringMode, &C::setBoringMode),
        stored("ui.pauseMediaDuringTranscription", member(&A::ui, &UiSettings::pauseMediaDuringTranscription),
               &C::pauseMediaDuringTranscription, &C::setPauseMediaDuringTranscription),
        stored("ui.soundsEnabled", member(&A::ui, &UiSettings::soundsEnabled), &C::soundsEnabled, &C::setSoundsEnabled),
        stored("ui.transcriptionPreviewEnabled", member(&A::ui, &UiSettings::transcriptionPreviewEnabled),
               &C::transcriptionPreviewEnabled, &C::setTranscriptionPreviewEnabled),
        stored("ui.refinementPreviewEnabled", member(&A::ui, &UiSettings::refinementPreviewEnabled),
               &C::refinementPreviewEnabled, &C::setRefinementPreviewEnabled),
        stored("ui.selectionEditReviewEnabled", member(&A::ui, &UiSettings::selectionEditReviewEnabled),
               &C::selectionEditReviewEnabled, &C::setSelectionEditReviewEnabled),
        stored("ui.previewWords", member(&A::ui, &UiSettings::previewWords), &C::previewWords, &C::setPreviewWords),

        stored("speech.services", member(&A::speech, &SpeechSettings::services), &C::speechServices,
               &C::setSpeechServices),
        stored("speech.codexFinalRetranscribe", member(&A::speech, &SpeechSettings::codexFinalRetranscribe),
               &C::codexFinalRetranscribe, &C::setCodexFinalRetranscribe),
        stored("speech.language", member(&A::speech, &SpeechSettings::language), &C::spokenLanguage,
               &C::setSpokenLanguage),
        stored("speech.local", member(&A::speech, &SpeechSettings::local), &C::localSpeechSettings,
               &C::setLocalSpeechSettings),
        stored("speech.server", member(&A::speech, &SpeechSettings::server), &C::speechServer, &C::setSpeechServer),
        readOnly("speech.claudeEndpointBase", member(&A::speech, &SpeechSettings::claudeEndpointBase),
                 &C::claudeEndpointBase),
        readOnly("speech.claudeVoicePath", member(&A::speech, &SpeechSettings::claudeVoicePath), &C::claudeVoicePath),
        stored("audio", member(&A::audio), &C::audioCaptureSettings, &C::setAudioCaptureSettings),

        stored("refinement.enabled", member(&A::refinement, &RefinementSettings::enabled), &C::refinementEnabled,
               &C::setRefinementEnabled),
        stored("refinement.services", member(&A::refinement, &RefinementSettings::services),
               &C::refinementServices, &C::setRefinementServices),
        stored("refinement.customTones", member(&A::refinement, &RefinementSettings::customTones), &C::customTones,
               &C::setCustomTones),
        stored("refinement.customCleanupLevels", member(&A::refinement, &RefinementSettings::customCleanupLevels),
               &C::customCleanupLevels, &C::setCustomCleanupLevels),
        stored("refinement.writingProfiles", member(&A::refinement, &RefinementSettings::writingProfiles),
               &C::writingProfileSettings, &C::setWritingProfileSettings),
        stored("apps", member(&A::apps), &C::apps, &C::setApps),
        stored("refinement.useTargetContext", member(&A::refinement, &RefinementSettings::useTargetContext),
               &C::useTargetContext, &C::setUseTargetContext),
        stored("refinement.includeScreenshotContext",
               member(&A::refinement, &RefinementSettings::includeScreenshotContext), &C::includeScreenshotContext,
               &C::setIncludeScreenshotContext),
        stored("refinement.additionalInstructions",
               member(&A::refinement, &RefinementSettings::additionalInstructions), &C::additionalInstructions,
               &C::setAdditionalInstructions),
        stored("refinement.customSystemPromptEnabled",
               member(&A::refinement, &RefinementSettings::customSystemPromptEnabled), &C::customSystemPromptEnabled,
               &C::setCustomSystemPromptEnabled),
        stored("refinement.customSystemPrompt", member(&A::refinement, &RefinementSettings::customSystemPrompt),
               &C::customSystemPrompt, &C::setCustomSystemPrompt),
        // The cleanup level saved before Writing Profiles, which seeds them
        // until they are saved. Each run takes its own from its profile.
        readOnly("refinement.style", member(&A::refinement, &RefinementSettings::style), &C::refinementStyle),
        // Tests point their own copy at a server of their own.
        testHook("refinement.anthropicEndpointBase", member(&A::refinement, &RefinementSettings::anthropicEndpointBase)),

        stored("accounts.openAiAuthMode", member(&A::accounts, &AccountSettings::openAiAuthMode), &C::openAiAuthMode,
               &C::setOpenAiAuthMode),
        stored("accounts.openAiCliproxyAccount", member(&A::accounts, &AccountSettings::openAiCliproxyAccount),
               &C::openAiCliproxyAccount, &C::setOpenAiCliproxyAccount),
        stored("accounts.anthropicAuthMode", member(&A::accounts, &AccountSettings::anthropicAuthMode),
               &C::anthropicAuthMode, &C::setAnthropicAuthMode),
        stored("accounts.anthropicCliproxyAccount", member(&A::accounts, &AccountSettings::anthropicCliproxyAccount),
               &C::anthropicCliproxyAccount, &C::setAnthropicCliproxyAccount),
        stored("accounts.cliproxyOauthDirConfigured",
               member(&A::accounts, &AccountSettings::cliproxyOauthDirConfigured), &C::configuredCliproxyOauthDir,
               &C::setCliproxyOauthDir),
        readOnly("accounts.cliproxyOauthDir", member(&A::accounts, &AccountSettings::cliproxyOauthDir),
                 &C::cliproxyOauthDir),
        readOnly("accounts.claudeCredentialsPath", member(&A::accounts, &AccountSettings::claudeCredentialsPath),
                 &C::claudeCredentialsPath),
        stored("accounts.cliproxyBaseUrl", member(&A::accounts, &AccountSettings::cliproxyBaseUrl),
               &C::cliproxyBaseUrl, &C::setCliproxyBaseUrl),
        stored("refinement.server", member(&A::refinement, &RefinementSettings::server), &C::refinementServer,
               &C::setRefinementServer),
        stored("refinement.localRunner", member(&A::refinement, &RefinementSettings::localRunner),
               &C::localRunnerSettings, &C::setLocalRunnerSettings),

        stored("output.steps", member(&A::output, &OutputSettings::steps), &C::deliverySteps, &C::setDeliverySteps),
        stored("output.format", member(&A::output, &OutputSettings::format), &C::outputFormat, &C::setOutputFormat),
        stored("output.completionStatusDurationMs", member(&A::output, &OutputSettings::completionStatusDurationMs),
               &C::completionStatusDurationMs, &C::setCompletionStatusDurationMs),
        // VirtualKeyboardSetup sets it once ydotool is set up.
        readOnly("output.ydotoolEnabled", member(&A::output, &OutputSettings::ydotoolEnabled), &C::ydotoolEnabled),
        stored("shortcutActivationMode", member(&A::shortcutActivationMode), &C::shortcutActivationMode,
               &C::setShortcutActivationMode),

        stored("updates.channel", member(&A::updates, &UpdateSettings::channel), &C::updateChannel,
               &C::setUpdateChannel),
        stored("updates.autoCheck", member(&A::updates, &UpdateSettings::autoCheck), &C::autoCheckUpdates,
               &C::setAutoCheckUpdates),
        stored("updates.autoInstall", member(&A::updates, &UpdateSettings::autoInstall), &C::autoInstallUpdates,
               &C::setAutoInstallUpdates),
        stored("updates.checkIntervalMinutes", member(&A::updates, &UpdateSettings::checkIntervalMinutes),
               &C::updateCheckIntervalMinutes, &C::setUpdateCheckIntervalMinutes),
        stored("updates.checkIntervalUnit", member(&A::updates, &UpdateSettings::checkIntervalUnit),
               &C::updateCheckIntervalUnit, &C::setUpdateCheckIntervalUnit),

        stored("vocabulary", member(&A::vocabulary), &C::vocabularyEntries, &C::setVocabularyEntries),
        // A dictation narrows this to its Writing Profile once it knows the target.
        readOnly("speech.vocabulary", member(&A::speech, &SpeechSettings::vocabulary),
                 [](const C &, const A &settings) {
                     return speechVocabulary(settings.vocabulary, VocabularyScope{});
                 }),
        stored("correctionLearningEnabled", member(&A::correctionLearningEnabled), &C::correctionLearningEnabled,
               &C::setCorrectionLearningEnabled),
        stored("history", member(&A::history), &C::historySettings, &C::setHistorySettings),
        stored("recording", member(&A::recording), &C::recordingSettings, &C::setRecordingSettings),
    };
    return table;
}

AppSettings SettingsCodecs::snapshot() const
{
    AppSettings settings;
    for (const Field &field : fields()) {
        if (field.read) {
            field.read(*this, settings);
        }
    }
    return settings;
}

void SettingsCodecs::writeSnapshot(const AppSettings &draft)
{
    for (const Field &field : fields()) {
        if (field.write) {
            field.write(*this, draft);
        }
    }
}

QString SettingsCodecs::changedUnstoredField(const AppSettings &before, const AppSettings &after)
{
    for (const Field &field : fields()) {
        if (!field.write && !field.same(before, after)) {
            return QString::fromLatin1(field.name);
        }
    }
    return {};
}

} // namespace speecher
