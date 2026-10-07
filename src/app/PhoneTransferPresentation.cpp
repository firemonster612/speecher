#include "app/PhoneTransferPresentation.h"

#include "core/PasteRules.h"

#include <algorithm>

namespace speecher {

namespace {

QString counted(qsizetype count, const QString &one, const QString &many)
{
    return count == 1 ? one : many.arg(count);
}

QString status(PhoneTransferState state)
{
    switch (state) {
    case PhoneTransferState::Waiting:
        return QStringLiteral("Waiting for your phone… This code works once, while this window is open.");
    case PhoneTransferState::Sent:
        return QStringLiteral("Sent to your phone. Finish the import there.");
    case PhoneTransferState::NoNetwork:
        return QStringLiteral("This computer isn't connected to a network. Connect it to the same "
                              "Wi-Fi as your phone, then open this again.");
    case PhoneTransferState::Failed:
        return QStringLiteral("Speecher couldn't open a network port for your phone.");
    }
    return {};
}

} // namespace

PhoneTransferText phoneTransferText(const AppSettings &settings, PhoneTransferState state)
{
    PhoneTransferText text;
    text.title = QStringLiteral("Copy settings to your phone");
    if (state == PhoneTransferState::Waiting || state == PhoneTransferState::Sent) {
        text.steps = {
            QStringLiteral("Open Speecher on your phone."),
            QStringLiteral("Go to Settings, then Import from computer."),
            QStringLiteral("Scan this code."),
        };
    }

    const RefinementSettings &refinement = settings.refinement;
    text.includedHeading = QStringLiteral("Included");
    if (!settings.vocabulary.isEmpty()) {
        text.included.append(counted(settings.vocabulary.size(),
                                     QStringLiteral("1 vocabulary term, with its context and profiles"),
                                     QStringLiteral("%1 vocabulary terms, with their context and profiles")));
    }
    if (!settings.bindings.isEmpty()) {
        text.included.append(counted(settings.bindings.size(),
                                     QStringLiteral("1 replacement or snippet"),
                                     QStringLiteral("%1 replacements and snippets")));
    }
    const qsizetype builtIn = std::count_if(
        refinement.writingProfiles.begin(), refinement.writingProfiles.end(),
        [](const WritingProfileSettings &profile) { return isBuiltInWritingProfile(profile.profile); });
    const qsizetype own = refinement.writingProfiles.size() - builtIn;
    const QString builtInProfiles =
        builtIn == 1 ? QStringLiteral("1 built-in Writing Profile")
                     // ui-lint: allow title-case: the plural of the Writing Profile glossary term.
                     : QStringLiteral("%1 built-in Writing Profiles").arg(builtIn);
    text.included.append(own == 0 ? builtInProfiles
                                  : QStringLiteral("%1 and %2 of your own").arg(builtInProfiles).arg(own));
    if (!refinement.customTones.isEmpty()) {
        text.included.append(counted(refinement.customTones.size(),
                                     QStringLiteral("1 tone of your own"),
                                     QStringLiteral("%1 tones of your own")));
    }
    if (!refinement.customCleanupLevels.isEmpty()) {
        text.included.append(counted(refinement.customCleanupLevels.size(),
                                     QStringLiteral("1 cleanup level of your own"),
                                     QStringLiteral("%1 cleanup levels of your own")));
    }
    if (!refinement.additionalInstructions.trimmed().isEmpty()) {
        text.included.append(QStringLiteral("Additional instructions"));
    }

    if (!settings.learnedCorrections.isEmpty()) {
        text.stays.append(counted(settings.learnedCorrections.size(),
                                  QStringLiteral("1 learned correction"),
                                  QStringLiteral("%1 learned corrections")));
    }
    const qsizetype recognitionRules = recognitionRulesWithMigratedProfileOverrides(
        settings.appRecognitionRules, refinement.writingProfileOverrides).size();
    if (recognitionRules > 0) {
        text.stays.append(counted(recognitionRules,
                                  QStringLiteral("1 application recognition rule"),
                                  QStringLiteral("%1 application recognition rules")));
    }
    const qsizetype pasteRules = std::count_if(
        settings.output.pasteRules.begin(), settings.output.pasteRules.end(),
        [](const PasteRule &rule) { return rule.scope == PasteRuleScope::Application; });
    if (pasteRules > 0) {
        text.stays.append(counted(pasteRules, QStringLiteral("1 app-specific paste rule"),
                                  QStringLiteral("%1 app-specific paste rules")));
    }
    if (!text.stays.isEmpty()) {
        text.staysHeading = QStringLiteral("Stays on this computer");
    }

    text.neverIncluded = QStringLiteral("Never included: accounts, sign-ins and API keys.");
    text.status = status(state);
#ifdef Q_OS_MACOS
    text.close = QStringLiteral("Done");
#else
    text.close = QStringLiteral("Close");
#endif
    return text;
}

} // namespace speecher
