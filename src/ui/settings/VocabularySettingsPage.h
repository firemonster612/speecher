#pragma once

#include "core/AppSettings.h"

#include <QFrame>

class QLabel;
class QPushButton;
class QTableWidget;

namespace speecher {

class VocabularySettingsPage : public QFrame {
    Q_OBJECT

public:
    explicit VocabularySettingsPage(QWidget *parent = nullptr);

    void load(const QList<VocabularyEntry> &entries);
    QList<VocabularyEntry> entries() const;
    bool hasChanges(const QList<VocabularyEntry> &entries) const;

signals:
    void changed();

private:
    void setRows(const QList<VocabularyEntry> &entries);
    void addEntry(const VocabularyEntry &entry = {});
    void importCsv();
    void updateLimit();

    QTableWidget *m_table;
    QLabel *m_limit;
    QPushButton *m_addButton;
    QPushButton *m_importButton;
    QPushButton *m_removeButton;
    bool m_updating = false;
};

} // namespace speecher
