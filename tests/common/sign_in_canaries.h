#pragma once

// Linux only: it watches the canaries with inotify.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <sys/inotify.h>
#include <unistd.h>

namespace speecher::test {

// Code that ignores CLAUDE_CONFIG_DIR or CODEX_HOME looks for the sign-ins
// under HOME. Tests get a HOME whose Claude and Codex sign-ins are canaries,
// and opening one, or creating a file beside one, fails the run.
class SignInCanaries {
public:
    explicit SignInCanaries(const QString &home)
        : m_watcher(inotify_init1(IN_NONBLOCK | IN_CLOEXEC))
    {
        for (const QString &file : {QStringLiteral(".claude/.credentials.json"), QStringLiteral(".codex/auth.json")}) {
            const QFileInfo canary(QDir(home).filePath(file));
            QFile written(canary.filePath());
            const bool made = QDir().mkpath(canary.path()) && written.open(QIODevice::WriteOnly)
                && written.write("canary") > 0;
            written.close();
            const int watch = made && m_watcher >= 0
                ? inotify_add_watch(m_watcher, QFile::encodeName(canary.path()).constData(), IN_OPEN | IN_CREATE)
                : -1;
            if (watch < 0) {
                if (m_watcher >= 0) close(m_watcher);
                m_watcher = -1;
                return;
            }
            m_directories.insert(watch, canary.path());
        }
    }
    ~SignInCanaries()
    {
        if (m_watcher >= 0) close(m_watcher);
    }
    bool isValid() const { return m_watcher >= 0; }

    // Each file a test opened or created in a canary's directory so far.
    QStringList touched() const
    {
        QStringList paths;
        alignas(inotify_event) char buffer[4096];
        ssize_t length;
        while ((length = read(m_watcher, buffer, sizeof buffer)) > 0) {
            for (ssize_t offset = 0; offset < length;) {
                const auto *event = reinterpret_cast<const inotify_event *>(buffer + offset);
                const QString name = event->len ? QFile::decodeName(event->name) : QString();
                paths.append(QDir(m_directories.value(event->wd)).filePath(name));
                offset += sizeof(inotify_event) + event->len;
            }
        }
        paths.removeDuplicates();
        return paths;
    }

private:
    int m_watcher;
    QHash<int, QString> m_directories;
};

} // namespace speecher::test
