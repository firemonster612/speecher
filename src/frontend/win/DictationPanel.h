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
    // A selection edit review's buttons, which WinFrontEnd sends to the
    // controller and the session.
    void keepOriginalRequested();
    void replaceSelectionRequested();

private:
    friend class WinFrontEndTests;
    friend class WinFrontEnd;
    void showForTest(quint64 generation);
    void dismissForTest();
    bool visibleForTest() const;
    quint64 presentedGenerationForTest() const;
    qintptr windowStyleForTest() const;
    // The fix button an error or an outcome offers, and pressing it.
    bool fixVisibleForTest() const;
    // Whether Dismiss or the countdown bar shows, which only an error has.
    bool errorChromeVisibleForTest() const;
    void pressFixForTest();
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
    QRect statusGeometryForTest() const;
    QRect pauseGeometryForTest() const;
    QRect spinnerGeometryForTest() const;
    QRect cancelGeometryForTest() const;
    QString previewTextForTest() const;
    bool previewTextFitsForTest() const;
    // Whether the preview's start fades, as it does when words were cut.
    bool previewFadesForTest() const;
    // The carved outline's shoulder height and lobe width in DIPs; a zero
    // shoulder means the capsule fell back to a plain rounded box.
    double outlineShoulderForTest() const;
    double outlineLobeWidthForTest() const;
    // A selection edit's review card: where it is, the edit's text as shown,
    // the Show all toggle's caption (empty while it is hidden), whether the
    // edit scrolls and Replace looks like the default button, and pressing
    // its controls.
    QRect reviewGeometryForTest() const;
    // Whether the whole card shows: laid out inside the window and inside
    // the window region that clips it. False until WinUI has laid the card
    // out in the island's new size.
    bool reviewFullyVisibleForTest() const;
    QString reviewTextForTest() const;
    QString reviewToggleForTest() const;
    bool reviewScrollsForTest() const;
    bool reviewReplaceIsDefaultForTest() const;
    void pressReviewToggleForTest();
    void pressKeepOriginalForTest();
    void pressReplaceForTest();
    struct Native;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
