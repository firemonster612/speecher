#pragma once

#include <QByteArray>
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
    General,
    Email,
    Technical,
    Personal,
};

QString appCategoryName(AppCategory category);
AppCategory appCategoryFromName(const QString &name);
QString writingProfileName(WritingProfile profile);
WritingProfile writingProfileFromName(const QString &name);

struct Target {
    QString applicationId;
    QString applicationName;
    QString processName;
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
WritingProfile inferWritingProfile(const Target &target, WritingProfile fallback = WritingProfile::General);

struct RefinementContext {
    Target target;
    WritingProfile writingProfile = WritingProfile::General;
    bool includeNearbyText = true;
    QByteArray screenshotData;
    QString screenshotMediaType;

    bool hasScreenshot() const
    {
        return !screenshotData.isEmpty() && !screenshotMediaType.isEmpty();
    }
};

} // namespace speecher
