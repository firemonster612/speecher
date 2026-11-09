// Writes the app's real refinement prompts for bench.py: the context-free
// system, compact and user prompts, and a system and user prompt for each
// sample in contexts.json, which bench.py sends in place of the defaults.
#include "core/Apps.h"
#include "providers/TranscriptRefinementPrompt.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace speecher;

static void write(const QString &name, const QString &text)
{
    QFile file(name);
    if (!file.open(QIODevice::WriteOnly)) {
        qFatal("cannot write %s", qPrintable(name));
    }
    file.write(text.toUtf8());
}

int main()
{
    const QString transcript = QStringLiteral("__TRANSCRIPT__");
    write("system.txt", dictationRefinementSystemPrompt(QStringLiteral("balanced"), {}));
    write("user.txt", transcriptRefinementUserMessage(transcript, {}, {}));
    write("compact.txt", compactRefinementSystemPrompt(QStringLiteral("balanced"), {}));

    QFile contexts("contexts.json");
    if (!contexts.open(QIODevice::ReadOnly)) {
        qFatal("cannot read contexts.json");
    }
    const QJsonObject samples = QJsonDocument::fromJson(contexts.readAll()).object();
    for (auto it = samples.begin(); it != samples.end(); ++it) {
        const QJsonObject sample = it.value().toObject();
        RefinementContext context;
        context.target.applicationName = sample.value("application_name").toString();
        context.target.applicationId = sample.value("application_id").toString();
        context.target.category = appCategoryFromName(sample.value("category").toString());
        context.writingProfile = resolveApp(context.target, {}).writingProfile;
        context.writingProfileName = writingProfileLabel(context.writingProfile, {});
        // A sample without text around the caret is one whose text could not be read.
        context.includeNearbyText = sample.contains("before");
        if (context.includeNearbyText) {
            const QString before = sample.value("before").toString();
            context.target.caretOffset = int(before.size());
            context.target.setNearbyText(before + sample.value("after").toString(), int(before.size()),
                                         int(before.size()));
        }
        QStringList vocabulary;
        for (const QJsonValue &term : sample.value("vocabulary").toArray()) {
            vocabulary << term.toString();
        }
        write("system-" + it.key() + ".txt", dictationRefinementSystemPrompt(QStringLiteral("balanced"), context));
        write("user-" + it.key() + ".txt", transcriptRefinementUserMessage(transcript, vocabulary, context));
    }
    return 0;
}
