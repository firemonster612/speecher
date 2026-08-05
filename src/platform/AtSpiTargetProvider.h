#pragma once

#include "dictation/DictationInterfaces.h"

#include <memory>

namespace speecher {

namespace atspi {
class CorrectionObserver;
class TargetSnapshot;
}

class AtSpiTargetProvider final : public TargetProvider {
    Q_OBJECT

public:
    explicit AtSpiTargetProvider(QObject *parent = nullptr);
    ~AtSpiTargetProvider() override;
    Target capture() override;
    bool stillFocused(const Target &target) override;
    bool canInsertText(const Target &target) override;
    bool insertText(const Target &target, const QString &plainText, QString *error = nullptr) override;
    bool verifyInsertion(const Target &target, const QString &plainText) override;

private:
    void clearAccessible();
    std::unique_ptr<atspi::TargetSnapshot> m_snapshot;
    std::unique_ptr<atspi::CorrectionObserver> m_correctionObserver;
};

} // namespace speecher
