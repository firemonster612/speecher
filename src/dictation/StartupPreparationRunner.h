#pragma once

#include "dictation/DictationInterfaces.h"

#include <QList>
#include <QObject>

#include <memory>
#include <optional>

class QThread;

namespace speecher {

struct StartupPreparationResult {
    quint64 generation = 0;
    SpeechPrepareResult speech;
    RefinementRefreshResult refinerRefresh;
    bool refinerRefreshAttempted = false;
};

class StartupPreparationRunner : public QObject {
    Q_OBJECT

public:
    explicit StartupPreparationRunner(QObject *parent = nullptr);
    ~StartupPreparationRunner() override;

    void start(quint64 generation,
               std::optional<SpeechPrepareJob> speechJob,
               std::optional<RefinementRefreshJob> refinerJob,
               SpeechPrepareResult speechPrepared);
    void cancel();

signals:
    void completed(const speecher::StartupPreparationResult &result);

private:
    struct Preparation;
    std::shared_ptr<Preparation> m_current;
    QList<QThread *> m_threads;
};

} // namespace speecher

Q_DECLARE_METATYPE(speecher::StartupPreparationResult)
