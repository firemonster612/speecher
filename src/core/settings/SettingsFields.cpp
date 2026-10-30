// The field table: every AppSettings field once, with how a snapshot reads it
// from the settings file and how a saved draft writes it back. It drives
// SettingsCodecs::snapshot and writeSnapshot, and tells SettingsStore::edit
// which fields it can't store.
#include "core/settings/SettingsCodecs.h"

#include "core/ProviderChain.h"
#include "core/Vocabulary.h"
#include "core/settings/SettingsKeys.h"

#include <QDebug>

#include <functional>
#include <type_traits>

namespace speecher {

struct SettingsCodecs::Field {
    // Its path in AppSettings, for the message when an edit changes a field
    // that isn't stored.
    const char *name;
    // Fills the field of a snapshot from the file. Empty for a field one run
    // sets on its own copy, which a snapshot leaves at its default.
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

// In the order the setters need: a provider before its fallbacks, custom
// tones and levels before the profiles that fall back from a deleted one,
// and the profiles before the rules, terms and default that name them.
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
    // Set by a dictation or a transcription on its own copy of the settings.
    const auto perRun = [](const char *name, auto member) { return Field{name, {}, {}, sameValue(member)}; };

    static const std::vector<Field> table{
        stored("setupCompleted", member(&A::setupCompleted), &C::setupCompleted, &C::setSetupCompleted),
        stored("launchAtLogin", member(&A::launchAtLogin), &C::launchAtLogin, &C::setLaunchAtLogin),
        stored("ui.theme", member(&A::ui, &UiSettings::theme), &C::theme, &C::setTheme),
        stored("ui.colorScheme", member(&A::ui, &UiSettings::colorScheme), &C::colorScheme, &C::setColorScheme),
        stored("ui.translucency", member(&A::ui, &UiSettings::translucency), &C::translucency, &C::setTranslucency),
        stored("ui.windowLayout", member(&A::ui, &UiSettings::windowLayout), &C::windowLayout, &C::setWindowLayout),
        stored("ui.roundedCorners", member(&A::ui, &UiSettings::roundedCorners), &C::roundedCorners,
               &C::setRoundedCorners),
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

        stored("speech.providerId", member(&A::speech, &SpeechSettings::providerId), &C::speechProvider,
               &C::setSpeechProvider),
        stored("speech.fallbackProviderIds", member(&A::speech, &SpeechSettings::fallbackProviderIds),
               &C::speechFallbackProviders, &C::setSpeechFallbackProviders),
        stored("speech.fallbackLanguages", member(&A::speech, &SpeechSettings::fallbackLanguages),
               &C::speechFallbackLanguages, &C::setSpeechFallbackLanguages),
        stored("speech.codexFinalRetranscribe", member(&A::speech, &SpeechSettings::codexFinalRetranscribe),
               &C::codexFinalRetranscribe, &C::setCodexFinalRetranscribe),
        stored("speech.language", member(&A::speech, &SpeechSettings::language), &C::spokenLanguage,
               &C::setSpokenLanguage),
        stored("speech.local", member(&A::speech, &SpeechSettings::local), &C::localSpeechSettings,
               &C::setLocalSpeechSettings),
        stored("speech.endpoint", member(&A::speech, &SpeechSettings::endpoint), &C::speechEndpointSettings,
               &C::setSpeechEndpointSettings),
        readOnly("speech.claudeEndpointBase", member(&A::speech, &SpeechSettings::claudeEndpointBase),
                 &C::claudeEndpointBase),
        readOnly("speech.claudeVoicePath", member(&A::speech, &SpeechSettings::claudeVoicePath), &C::claudeVoicePath),
        perRun("speech.timedSegments", member(&A::speech, &SpeechSettings::timedSegments)),
        perRun("speech.cutIntoUtterances", member(&A::speech, &SpeechSettings::cutIntoUtterances)),
        stored("audio", member(&A::audio), &C::audioCaptureSettings, &C::setAudioCaptureSettings),

        stored("refinement.providerId", member(&A::refinement, &RefinementSettings::providerId),
               &C::refinementProvider, &C::setRefinementProvider),
        // Against the provider just read, which refinementFallbackProviders()
        // would look up again, finding installed CLIs when none was chosen.
        stored("refinement.fallbackProviderIds", member(&A::refinement, &RefinementSettings::fallbackProviderIds),
               [](const C &codecs, const A &settings) {
                   return normalizedFallbackProviders(
                       ProviderRole::Refinement, settings.refinement.providerId,
                       codecs.value(SettingsKeys::RefinementFallbackProviders, QStringList()).toStringList());
               },
               &C::setRefinementFallbackProviders),
        stored("refinement.customTones", member(&A::refinement, &RefinementSettings::customTones), &C::customTones,
               &C::setCustomTones),
        stored("refinement.customCleanupLevels", member(&A::refinement, &RefinementSettings::customCleanupLevels),
               &C::customCleanupLevels, &C::setCustomCleanupLevels),
        stored("refinement.writingProfiles", member(&A::refinement, &RefinementSettings::writingProfiles),
               &C::writingProfileSettings, &C::setWritingProfileSettings),
        stored("appRecognitionRules", member(&A::appRecognitionRules), &C::appRecognitionRules,
               &C::setAppRecognitionRules),
        stored("refinement.defaultWritingProfile", member(&A::refinement, &RefinementSettings::defaultWritingProfile),
               &C::defaultWritingProfile, &C::setDefaultWritingProfile),
        stored("refinement.writingProfileOverrides",
               member(&A::refinement, &RefinementSettings::writingProfileOverrides), &C::writingProfileOverrides,
               &C::setWritingProfileOverrides),
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
        stored("refinement.openAiModel", member(&A::refinement, &RefinementSettings::openAiModel), &C::openAiModel,
               &C::setOpenAiModel),
        stored("refinement.openAiEffort", member(&A::refinement, &RefinementSettings::openAiEffort), &C::openAiEffort,
               &C::setOpenAiEffort),
        stored("refinement.openAiSpeed", member(&A::refinement, &RefinementSettings::openAiSpeed), &C::openAiSpeed,
               &C::setOpenAiSpeed),
        stored("refinement.anthropicModel", member(&A::refinement, &RefinementSettings::anthropicModel),
               &C::anthropicModel, &C::setAnthropicModel),
        stored("refinement.anthropicEffort", member(&A::refinement, &RefinementSettings::anthropicEffort),
               &C::anthropicEffort, &C::setAnthropicEffort),
        stored("refinement.anthropicFastMode", member(&A::refinement, &RefinementSettings::anthropicFastMode),
               &C::anthropicFastMode, &C::setAnthropicFastMode),
        // The cleanup level saved before Writing Profiles, which seeds them
        // until they are saved. Each run takes its own from its profile.
        readOnly("refinement.style", member(&A::refinement, &RefinementSettings::style), &C::refinementStyle),
        perRun("refinement.sessionWritingProfile", member(&A::refinement, &RefinementSettings::sessionWritingProfile)),
        // A test hook, not a choice a run makes: tests point their own copy at
        // a server of their own. Like a per-run field, never read or saved.
        perRun("refinement.anthropicEndpointBase", member(&A::refinement, &RefinementSettings::anthropicEndpointBase)),

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
        stored("refinement.cliproxyBaseUrl", member(&A::refinement, &RefinementSettings::cliproxyBaseUrl),
               &C::cliproxyBaseUrl, &C::setCliproxyBaseUrl),
        stored("refinement.endpoint", member(&A::refinement, &RefinementSettings::endpoint),
               &C::refinementEndpointSettings, &C::setRefinementEndpointSettings),
        stored("refinement.localRunner", member(&A::refinement, &RefinementSettings::localRunner),
               &C::localRunnerSettings, &C::setLocalRunnerSettings),

        stored("output.method", member(&A::output, &OutputSettings::method), &C::outputMethod, &C::setOutputMethod),
        stored("output.format", member(&A::output, &OutputSettings::format), &C::outputFormat, &C::setOutputFormat),
        stored("output.pasteRules", member(&A::output, &OutputSettings::pasteRules), &C::pasteRules, &C::setPasteRules),
        stored("output.restoreClipboardAfterTyping", member(&A::output, &OutputSettings::restoreClipboardAfterTyping),
               &C::restoreClipboardAfterTyping, &C::setRestoreClipboardAfterTyping),
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
        stored("learnedCorrections", member(&A::learnedCorrections), &C::learnedCorrections, &C::setLearnedCorrections),
        // A dictation narrows this to its Writing Profile once it knows the target.
        readOnly("speech.vocabulary", member(&A::speech, &SpeechSettings::vocabulary),
                 [](const C &, const A &settings) {
                     return speechVocabulary(settings.vocabulary, settings.learnedCorrections, QString());
                 }),
        perRun("sessionVocabulary", member(&A::sessionVocabulary)),
        stored("correctionLearningEnabled", member(&A::correctionLearningEnabled), &C::correctionLearningEnabled,
               &C::setCorrectionLearningEnabled),
        stored("insightsEnabled", member(&A::insightsEnabled), &C::insightsEnabled, &C::setInsightsEnabled),
        // The settings surface refuses invalid replacements before it saves,
        // so a rejection here is a bug rather than something a person typed.
        stored("bindings", member(&A::bindings), &C::bindingRules,
               [](C &codecs, const QList<BindingRule> &rules) {
                   QString error;
                   if (!codecs.setBindingRules(rules, &error)) {
                       qWarning("dropped invalid replacement rules: %s", qPrintable(error));
                   }
               }),
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
