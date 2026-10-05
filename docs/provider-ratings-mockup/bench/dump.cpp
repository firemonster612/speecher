#include "providers/TranscriptRefinementPrompt.h"
#include <QFile>
#include <cstdio>
using namespace speecher;
int main() {
    QFile s("system.txt"); s.open(QIODevice::WriteOnly); s.write(dictationRefinementSystemPrompt(QStringLiteral("balanced")).toUtf8());
    QFile u("user.txt"); u.open(QIODevice::WriteOnly); u.write(transcriptRefinementUserMessage(QStringLiteral("__TRANSCRIPT__"), {}, {}).toUtf8());
    QFile c("compact.txt"); c.open(QIODevice::WriteOnly); c.write(compactRefinementSystemPrompt(QStringLiteral("balanced"), {}).toUtf8());
    return 0;
}
