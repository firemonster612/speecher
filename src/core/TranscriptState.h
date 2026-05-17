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

signals:
    void changed(const QString &text);

private:
    QStringList m_finals;
    QString m_partial;
};

} // namespace speecher
