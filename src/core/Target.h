#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace speecher {

enum class AppCategory {
    Unknown,
    General,
    Terminal,
    Browser,
    Email,
    Office,
    CodeEditor,
};

enum class WritingProfile {
    Work,
    Email,
    Personal,
    Other,
};

QString appCategoryName(AppCategory category);
AppCategory appCategoryFromName(const QString &name);
QString writingProfileName(WritingProfile profile);
WritingProfile writingProfileFromName(const QString &name);

struct WritingProfileOverride {
    QString applicationId;
    WritingProfile profile = WritingProfile::Other;
    bool enabled = true;

    bool operator==(const WritingProfileOverride &other) const = default;
};

struct WritingProfileSettings {
    WritingProfile profile = WritingProfile::Other;
    QString cleanupStrength = QStringLiteral("balanced");
    QString tone = QStringLiteral("none");

    bool operator==(const WritingProfileSettings &other) const = default;
};

QList<WritingProfileSettings> defaultWritingProfileSettings();
WritingProfileSettings writingProfileSettingsFor(const QList<WritingProfileSettings> &settings,
                                                  WritingProfile profile);

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

    bool hasIdentity() const;
    bool hasSelection() const;
};

AppCategory classifyTarget(const Target &target);
WritingProfile inferWritingProfile(const Target &target, WritingProfile fallback = WritingProfile::Other);
WritingProfile resolveWritingProfile(const Target &target,
                                     const QList<WritingProfileOverride> &overrides,
                                     WritingProfile fallback = WritingProfile::Other);

struct RefinementContext {
    Target target;
    WritingProfile writingProfile = WritingProfile::Other;
    QString tone = QStringLiteral("none");
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
