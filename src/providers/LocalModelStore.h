#pragma once

#include "core/LocalModelCatalog.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

class QFile;
class QNetworkReply;

namespace speecher {

// Local Model files on disk: downloads that resume, are checked against the
// catalog's sha256 before they count, and can be cancelled or deleted.
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
    // Keeps the partial file for the next download to resume.
    void cancel(const QString &modelId);
    // Deletes the model and any partial download. False if a file stayed.
    bool remove(const LocalModel &model);

signals:
    void downloadProgress(const QString &modelId, qint64 receivedBytes, qint64 totalBytes);
    void downloadFinished(const QString &modelId);
    void downloadFailed(const QString &modelId, const QString &message);

private:
    struct Download;

    QString partPath(const LocalModel &model) const;
    void writeReceived(Download *download);
    void finishReply(const QString &modelId);
    void verify(const LocalModel &model);
    void fail(const QString &modelId, const QString &message);

    QString m_directory;
    QUrl m_server;
    QNetworkAccessManager m_network;
    QHash<QString, Download *> m_downloads;
};

} // namespace speecher
