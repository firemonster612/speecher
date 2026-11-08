#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <memory>
#include <vector>

namespace speecher::test {

// While it lives, the files now in folder can't be deleted: the folder is
// read-only on Unix, and each file is held open on Windows. holds() says
// whether that stops a delete here, which it doesn't for root.
class LockedFolder {
public:
    explicit LockedFolder(const QString &folder)
        : m_folder(folder)
    {
        QFile probe(QDir(folder).filePath(QStringLiteral(".locked-probe")));
        if (probe.open(QIODevice::WriteOnly)) {
            probe.close();
        }
#ifdef Q_OS_WIN
        for (const QFileInfo &file : QDir(folder).entryInfoList(QDir::Files | QDir::Hidden)) {
            auto held = std::make_unique<QFile>(file.filePath());
            if (held->open(QIODevice::ReadOnly)) {
                m_held.push_back(std::move(held));
            }
        }
#else
        QFile::setPermissions(folder, QFile::ReadOwner | QFile::ExeOwner);
#endif
        m_holds = !QFile::remove(probe.fileName());
    }
    ~LockedFolder()
    {
        m_held.clear();
        QFile::setPermissions(m_folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QFile::remove(QDir(m_folder).filePath(QStringLiteral(".locked-probe")));
    }

    bool holds() const { return m_holds; }

private:
    QString m_folder;
    std::vector<std::unique_ptr<QFile>> m_held;
    bool m_holds = false;
};

} // namespace speecher::test
