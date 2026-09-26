#include "providers/LocalModelStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>

#include <map>

namespace speecher {
namespace {

constexpr qint64 hashChunkBytes = qint64(1) << 20;
// A download that receives nothing for this long has stalled.
constexpr int transferTimeoutMs = 30000;
constexpr int rangeNotSatisfiable = 416;

QString defaultDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("models"));
}

QString partPath(const QString &modelPath)
{
    return modelPath + QStringLiteral(".part");
}

} // namespace

// Lives on the store's thread and does everything that touches the network
// or the disk. It reports back through the store's queued report* calls.
class LocalModelStore::Worker : public QObject {
public:
    Worker(LocalModelStore *store, QString directory, QUrl server)
        : m_store(store)
        , m_directory(std::move(directory))
        , m_server(std::move(server))
    {
    }

    void start(const LocalModel &model, quint64 serial, std::shared_ptr<std::atomic_bool> cancelled);
    void stop(const QString &modelId);

private:
    struct Download {
        LocalModel model;
        quint64 serial = 0;
        QFile file;
        QNetworkReply *reply = nullptr;
        qint64 resumedFrom = 0;
        // Set by the store's thread to stop hashing early.
        std::shared_ptr<std::atomic_bool> cancelled;
    };

    QString modelPath(const LocalModel &model) const
    {
        return QDir(m_directory).filePath(model.fileName);
    }
    void request(Download *download);
    void writeReceived(Download *download);
    void finishReply(Download *download);
    void verifyAndInstall(Download *download);
    void end(Download *download, const QString &error);

    LocalModelStore *m_store;
    QString m_directory;
    QUrl m_server;
    // Created on first use, so on this thread rather than the store's.
    QNetworkAccessManager *m_network = nullptr;
    std::map<QString, std::unique_ptr<Download>> m_downloads;
};

void LocalModelStore::Worker::start(const LocalModel &model, quint64 serial,
                                    std::shared_ptr<std::atomic_bool> cancelled)
{
    auto owned = std::make_unique<Download>();
    Download *download = owned.get();
    download->model = model;
    download->serial = serial;
    download->cancelled = std::move(cancelled);
    download->file.setFileName(partPath(modelPath(model)));
    m_downloads[model.id] = std::move(owned);
    if (!QDir().mkpath(m_directory)
        || !download->file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        end(download, QStringLiteral("Speecher could not write to %1.").arg(m_directory));
        return;
    }
    if (download->file.size() > model.sizeBytes) {
        download->file.resize(0);
    }
    download->resumedFrom = download->file.size();
    if (download->resumedFrom == model.sizeBytes) {
        download->file.close();
        verifyAndInstall(download);
        return;
    }
    request(download);
}

void LocalModelStore::Worker::request(Download *download)
{
    const LocalModel &model = download->model;
    QUrl url = m_server;
    url.setPath(QStringLiteral("/%1/resolve/%2/%3")
                    .arg(model.huggingFaceRepo, model.revision, model.fileName));
    QNetworkRequest request(url);
    request.setTransferTimeout(transferTimeoutMs);
    if (download->resumedFrom > 0) {
        request.setRawHeader("Range", "bytes=" + QByteArray::number(download->resumedFrom) + '-');
    }
    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
    }
    download->reply = m_network->get(request);
    connect(download->reply, &QNetworkReply::readyRead, this,
            [this, download] { writeReceived(download); });
    connect(download->reply, &QNetworkReply::finished, this,
            [this, download] { finishReply(download); });
}

void LocalModelStore::Worker::writeReceived(Download *download)
{
    const QByteArray data = download->reply->readAll();
    const int status = download->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // An error page is not part of the model; finished() reports the error.
    if (status != 200 && status != 206) {
        return;
    }
    // A server that ignores Range sends the whole file again.
    if (status == 200 && download->resumedFrom > 0) {
        download->file.resize(0);
        download->resumedFrom = 0;
    }
    if (download->file.write(data) != data.size()) {
        download->reply->abort();
        return;
    }
    QMetaObject::invokeMethod(m_store,
                              [store = m_store, id = download->model.id, serial = download->serial,
                               received = download->file.size(), total = download->model.sizeBytes] {
                                  store->reportProgress(id, serial, received, total);
                              },
                              Qt::QueuedConnection);
}

void LocalModelStore::Worker::finishReply(Download *download)
{
    QNetworkReply *reply = download->reply;
    download->reply = nullptr;
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // The server has no bytes past the partial file, so it is not a prefix of
    // this model: start over rather than ask for the same range again.
    if (status == rangeNotSatisfiable && download->resumedFrom > 0) {
        download->file.resize(0);
        download->resumedFrom = 0;
        request(download);
        return;
    }
    if (download->file.error() != QFileDevice::NoError) {
        end(download, QStringLiteral("Speecher could not save the download: %1.")
                          .arg(download->file.errorString()));
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        end(download, QStringLiteral("The download stopped: %1. Download again to resume.")
                          .arg(reply->errorString()));
        return;
    }
    download->file.close();
    verifyAndInstall(download);
}

