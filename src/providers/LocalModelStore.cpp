#include "providers/LocalModelStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkReply>
#include <QPointer>
#include <QStandardPaths>
#include <QThread>

#include <atomic>

namespace speecher {
namespace {

constexpr qint64 hashChunkBytes = qint64(1) << 20;
// A download that receives nothing for this long has stalled.
constexpr int transferTimeoutMs = 30000;

QString defaultDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("models"));
}

} // namespace

struct LocalModelStore::Download {
    LocalModel model;
    QFile file;
    QNetworkReply *reply = nullptr;
    qint64 resumedFrom = 0;
    QThread *verifier = nullptr;
    std::atomic_bool cancelled = false;
};

LocalModelStore::LocalModelStore(QObject *parent)
    : LocalModelStore(defaultDirectory(), QUrl(QStringLiteral("https://huggingface.co")), parent)
{
}

LocalModelStore::LocalModelStore(const QString &directory, const QUrl &server, QObject *parent)
    : QObject(parent)
    , m_directory(directory)
    , m_server(server)
{
}

LocalModelStore::~LocalModelStore()
{
    for (const QString &modelId : m_downloads.keys()) {
        cancel(modelId);
    }
}

QString LocalModelStore::directory() const
{
    return m_directory;
}

QString LocalModelStore::modelPath(const LocalModel &model) const
{
    return QDir(m_directory).filePath(model.fileName);
}

QString LocalModelStore::partPath(const LocalModel &model) const
{
    return modelPath(model) + QStringLiteral(".part");
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
    auto *download = new Download;
    download->model = model;
    download->file.setFileName(partPath(model));
    m_downloads.insert(model.id, download);
    if (!QDir().mkpath(m_directory)
        || !download->file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        fail(model.id, QStringLiteral("Speecher could not write to %1.").arg(m_directory));
        return;
    }
    if (download->file.size() > model.sizeBytes) {
        download->file.resize(0);
    }
    download->resumedFrom = download->file.size();
    if (download->resumedFrom == model.sizeBytes) {
        download->file.close();
        verify(model);
        return;
    }

    QUrl url = m_server;
    url.setPath(QStringLiteral("/%1/resolve/%2/%3")
                    .arg(model.huggingFaceRepo, model.revision, model.fileName));
    QNetworkRequest request(url);
    request.setTransferTimeout(transferTimeoutMs);
    if (download->resumedFrom > 0) {
        request.setRawHeader("Range", "bytes=" + QByteArray::number(download->resumedFrom) + '-');
    }
    download->reply = m_network.get(request);
    const QString modelId = model.id;
    connect(download->reply, &QNetworkReply::readyRead, this,
            [this, download] { writeReceived(download); });
    connect(download->reply, &QNetworkReply::finished, this,
            [this, modelId] { finishReply(modelId); });
}

void LocalModelStore::writeReceived(Download *download)
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
    emit downloadProgress(download->model.id, download->file.size(), download->model.sizeBytes);
}

void LocalModelStore::finishReply(const QString &modelId)
{
    Download *download = m_downloads.value(modelId);
    QNetworkReply *reply = download->reply;
    download->reply = nullptr;
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // The server has no bytes past the partial file, so it is not a prefix of
    // this model: start over rather than ask for the same range again.
    constexpr int rangeNotSatisfiable = 416;
    if (status == rangeNotSatisfiable && download->resumedFrom > 0) {
        const LocalModel model = download->model;
        delete m_downloads.take(modelId);
        QFile::remove(partPath(model));
        this->download(model);
        return;
    }
    if (download->file.error() != QFileDevice::NoError) {
        fail(modelId, QStringLiteral("Speecher could not save the download: %1.")
                          .arg(download->file.errorString()));
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        fail(modelId, QStringLiteral("The download stopped: %1. Download again to resume.")
                          .arg(reply->errorString()));
        return;
    }
    download->file.close();
    verify(download->model);
}

void LocalModelStore::verify(const LocalModel &model)
{
    Download *download = m_downloads.value(model.id);
    auto matches = std::make_shared<bool>(false);
    const QString path = partPath(model);
    download->verifier = QThread::create([download, path, expected = model.sha256, matches] {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!file.atEnd() && !download->cancelled) {
            hash.addData(file.read(hashChunkBytes));
        }
        *matches = !download->cancelled && hash.result().toHex() == expected.toLatin1();
    });
    const QPointer<QThread> verifier = download->verifier;
    connect(verifier, &QThread::finished, this, [this, model, matches, verifier] {
        Download *download = m_downloads.value(model.id);
        // cancel() already joined and deleted this verifier.
        if (!download || !verifier || download->verifier != verifier) {
            return;
        }
        // finished() comes just before the thread ends.
        download->verifier->wait();
        delete download->verifier;
        download->verifier = nullptr;
        if (!*matches) {
            QFile::remove(partPath(model));
            fail(model.id, QStringLiteral("The downloaded file was damaged, so Speecher deleted it. Download it again."));
            return;
        }
        QFile::remove(modelPath(model));
        if (!QFile::rename(partPath(model), modelPath(model))) {
            fail(model.id, QStringLiteral("Speecher could not move the finished download into %1.")
                               .arg(m_directory));
            return;
        }
        delete m_downloads.take(model.id);
        emit downloadFinished(model.id);
    });
    download->verifier->start();
}

void LocalModelStore::cancel(const QString &modelId)
{
    Download *download = m_downloads.take(modelId);
    if (!download) {
        return;
    }
    download->cancelled = true;
    if (download->verifier) {
        // The hash loop checks the flag every megabyte, so this is brief, and
        // it closes the file before remove() deletes it (Windows refuses to
        // delete an open file).
        download->verifier->wait();
        delete download->verifier;
    }
    if (download->reply) {
        download->reply->disconnect(this);
        download->reply->abort();
        download->reply->deleteLater();
    }
    delete download;
}

bool LocalModelStore::remove(const LocalModel &model)
{
    cancel(model.id);
    QFile::remove(partPath(model));
    QFile::remove(modelPath(model));
    return !QFile::exists(partPath(model)) && !QFile::exists(modelPath(model));
}

void LocalModelStore::fail(const QString &modelId, const QString &message)
{
    delete m_downloads.take(modelId);
    emit downloadFailed(modelId, message);
}

} // namespace speecher
