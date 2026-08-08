#pragma once

#include "dictation/DictationInterfaces.h"

namespace speecher {

class AtSpiTargetProvider final : public TargetProvider {
    Q_OBJECT

public:
    using TargetProvider::TargetProvider;
    ~AtSpiTargetProvider() override;
    Target capture() override;
    bool stillFocused(const Target &target) override;
    bool verifyInsertion(const Target &target, const QString &plainText) override;

private:
    void clearAccessible();
    void observeCorrection(const Target &target,
                           const QString &original,
                           const QString &prefix,
                           const QString &suffix,
                           quint64 generation);
    void *m_accessible = nullptr;
    Target m_snapshot;
    quint64 m_observationGeneration = 0;
};

} // namespace speecher
