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
    // The Spoken Language the last attempt whose words the transcript holds
    // listened for; empty before any words arrive.
    QString heardLanguage() const;

public slots:
    void clear();
    void setPartial(const QString &partial);
    void commitFinal(const QString &finalText);
    void replaceFinals(const QString &text);
    // Text from here on comes from a new speech attempt, listening for
    // spokenLanguage: the text so far stays when the attempt sends its whole
    // transcript.
    void startAttempt(const QString &spokenLanguage);
    // The current attempt's whole transcript, in place of what it sent before.
    void replaceAttemptFinals(const QString &attemptText);

signals:
    void changed(const QString &text);

private:
    QStringList m_finals;
    QString m_partial;
    // The text from before the current attempt.
    QString m_attemptBase;
    QString m_attemptLanguage;
    QString m_heardLanguage;
};

} // namespace speecher
