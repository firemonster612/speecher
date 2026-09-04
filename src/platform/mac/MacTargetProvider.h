#pragma once

#include "dictation/DictationPorts.h"

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

    Target capture(const QList<AppRecognitionRule> &recognitionRules = {}) override;
    bool stillFocused(const Target &target) override;
    bool canInsertText(const Target &target) override;
    bool insertText(const Target &target, const QString &plainText, QString *error = nullptr) override;
    bool verifyInsertion(const Target &target, const QString &plainText) override;
    void setCorrectionObservationEnabled(bool enabled) override;

private:
    void releaseFocusedElement();
    void observeCorrections(const Target &target,
                            const QString &value,
                            int insertedAt,
                            const QString &plainText);

    // AXUIElementRef, kept opaque so this header stays plain C++ for moc.
    void *m_focusedElement = nullptr;
    // The control's value just before insertText wrote to it. Empty optional on
    // the paste path, where there is no baseline to compare against.
    std::optional<QString> m_valueBeforeInsertion;
    // The AX selection location immediately before the write, so repeated text
    // cannot make verification attach correction learning to an older copy.
    std::optional<int> m_insertionOffset;
    std::unique_ptr<mac::CorrectionObserver> m_correctionObserver;
    bool m_correctionObservationEnabled = true;
};

} // namespace speecher
