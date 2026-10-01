#include "frontend/qt/BindingRows.h"

#include "frontend/qt/CollectionRow.h"

#include <algorithm>

#include <QAbstractItemView>
#include "ui/settings/SettingsPageSupport.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>

namespace speecher {

namespace {

class ElidedLabel final : public QLabel {
public:
    explicit ElidedLabel(QWidget *parent)
        : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setFont(font());
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(rect(),
                         alignment(),
                         fontMetrics().elidedText(text(), Qt::ElideRight, width()));
    }
};

QString listPreview(const QString &replacement)
{
    QString preview = replacement;
    preview.replace(QLatin1Char('\n'), QStringLiteral(" / "));
    return preview.simplified();
}

} // namespace

QString BindingRows::phrase(const QVariantMap &record) const
{
    return record.value(m_collection.columns.at(0).id).toString();
}

QString BindingRows::replacement(const QVariantMap &record) const
{
    return record.value(m_collection.columns.at(1).id).toString();
}

SchemaCustomRowFactory BindingRows::factory()
{
    return [this](const SettingsRow &descriptor,
                  QWidget *parent,
                  std::function<void()> notifyChanged) {
        if (descriptor.id == QStringLiteral("bindingRules")) {
            return makeReplacementRow(descriptor, parent, std::move(notifyChanged));
        }
        return SchemaCustomRow{};
    };
}

SchemaCustomRow BindingRows::makeReplacementRow(const SettingsRow &descriptor,
                                                QWidget *parent,
                                                std::function<void()> notifyChanged)
{
    m_collection = descriptor.collection;
    m_notifyChanged = std::move(notifyChanged);

    auto *control = new QWidget(parent);
    m_list = new QListWidget(control);
    m_list->setObjectName(QStringLiteral("bindingList"));
    m_list->setUniformItemSizes(false);
    m_list->setAlternatingRowColors(false);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setMinimumHeight(m_collection.minimumHeight);

    auto *layout = new QVBoxLayout(control);
    layout->setContentsMargins(settings::rowPadding());
    auto *title = new QLabel(descriptor.label, control);
    title->setObjectName(QStringLiteral("subsectionLabel"));
    title->setForegroundRole(QPalette::WindowText);
    title->setAttribute(Qt::WA_StyledBackground, false);
    auto *description = new QLabel(descriptor.help, control);
    description->setObjectName(QStringLiteral("rowDescription"));
    description->setWordWrap(true);
    description->setAttribute(Qt::WA_StyledBackground, false);
    auto *addButton = new QPushButton(m_collection.addLabel, control);
    addButton->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    auto *importButton = new QPushButton(m_collection.supportsImport.actionLabel, control);
    // collectionRow() gives every deletable collection its Undo delete.
    const auto undoAction = std::find_if(m_collection.actions.cbegin(), m_collection.actions.cend(),
                                         [](const RowOption &action) {
                                             return action.id == QStringLiteral("undoDelete");
                                         });
    Q_ASSERT(undoAction != m_collection.actions.cend());
    m_undoDelete = new QPushButton(undoAction->label, control);
    m_undoDelete->setObjectName(QStringLiteral("undoDeleteBindingRules"));
    m_undoDelete->setEnabled(false);
    m_empty = new QLabel(m_collection.emptyTitle + QLatin1Char('\n') + m_collection.emptyHelp,
                         m_list->viewport());
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->setForegroundRole(QPalette::PlaceholderText);
    (new QVBoxLayout(m_list->viewport()))->addWidget(m_empty);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(importButton);
    buttons->addStretch();
    buttons->addWidget(m_undoDelete);
    buttons->addWidget(addButton);
    layout->addWidget(title);
    layout->addWidget(description);
    layout->addWidget(m_list);
    layout->addLayout(buttons);

    QObject::connect(addButton, &QPushButton::clicked, control, [this] { editRecord(-1); });
    QObject::connect(importButton, &QPushButton::clicked, control, [this, control] {
        const std::optional<QList<QVariantMap>> merged =
            importedRecords(control, m_collection, m_records);
        if (!merged) {
            return;
        }
        m_records = *merged;
        refreshList();
        m_notifyChanged();
    });
    QObject::connect(m_list, &QListWidget::itemDoubleClicked, control, [this](QListWidgetItem *item) {
        editRecord(item->data(Qt::UserRole).toInt());
    });
    QObject::connect(m_undoDelete, &QPushButton::clicked, control, [this] {
        if (m_deleted.isEmpty()) {
            return;
        }
        const auto [index, record] = m_deleted.takeLast();
        m_records.insert(std::min(index, m_records.size()), record);
        refreshList();
        m_notifyChanged();
    });

    return {
        control,
        [this] { return QVariant::fromValue(m_records); },
        [this](const QVariant &value) {
            const QList<QVariantMap> records = value.value<QList<QVariantMap>>();
            // A reload with other records committed whatever Delete took.
            if (records != m_records) {
                m_deleted.clear();
            }
            m_records = records;
            refreshList();
        },
        true,
        nullptr,
        [this](const AppSettings &settings) { m_settings = settings; },
    };
}

