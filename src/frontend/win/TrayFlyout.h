#pragma once

#include <memory>

#include <QObject>
#include <QRect>

struct tagRECT;

namespace speecher {

class ApplicationController;

class TrayFlyout final : public QObject {
public:
    explicit TrayFlyout(ApplicationController *controller, QObject *parent = nullptr);
    ~TrayFlyout() override;

    void show(const tagRECT &iconRect);
    void hide();

    // For tests: the window and its last button, Settings, in screen pixels
    // (empty while hidden), and the window's pixels.
    QRect geometryForTest() const;
    QRect settingsGeometryForTest() const;
    bool saveGrabForTest(const QString &path) const;

private:
    struct Native;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
