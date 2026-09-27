#pragma once

#include "core/LocalModelCatalog.h"

#include <QHash>
#include <QObject>
#include <QThread>
#include <QUrl>

namespace speecher {

// Local Model files on disk: downloads that resume, are checked against the
// catalog's sha256 before they count, and can be cancelled or deleted. The
// network, disk writes, hashing and rename run on a thread of the store's own;
// the calls and signals here belong to the thread that created it.
class LocalModelStore : public QObject {
    Q_OBJECT

public:
    // Models live in the app data directory's models/ folder and come from
    // Hugging Face; tests pass their own of each.
    explicit LocalModelStore(QObject *parent = nullptr);
    LocalModelStore(const QString &directory, const QUrl &server, QObject *parent = nullptr);
    ~LocalModelStore() override;

    QString directory() const;
    QString modelPath(const LocalModel &model) const;
    bool isDownloaded(const LocalModel &model) const;
    bool isDownloading(const QString &modelId) const;

    // Picks up a partial file where it stopped. Does nothing while the model
    // is already downloading.
    void download(const LocalModel &model);
    // Keeps the partial file for the next download to resume. Returns once
    // nothing holds the file open.
    void cancel(const QString &modelId);
    // Deletes the model and any partial download. False if a file stayed.
    bool remove(const LocalModel &model);

signals:
    void downloadProgress(const QString &modelId, qint64 receivedBytes, qint64 totalBytes);
    void downloadFinished(const QString &modelId);
    void downloadFailed(const QString &modelId, const QString &message);

private:
    class Worker;

    // A report from the worker about download `serial`; stale ones are dropped.
    void reportProgress(const QString &modelId, quint64 serial, qint64 received, qint64 total);
    void reportEnd(const QString &modelId, quint64 serial, const QString &error);

    QString m_directory;
    // The serial of each running download.
    QHash<QString, quint64> m_downloads;
    quint64 m_lastSerial = 0;
    QThread m_thread;
    Worker *m_worker = nullptr;
};

} // namespace speecher
