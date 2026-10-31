#pragma once

#include "dictation/DictationPorts.h"
#include "platform/InsertionBaseline.h"

#include <memory>
#include <optional>

namespace speecher {

class WinCorrectionObserver;

class WinTargetProvider : public TargetProvider {
    Q_OBJECT

public:
    explicit WinTargetProvider(QObject *parent = nullptr);
    ~WinTargetProvider() override;

    // Answers at once: the native APIs answer before they return.
    void capture(QObject *context, std::function<void(const Target &)> done) override;
    bool stillFocused(const Target &target) override;
    bool canInsertText(const Target &target) override;
    bool insertText(const Target &target,
                    const QString &plainText,
                    QString *error = nullptr) override;
    bool preparePaste(const Target &target) override;
    void verifyInsertion(const Target &target,
                         const QString &plainText,
                         QObject *context,
                         std::function<void(bool shown)> done) override;
    void setCorrectionObservationEnabled(bool enabled) override;

private:
    Target captureNow();
    struct Native;
    void clearCapture();
    void observeCorrections(const CorrectionWindow &window);

    std::unique_ptr<Native> m_native;
    // Taken immediately before direct insertion or keyboard paste. Without one,
    // nothing can prove that delivery changed the control.
    std::optional<InsertionBaseline> m_insertionBaseline;
    std::unique_ptr<WinCorrectionObserver> m_correctionObserver;
    bool m_correctionObservationEnabled = true;
};

} // namespace speecher
