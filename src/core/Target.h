#pragma once

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

QString appCategoryName(AppCategory category);
AppCategory appCategoryFromName(const QString &name);

struct Target {
    QString applicationId;
    QString applicationName;
    QString processName;
    QString controlName;
    QString role;
    QString toolkit;
    QString nearbyTextBefore;
    QString nearbyTextAfter;
    QString fingerprint;
    qint64 processId = 0;
    int caretOffset = -1;
    int selectionStart = -1;
    int selectionEnd = -1;
    AppCategory category = AppCategory::Unknown;
    bool accessible = false;
    bool secure = false;

    bool hasIdentity() const;
};

AppCategory classifyTarget(const Target &target);

} // namespace speecher
