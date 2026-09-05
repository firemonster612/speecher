#pragma once

#include "dictation/DictationPorts.h"

#include <memory>

namespace speecher {

class WinMediaController : public MediaController {
    Q_OBJECT

public:
    explicit WinMediaController(QObject *parent = nullptr);
    ~WinMediaController() override;

    void pausePlaying() override;
    void resumePaused() override;

private:
    std::shared_ptr<struct WinMediaState> m_native;
};

} // namespace speecher
