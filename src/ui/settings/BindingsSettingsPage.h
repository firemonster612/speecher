#pragma once

#include "core/AppSettings.h"

#include <QFrame>

class QListWidget;
class QScrollArea;

namespace speecher {

class BindingsSettingsPage : public QFrame {
    Q_OBJECT

public:
    explicit BindingsSettingsPage(QScrollArea *scrollArea, QWidget *parent = nullptr);

    void load(const QList<BindingRule> &rules);
    bool validate(QList<BindingRule> *validatedRules);
    bool hasChanges(const QList<BindingRule> &rules) const;

signals:
    void changed();

private:
    void refreshBindingList();
    void editBinding(int row);

    QScrollArea *m_scrollArea;
    QListWidget *m_bindings;
    QList<BindingRule> m_bindingRules;
};

} // namespace speecher
