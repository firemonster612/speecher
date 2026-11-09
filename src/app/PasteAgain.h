#pragma once

// Paste Again: a text delivered once more into the app in front, through the
// Delivery Steps and that app's Pasting choice, as a dictation delivers.

#include <QObject>
#include <QString>
#include <QTimer>

namespace speecher {

class SettingsStore;
class TargetProvider;
class TextDeliveryAdapter;

class PasteAgain : public QObject {
    Q_OBJECT

public:
    // delivery is Paste Again's own, apart from the session's, whose answers
    // the session counts.
    PasteAgain(SettingsStore &settings, TargetProvider *targets, TextDeliveryAdapter *delivery,
               QObject *parent = nullptr);

    // Waits for Speecher's own windows to give up focus, at most
    // focusWaitMs, captures the app in front and delivers text there. With
    // no app in front it copies text instead. A paste asked for while one
    // is under way replaces it.
    void paste(const QString &text);

    static constexpr int focusWaitMs = 2000;

signals:
    // What became of it: ok, or why not, in words for the person.
    void finished(bool ok, const QString &message);

private:
    void captureAndDeliver();
    void copyInstead();

    SettingsStore &m_settings;
    TargetProvider *m_targets;
    TextDeliveryAdapter *m_delivery;
    QString m_text;
    QTimer m_waitForFocus;
    int m_waitedMs = 0;
    quint64 m_generation = 0;
};

} // namespace speecher
