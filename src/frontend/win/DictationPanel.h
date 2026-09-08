#pragma once

#include "app/UpdateController.h"
#include "dictation/DictationTypes.h"

#include <memory>

#include <QObject>
#include <QString>

namespace speecher {

namespace win {
struct UpdateChipState {
    QString text;
    QString action;
    bool visible = false;
    bool enabled = false;
};

UpdateChipState updateChipState(UpdateController::State state, const QString &version,
                                int percent, const QString &error, bool repeatedFailure,
                                bool manualInstall, DictationState sessionState);
} // namespace win

class ApplicationController;
class WinFrontEnd;
class WinFrontEndTests;

class DictationPanel final : public QObject {
    Q_OBJECT

public:
    explicit DictationPanel(ApplicationController *controller, QObject *parent = nullptr);
    ~DictationPanel() override;

    void showProblem(const QString &message);

signals:
    void whatsNewRequested();

private:
    friend class WinFrontEndTests;
    friend class WinFrontEnd;
    void showForTest(quint64 generation);
    void dismissForTest();
    bool visibleForTest() const;
    quint64 presentedGenerationForTest() const;
    qintptr windowStyleForTest() const;
    // Drive the panel's states without a live dictation session, and film the
    // result, for the UI-evidence grabs on the pattern of the E2E rigs.
    void driveStatusForTest(const QString &status);
    void drivePreviewForTest(const QString &preview);
    void driveLevelForTest(float level);
    bool saveGrabForTest(const QString &path) const;
    struct Native;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
