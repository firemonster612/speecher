#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace speecher {

enum class AppCategory {
    Unknown,
    General,
    Terminal,
    Browser,
    Email,
    Office,
    CodeEditor,
    AiCoding,
};

// A Writing Profile is named by its id: one of these built-ins, or the id of
// a profile the user added, which starts with kCustomIdPrefix.
namespace WritingProfile {
inline const QString Work = QStringLiteral("work");
inline const QString Email = QStringLiteral("email");
inline const QString Personal = QStringLiteral("personal");
inline const QString AiCoding = QStringLiteral("ai_coding");
inline const QString Other = QStringLiteral("other");
} // namespace WritingProfile

// What every id the user's own tones, cleanup levels and profiles get starts
// with, so none matches a built-in id.
inline const QString kCustomIdPrefix = QStringLiteral("custom_");

QString appCategoryName(AppCategory category);
AppCategory appCategoryFromName(const QString &name);
bool isBuiltInWritingProfile(const QString &id);
// The profile a stored id names: a built-in, the legacy "technical" as Work,
// or a custom profile's id. Anything else reads as Other.
QString writingProfileFromName(const QString &name);
// How each is written where a person reads it, as opposed to how it is stored.
QString appCategoryLabel(AppCategory category);

struct WritingProfileOverride {
    QString applicationId;
    QString profile = WritingProfile::Other;
    bool enabled = true;

    bool operator==(const WritingProfileOverride &other) const = default;
};

struct WritingProfileSettings {
    QString profile = WritingProfile::Other;
    QString cleanupStrength = QStringLiteral("balanced");
    QString tone = QStringLiteral("none");
    // Added to the refinement prompt for this profile, after the global ones.
    QString instructions;
    // A custom profile's name. A built-in is called by its label.
    QString name;
    // The language refinement writes in, by name, such as "Spanish". Empty
    // writes in the language that was spoken.
    QString outputLanguage;

    bool operator==(const WritingProfileSettings &other) const = default;
};

// A built-in's label or a custom profile's name, empty for an id `profiles`
// does not hold.
QString writingProfileLabel(const QString &id, const QList<WritingProfileSettings> &profiles);
// The id when `profiles` holds it, otherwise `fallback`: what a reference to a
// deleted profile becomes.
QString offeredWritingProfile(const QString &id,
                              const QList<WritingProfileSettings> &profiles,
                              const QString &fallback = WritingProfile::Other);

struct AppRecognitionRule {
    QString match;
    std::optional<AppCategory> category;
    std::optional<QString> writingProfile;

    bool operator==(const AppRecognitionRule &other) const = default;
};

QList<WritingProfileSettings> defaultWritingProfileSettings();
WritingProfileSettings writingProfileSettingsFor(const QList<WritingProfileSettings> &settings,
                                                  const QString &profile);

// A tone the user defined: the model is told its name and follows the
// instruction.
struct CustomTone {
    QString id;
    QString name;
    QString instruction;

    bool operator==(const CustomTone &other) const = default;
};

// A cleanup level the user defined: the built-in level it builds on
// (light_cleanup, balanced, strong_polish, or custom_only for none of the
// level rules) plus the user's instructions.
struct CustomCleanupLevel {
    QString id;
    QString name;
    QString base = QStringLiteral("balanced");
    QString instructions;

    bool operator==(const CustomCleanupLevel &other) const = default;
};

inline const QString kCustomOnlyCleanupBase = QStringLiteral("custom_only");

// The cleanup level refinement runs at. Translating is refinement, so None
// with an output language refines at Light, the level that stays closest to
// what was said.
QString refinedCleanupLevel(const QString &level, const QString &outputLanguage);

struct Target {
    QString applicationId;
    QString applicationName;
    QString processName;
    QString windowTitle;
    QString documentUrl;
    QString controlName;
    QString role;
    QString toolkit;
    QString nearbyTextBefore;
    QString nearbyTextAfter;
    QString selectedText;
    QString fingerprint;
    qint64 processId = 0;
    int caretOffset = -1;
    int selectionStart = -1;
    int selectionEnd = -1;
    AppCategory category = AppCategory::Unknown;
    bool accessible = false;
    bool secure = false;
    bool terminalHost = false;
    bool aiCodingToolActive = false;
    // The compositor named this the active window when accessibility could not
    // identify it. Delivery trusts it as the focus target without an AT-SPI
    // match, so a terminal that never reaches the a11y bus still pastes.
    bool compositorActive = false;
    // The compositor's own handle for that window, when it has one. It is what
    // tells one window of a program from another, and survives a retitle.
    QString compositorWindowId;

    bool hasIdentity() const;
    bool hasSelection() const;
};

QList<AppRecognitionRule> builtInAppRecognitionRules();
// Recognition rules replaced per-application Writing Profile overrides. This
// folds any surviving override into the rule list that supersedes it; saving
// the folded list and clearing the overrides is what retires them for good.
QList<AppRecognitionRule> recognitionRulesWithMigratedProfileOverrides(
    const QList<AppRecognitionRule> &rules,
    const QList<WritingProfileOverride> &overrides);
bool isTerminalTarget(const Target &target);
AppCategory classifyTarget(const Target &target,
                           const QList<AppRecognitionRule> &customRules = {});
QString inferWritingProfile(const Target &target, const QString &fallback = WritingProfile::Other);
QString resolveWritingProfile(const Target &target,
                              const QList<WritingProfileOverride> &overrides,
                              const QString &fallback = WritingProfile::Other);
QString resolveWritingProfile(const Target &target,
                              const QList<WritingProfileOverride> &overrides,
                              const QList<AppRecognitionRule> &recognitionRules,
                              const QString &fallback = WritingProfile::Other);

struct RefinementContext {
    Target target;
    QString writingProfile = WritingProfile::Other;
    QString tone = QStringLiteral("none");
    // The user's own instructions, from settings: every refinement's, then
    // the writing profile's.
    QString additionalInstructions;
    QString profileInstructions;
    // The writing profile's output language; empty keeps the spoken one.
    QString outputLanguage;
    // The session's Spoken Language. English, or empty, adds no rule.
    QString spokenLanguage;
    // Replaces the built-in dictation rules when not blank.
    QString customSystemPrompt;
    // Set when the tone or the cleanup level is one the user defined. The
    // refinement style is then the level's base.
    std::optional<CustomTone> customTone;
    std::optional<CustomCleanupLevel> cleanupLevel;
    bool includeNearbyText = true;
    bool editSelection = false;
    QByteArray screenshotData;
    QString screenshotMediaType;

    bool hasScreenshot() const
    {
        return !screenshotData.isEmpty() && !screenshotMediaType.isEmpty();
    }
};

} // namespace speecher