void LocalModelStore::Worker::verifyAndInstall(Download *download)
{
    const QString path = download->file.fileName();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        end(download, QStringLiteral("Speecher could not read the download back from %1.").arg(path));
        return;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        // cancel() waits for this thread, so stop hashing as soon as it asks.
        if (*download->cancelled) {
            return;
        }
        hash.addData(file.read(hashChunkBytes));
    }
    file.close();
    if (hash.result().toHex() != download->model.sha256.toLatin1()) {
        QFile::remove(path);
        end(download, QStringLiteral("The downloaded file was damaged, so Speecher deleted it. Download it again."));
        return;
    }
    const QString finalPath = modelPath(download->model);
    QFile::remove(finalPath);
    if (!QFile::rename(path, finalPath)) {
        end(download, QStringLiteral("Speecher could not move the finished download into %1.")
                          .arg(m_directory));
        return;
    }
    end(download, {});
}

void LocalModelStore::Worker::end(Download *download, const QString &error)
{
    QMetaObject::invokeMethod(m_store,
                              [store = m_store, id = download->model.id, serial = download->serial,
                               error] { store->reportEnd(id, serial, error); },
                              Qt::QueuedConnection);
    m_downloads.erase(download->model.id);
}

void LocalModelStore::Worker::stop(const QString &modelId)
{
    const auto it = m_downloads.find(modelId);
    if (it == m_downloads.end()) {
        return;
    }
    if (QNetworkReply *reply = it->second->reply) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    // Destroying the download closes the partial file.
    m_downloads.erase(it);
}

LocalModelStore::LocalModelStore(QObject *parent)
    : LocalModelStore(defaultDirectory(), QUrl(QStringLiteral("https://huggingface.co")), parent)
{
}

LocalModelStore::LocalModelStore(const QString &directory, const QUrl &server, QObject *parent)
    : QObject(parent)
    , m_directory(directory)
    , m_worker(new Worker(this, directory, server))
{
    m_thread.setObjectName(QStringLiteral("LocalModelStore"));
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
}

LocalModelStore::~LocalModelStore()
{
    for (const QString &modelId : m_downloads.keys()) {
        cancel(modelId);
    }
    m_thread.quit();
    m_thread.wait();
}

QString LocalModelStore::directory() const
{
    return m_directory;
}

QString LocalModelStore::modelPath(const LocalModel &model) const
{
    return QDir(m_directory).filePath(model.fileName);
}

bool LocalModelStore::isDownloaded(const LocalModel &model) const
{
    return QFileInfo(modelPath(model)).size() == model.sizeBytes;
}

bool LocalModelStore::isDownloading(const QString &modelId) const
{
    return m_downloads.contains(modelId);
}

void LocalModelStore::download(const LocalModel &model)
{
    if (m_downloads.contains(model.id)) {
        return;
    }
    const Running running{++m_lastSerial, std::make_shared<std::atomic_bool>(false)};
    m_downloads.insert(model.id, running);
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, model, running] {
        worker->start(model, running.serial, running.cancelled);
    }, Qt::QueuedConnection);
}

void LocalModelStore::cancel(const QString &modelId)
{
    const auto it = m_downloads.constFind(modelId);
    if (it == m_downloads.cend()) {
        return;
    }
    *it->cancelled = true;
    m_downloads.erase(it);
    // Blocking: remove() deletes the partial file next, which Windows refuses
    // while the worker still has it open.
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, modelId] {
        worker->stop(modelId);
    }, Qt::BlockingQueuedConnection);
}

bool LocalModelStore::remove(const LocalModel &model)
{
    cancel(model.id);
    const QString path = modelPath(model);
    QFile::remove(partPath(path));
    QFile::remove(path);
    return !QFile::exists(partPath(path)) && !QFile::exists(path);
}

void LocalModelStore::reportProgress(const QString &modelId, quint64 serial, qint64 received, qint64 total)
{
    if (m_downloads.value(modelId).serial == serial) {
        emit downloadProgress(modelId, received, total);
    }
}

void LocalModelStore::reportEnd(const QString &modelId, quint64 serial, const QString &error)
{
    if (m_downloads.value(modelId).serial != serial) {
        return;
    }
    m_downloads.remove(modelId);
    if (error.isEmpty()) {
        emit downloadFinished(modelId);
    } else {
        emit downloadFailed(modelId, error);
    }
}

} // namespace speecher
