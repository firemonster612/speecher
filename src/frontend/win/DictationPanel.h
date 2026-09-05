#pragma once

#include <memory>

#include <QObject>
#include <QString>

namespace speecher {

class ApplicationController;
class WinFrontEnd;
class WinFrontEndTests;

class DictationPanel final : public QObject {
public:
    explicit DictationPanel(ApplicationController *controller, QObject *parent = nullptr);
    ~DictationPanel() override;

    void showProblem(const QString &message);

private:
    friend class WinFrontEndTests;
    friend class WinFrontEnd;
    void showForTest(quint64 generation);
    void dismissForTest();
    bool visibleForTest() const;
    quint64 presentedGenerationForTest() const;
    qintptr windowStyleForTest() const;
    struct Native;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
