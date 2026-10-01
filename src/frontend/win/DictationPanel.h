#pragma once

#include <memory>

#include "dictation/PopupPresentation.h"

#include <QObject>
#include <QRect>
#include <QString>

namespace speecher {

class ApplicationController;
class WinFrontEnd;
class WinFrontEndTests;

class DictationPanel final : public QObject {
    Q_OBJECT

public:
    explicit DictationPanel(ApplicationController *controller, QObject *parent = nullptr);
    ~DictationPanel() override;

    // An error with the countdown every platform shows, and a button for its
    // fix where this platform has one.
    void showProblem(const QString &message, const PopupErrorAction &fix = {});

signals:
    void whatsNewRequested();
    // The error's fix button was pressed.
    void fixRequested(const PopupErrorAction &fix);

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
    int levelBarCountForTest() const;
    QRect capsuleGeometryForTest() const;
    QRect waveformGeometryForTest() const;
    QRect previewGeometryForTest() const;
    QString previewTextForTest() const;
    bool previewTextFitsForTest() const;
    struct Native;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
