#pragma once

#include "transcribe/TranscribeQueue.h"

#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

namespace speecher {

class ApplicationController;

// The UI's Transcription singleton: the Transcribe page's queue in plain
// values, and its commands. A file and a result are maps:
//   file: { path, name, kind (audio|video), bytes, durationMs (-1 while
//     unknown) }
//   result: the file's fields and { failed, error (why it failed, or for a
//     finished file what went wrong on the way), text (the transcript as
//     shown, refined or raw), words, timings (it exports as subtitles),
//     savedPath }
// The page asks for files and folders with its own choosers and hands the
// chosen ones in.
class QuickTranscription final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Transcription)
    QML_SINGLETON
    // 0 configure, 1 transcribe, 2 export.
    Q_PROPERTY(int step READ step NOTIFY stepChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    // The extensions, without the dot, of the files the chooser offers.
    Q_PROPERTY(QStringList extensions READ extensions CONSTANT)
    // The next batch's choices: { service, vocabulary, refiner, cleanup,
    // profile, tone, language (a spoken language id), destination (beside,
    // folder or none), folder (as shown), folderUrl }.
    Q_PROPERTY(QVariantMap options READ options NOTIFY optionsChanged)
    // The Local Model the batch listens with, or empty for another service.
    Q_PROPERTY(QString speechModel READ speechModel NOTIFY optionsChanged)
    // The model the chosen refiner runs, or empty.
    Q_PROPERTY(QString refinerModel READ refinerModel NOTIFY optionsChanged)
    // The batch as it started: { files, speech and refinement (who hears
    // it and who refines it, refinement empty when it does not), refines,
    // destination, folder, cancelled }.
    Q_PROPERTY(QVariantMap batch READ batch NOTIFY batchChanged)
    // The file being transcribed, or -1.
    Q_PROPERTY(int current READ current NOTIFY fileChanged)
    // reading, transcribing, finishing or refining.
    Q_PROPERTY(QString phase READ phase NOTIFY fileChanged)
    // The current file's levels, 0..1 across its length; empty until read.
    Q_PROPERTY(QVariantList peaks READ peaks NOTIFY fileChanged)
    Q_PROPERTY(QString partial READ partial NOTIFY fileChanged)
    // Each batch file's state: waiting, current, done or failed.
    Q_PROPERTY(QStringList queue READ queue NOTIFY fileChanged)
    // How far the current file is, 0..1.
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    // The result a retry runs for, or -1.
    Q_PROPERTY(int retrying READ retrying NOTIFY resultsChanged)
    // Results show the raw transcript rather than the refined one.
    Q_PROPERTY(bool showRaw READ showRaw WRITE setShowRaw NOTIFY resultsChanged)
    // Why the last start, retry or export failed; empty once one works.
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)

public:
    explicit QuickTranscription(ApplicationController *controller, QObject *parent = nullptr);
    ~QuickTranscription() override;

    // The engine's instance: the one the host made before loading any QML.
    static QuickTranscription *create(QQmlEngine *, QJSEngine *);

    // Files opened from outside, such as a file manager's Open With.
    void addFiles(const QStringList &paths);

    int step() const;
    QVariantList files() const;
    QStringList extensions() const;
    QVariantMap options() const;
    QString speechModel() const;
    QString refinerModel() const;
    QVariantMap batch() const;
    int current() const;
    QString phase() const;
    QVariantList peaks() const;
    QString partial() const;
    QStringList queue() const;
    qreal progress() const;
    QVariantList results() const;
    int retrying() const;
    bool showRaw() const;
    void setShowRaw(bool raw);
    QString problem() const;

    // Chosen or dropped files, as URLs.
    Q_INVOKABLE void addUrls(const QList<QUrl> &urls);
    Q_INVOKABLE void removeFile(const QString &path);
    // Sets one of options, or speechModel or refinerModel, the models the
    // batch's services run, which a new service or profile puts back to
    // theirs; profile brings its services, cleanup and tone.
    Q_INVOKABLE void setOption(const QString &id, const QVariant &value);
    // The folder transcripts go in. An empty one, the chooser dismissed,
    // while there is none yet sends the transcripts beside each file instead.
    Q_INVOKABLE void setFolder(const QUrl &folder);
    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();
    // Back to the setup step for the next batch.
    Q_INVOKABLE void reset();
    Q_INVOKABLE void retry(int index);
    Q_INVOKABLE void copy(int index);
    Q_INVOKABLE void copyAll();
    // Where Export offers to save a result as text, srt or vtt: beside its
    // file, named after it. Empty for a result without that format.
    Q_INVOKABLE QUrl exportFile(int index, const QString &format) const;
    // Saves one result in the format to file; exported(index) says it was.
    Q_INVOKABLE void exportResult(int index, const QString &format, const QUrl &file);
    // Saves every result that has the format in folder, numbering rather
    // than overwriting; exported(-1) says it was.
    Q_INVOKABLE void exportAll(const QString &format, const QUrl &folder);

signals:
    void stepChanged();
    void filesChanged();
    void optionsChanged();
    void batchChanged();
    void fileChanged();
    void progressChanged();
    void resultsChanged();
    void problemChanged();
    // A result, or all of them for -1, was written where asked.
    void exported(int index);

private:
    ApplicationController *m_controller;
    TranscribeQueue m_queue;
    bool m_showRaw = false;
    // Moves progress on through the waits while a batch runs.
    QTimer m_progressClock;
    qreal m_progress = 0;
};

} // namespace speecher
