#pragma once

#include "core/AppSettings.h"

#include <QFrame>

class QListWidget;

namespace speecher {

class BindingsSettingsPage : public QFrame {
    Q_OBJECT

public:
    explicit BindingsSettingsPage(QWidget *parent = nullptr);

    void load(const QList<BindingRule> &rules);
    bool validate(QList<BindingRule> *validatedRules);
    bool hasChanges(const QList<BindingRule> &rules) const;

signals:
    void changed();
    void preserveScrollRequested(bool rebuilding);

private:
    void refreshBindingList();
    void editBinding(int row);

    QListWidget *m_bindings;
    QList<BindingRule> m_bindingRules;
};

} // namespace speecher
