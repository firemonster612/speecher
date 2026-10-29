#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace speecher {

class TranscriptState : public QObject {
    Q_OBJECT

public:
    explicit TranscriptState(QObject *parent = nullptr);

    QString text() const;
    QString partial() const;
    bool isEmpty() const;

public slots:
    void clear();
    void setPartial(const QString &partial);
    void commitFinal(const QString &finalText);
    void replaceFinals(const QString &text);
    // Text from here on comes from a new speech attempt: the text so far
    // stays when the attempt sends its whole transcript.
    void startAttempt();
    // The current attempt's whole transcript, in place of what it sent before.
    void replaceAttemptFinals(const QString &attemptText);

signals:
    void changed(const QString &text);

private:
    QStringList m_finals;
    QString m_partial;
    // The text from before the current attempt.
    QString m_attemptBase;
};

} // namespace speecher
