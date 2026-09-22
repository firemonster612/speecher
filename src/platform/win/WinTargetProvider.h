#pragma once

#include "dictation/DictationPorts.h"

#include <memory>
#include <optional>

namespace speecher {

class WinCorrectionObserver;

class WinTargetProvider : public TargetProvider {
    Q_OBJECT

public:
    explicit WinTargetProvider(QObject *parent = nullptr);
    ~WinTargetProvider() override;

    Target capture(const QList<AppRecognitionRule> &recognitionRules = {}) override;
    bool stillFocused(const Target &target) override;
    bool canInsertText(const Target &target) override;
    bool insertText(const Target &target,
                    const QString &plainText,
                    QString *error = nullptr) override;
    bool preparePaste(const Target &target) override;
    bool verifyInsertion(const Target &target, const QString &plainText) override;
    void setCorrectionObservationEnabled(bool enabled) override;

private:
    struct Native;
    void clearCapture();
    void observeCorrections(const Target &target,
                            const QString &value,
                            int insertedAt,
                            const QString &plainText);

    std::unique_ptr<Native> m_native;
    std::optional<QString> m_valueBeforeInsertion;
    std::optional<int> m_insertionOffset;
    // End of the selection the insertion replaced, so verification can require
    // the exact anchored transformation of the saved value rather than accept
    // any change that happens to show the text at the offset.
    std::optional<int> m_replacedSelectionEnd;
    std::unique_ptr<WinCorrectionObserver> m_correctionObserver;
    bool m_correctionObservationEnabled = true;
};

} // namespace speecher
