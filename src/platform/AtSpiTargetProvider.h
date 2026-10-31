#pragma once

#include "dictation/DictationPorts.h"

#include <memory>

namespace speecher {

namespace atspi {
class CorrectionObserver;
class TargetSnapshot;
}

class KWinActiveWindow;
struct CorrectionWindow;

class AtSpiTargetProvider final : public TargetProvider {
    Q_OBJECT

public:
    explicit AtSpiTargetProvider(QObject *parent = nullptr);
    ~AtSpiTargetProvider() override;
    // Both answer at once for an accessible target, and once KWin has for a
    // compositor-fallback one.
    void capture(QObject *context, std::function<void(const Target &)> done) override;
    void focusMatch(const Target &target, QObject *context, std::function<void(FocusMatch)> done) override;
    bool canInsertText(const Target &target) override;
    bool insertText(const Target &target, const QString &plainText, QString *error = nullptr) override;
    void verifyInsertion(const Target &target,
                         const QString &plainText,
                         QObject *context,
                         std::function<void(bool shown)> done) override;
    void setCorrectionObservationEnabled(bool enabled) override;

    // Identity of a compositor-fallback window: the compositor's own window
    // handle where there is one, otherwise the process, its window class and
    // its caption.
    static FocusMatch compositorWindowMatch(const Target &a, const Target &b);

private:
    void clearAccessible();
    void observeCorrections(const CorrectionWindow &window);
    // The window KWin has in front, or an empty Target off KDE.
    void compositorFallbackTarget(std::function<void(const Target &)> done);

    std::unique_ptr<atspi::TargetSnapshot> m_snapshot;
    std::unique_ptr<atspi::CorrectionObserver> m_correctionObserver;
    std::unique_ptr<KWinActiveWindow> m_activeWindow;
    // Counts captures. A new snapshot can take the freed one's address, so
    // this, not the pointer, says whether the snapshot was replaced.
    quint64 m_captures = 0;
    bool m_correctionObservationEnabled = true;
};

} // namespace speecher
