#pragma once

#include "dictation/DictationPorts.h"

namespace speecher {

// Identifies the frontmost application through NSWorkspace, then reaches for its
// focused control through the system-wide accessibility element. The app
// identity is always available; everything below it needs the Accessibility
// grant, so a capture without it still names the target but cannot edit it.
class MacTargetProvider : public TargetProvider {
    Q_OBJECT

public:
    explicit MacTargetProvider(QObject *parent = nullptr);
    ~MacTargetProvider() override;

    Target capture(const QList<AppRecognitionRule> &recognitionRules = {}) override;
    bool stillFocused(const Target &target) override;
    bool canInsertText(const Target &target) override;
    bool insertText(const Target &target, const QString &plainText, QString *error = nullptr) override;
    bool verifyInsertion(const Target &target, const QString &plainText) override;
    void setCorrectionObservationEnabled(bool enabled) override;

private:
    void releaseFocusedElement();

    // AXUIElementRef, kept opaque so this header stays plain C++ for moc.
    void *m_focusedElement = nullptr;
};

} // namespace speecher
