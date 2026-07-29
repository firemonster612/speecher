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
    void *m_accessible = nullptr;
    Target m_snapshot;
};

} // namespace speecher