void BindingRows::deleteRecord(int row)
{
    if (row < 0 || row >= m_records.size()) {
        return;
    }
    m_deleted.append({row, m_records.takeAt(row)});
    refreshList();
    m_notifyChanged();
}

void BindingRows::refreshList()
{
    emit preserveScrollRequested(true);

    const QSignalBlocker blocker(m_list);
    m_list->clear();
    for (int row = 0; row < m_records.size(); ++row) {
        const QVariantMap record = m_records.at(row);
        auto *item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, row);
        item->setSizeHint(QSize(0, 56));

        auto *rowWidget = new QWidget(m_list);
        rowWidget->setObjectName(QStringLiteral("bindingRow"));
        auto *layout = new QHBoxLayout(rowWidget);
        layout->setContentsMargins(10, 6, 8, 6);
        layout->setSpacing(8);

        auto *spoken = new ElidedLabel(rowWidget);
        spoken->setText(phrase(record));
        spoken->setToolTip(phrase(record));
        spoken->setMinimumWidth(120);
        spoken->setForegroundRole(QPalette::WindowText);
        QFont phraseFont = spoken->font();
        phraseFont.setBold(true);
        spoken->setFont(phraseFont);

        auto *arrow = new QLabel(rowWidget);
        arrow->setAlignment(Qt::AlignCenter);
        arrow->setPixmap(rowWidget->style()->standardIcon(QStyle::SP_ArrowRight).pixmap(16, 16));
        arrow->setFixedWidth(18);

        auto *preview = new ElidedLabel(rowWidget);
        preview->setText(listPreview(replacement(record)));
        preview->setToolTip(replacement(record));
        preview->setForegroundRole(QPalette::WindowText);

        auto *edit = new QPushButton(QStringLiteral("Edit"), rowWidget);
        edit->setIcon(QIcon::fromTheme(QStringLiteral("document-edit")));
        edit->setMinimumWidth(edit->fontMetrics().horizontalAdvance(edit->text()) + 32);
        edit->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        // Every row has the same two buttons, so their names say which phrase.
        edit->setAccessibleName(QStringLiteral("%1 %2").arg(edit->text(), phrase(record)));

        auto *remove = new QPushButton(m_collection.deleteLabel, rowWidget);
        remove->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
        remove->setMinimumWidth(remove->fontMetrics().horizontalAdvance(remove->text()) + 32);
        remove->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        remove->setAccessibleName(QStringLiteral("%1 %2").arg(remove->text(), phrase(record)));

        layout->addWidget(spoken, 1);
        layout->addWidget(arrow, 0);
        layout->addWidget(preview, 4);
        layout->addWidget(edit, 0);
        layout->addWidget(remove, 0);

        m_list->setItemWidget(item, rowWidget);

        QObject::connect(edit, &QPushButton::clicked, rowWidget, [this, row] { editRecord(row); });
        QObject::connect(remove, &QPushButton::clicked, rowWidget, [this, row] { deleteRecord(row); });
    }
    m_empty->setVisible(m_records.isEmpty());
    m_undoDelete->setEnabled(!m_deleted.isEmpty());

    emit preserveScrollRequested(false);
}

void BindingRows::editRecord(int row)
{
    openRecordDialog(m_list, m_collection, m_settings, row, [this] { return m_records; },
                     [this](const QList<QVariantMap> &records) {
                         m_records = records;
                         refreshList();
                         m_notifyChanged();
                     });
}

} // namespace speecher
