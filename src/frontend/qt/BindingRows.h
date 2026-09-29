#pragma once

#include "frontend/qt/SchemaSettingsPage.h"

#include <QObject>
#include <QVariantMap>

class QLabel;
class QListWidget;
class QPushButton;

namespace speecher {

// The replacements collection, rendered as a list with an edit dialog rather
// than as a table: a replacement is a multi-line snippet, which no table cell
// can show or edit. It reads and writes the same descriptor the generic editor
// would have.
class BindingRows : public QObject {
    Q_OBJECT

public:
    SchemaCustomRowFactory factory();

signals:
    // Rebuilding the list resets the scroll of whatever holds it, so the
    // container is told to remember its position across the rebuild.
    void preserveScrollRequested(bool rebuilding);

private:
    SchemaCustomRow makeReplacementRow(const SettingsRow &descriptor,
                                       QWidget *parent,
                                       std::function<void()> notifyChanged);
    void refreshList();
    void editRecord(int row);
    void deleteRecord(int row);
    QString phrase(const QVariantMap &record) const;
    QString replacement(const QVariantMap &record) const;

    CollectionDescriptor m_collection;
    QListWidget *m_list = nullptr;
    QLabel *m_empty = nullptr;
    QPushButton *m_undoDelete = nullptr;
    QList<QVariantMap> m_records;
    // What Delete took and where it stood, newest last, so Undo delete can
    // put it back in its place.
    QList<QPair<qsizetype, QVariantMap>> m_deleted;
    std::function<void()> m_notifyChanged;
};

} // namespace speecher
