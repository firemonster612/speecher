#pragma once

#include "dictation/DictationPorts.h"
#include "platform/InsertionBaseline.h"

#include <QSet>
#include <QString>

#include <memory>
#include <optional>

namespace speecher {

namespace mac {
class CorrectionObserver;
}

// Identifies the frontmost application through NSWorkspace, then reaches for its
// focused control through the system-wide accessibility element. The app
// identity is always available; everything below it needs the Accessibility
// grant, so a capture without it still names the target but cannot edit it.
class MacTargetProvider : public TargetProvider {
    Q_OBJECT

public:
    explicit MacTargetProvider(QObject *parent = nullptr);
    ~MacTargetProvider() override;

    // Answers at once, except when the control's text can't be read in an app
    // not yet asked for its accessibility tree: that capture asks, then answers
    // from a second look once the tree has had time to build.
    void capture(QObject *context, std::function<void(const Target &)> done) override;
    bool stillFocused(const Target &target) override;
    bool canInsertText(const Target &target) override;
    bool insertText(const Target &target, const QString &plainText, QString *error = nullptr) override;
    bool preparePaste(const Target &target) override;
    void verifyInsertion(const Target &target,
                         const QString &plainText,
                         QObject *context,
                         std::function<void(bool shown)> done) override;
    void setCorrectionObservationEnabled(bool enabled) override;

private:
    Target captureNow();
    bool requestAccessibilityTree(qint64 processId);
    void releaseFocusedElement();
    void observeCorrections(const CorrectionWindow &window);

    // AXUIElementRef, kept opaque so this header stays plain C++ for moc.
    void *m_focusedElement = nullptr;
    quint32 m_windowId = 0;
    // Taken immediately before direct insertion or keyboard paste. Without one,
    // nothing can prove that delivery changed the control.
    std::optional<InsertionBaseline> m_insertionBaseline;
    std::unique_ptr<mac::CorrectionObserver> m_correctionObserver;
    // Counts captures, so a verification can tell that the element it reads
    // now belongs to a later capture, even one of the same control.
    quint64 m_captures = 0;
    bool m_correctionObservationEnabled = true;
    // Apps already asked for their accessibility tree, and of those, the ones
    // whose AXEnhancedUserInterface Speecher turned on and turns off again.
    QSet<qint64> m_treeRequested;
    QSet<qint64> m_enhancedInterfaceEnabled;
};

} // namespace speecher
