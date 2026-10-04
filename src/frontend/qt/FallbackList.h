#pragma once

#include "core/settings/FallbackPresentation.h"
#include "frontend/qt/SchemaSettingsPage.h"

#include <QWidget>

class QFormLayout;

namespace speecher {

class LocalSetup;
class ProviderRegistry;

// One role's fallbacks as rows of a settings card, as core presents them: a
// row per fallback with Move up, Move down and Remove, then the Add row. The
// Fallbacks subpage and the setup assistant both show it, each around its own
// heading; what an edit changes is theirs to say.
class FallbackList final : public QWidget {
    Q_OBJECT

public:
    explicit FallbackList(QWidget *parent = nullptr);

    void setPresentation(const FallbackListPresentation &list);

signals:
    void moveRequested(int index, int offset);
    void removeRequested(int index);
    void addRequested(const QString &providerId);

protected:
    void changeEvent(QEvent *event) override;

private:
    void rebuild();
    void restoreFocus(const QString &name);
    void showStatuses();

    QFormLayout *m_form;
    FallbackListPresentation m_list;
};

// The Fallbacks subpages' list rows, speechFallbackList and
// refinementFallbackList, edited through withFallbackMoved() and its siblings.
SchemaCustomRowFactory fallbackListRows(const ProviderRegistry &providers, const LocalSetup &local);

} // namespace speecher
