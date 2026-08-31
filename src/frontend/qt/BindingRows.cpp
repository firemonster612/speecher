#include "frontend/qt/BindingRows.h"

#include "frontend/qt/CollectionRow.h"

#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
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
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(importButton);
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

    return {
        control,
        [this] { return QVariant::fromValue(m_records); },
        [this](const QVariant &value) {
            m_records = value.value<QList<QVariantMap>>();
            refreshList();
        },
        true,
    };
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

        auto *remove = new QPushButton(QStringLiteral("Remove"), rowWidget);
        remove->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
        remove->setMinimumWidth(remove->fontMetrics().horizontalAdvance(remove->text()) + 32);
        remove->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

        layout->addWidget(spoken, 1);
        layout->addWidget(arrow, 0);
        layout->addWidget(preview, 4);
        layout->addWidget(edit, 0);
        layout->addWidget(remove, 0);

        m_list->setItemWidget(item, rowWidget);

        QObject::connect(edit, &QPushButton::clicked, rowWidget, [this, row] { editRecord(row); });
        QObject::connect(remove, &QPushButton::clicked, rowWidget, [this, row] {
            if (row < 0 || row >= m_records.size()) {
                return;
            }
            m_records.removeAt(row);
            refreshList();
            m_notifyChanged();
        });
    }

    emit preserveScrollRequested(false);
}

void BindingRows::editRecord(int row)
{
    const bool editing = row >= 0 && row < m_records.size();

    QDialog dialog(m_list);
    dialog.setWindowTitle(editing ? QStringLiteral("Edit replacement")
                                  : QStringLiteral("Add replacement"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(8);

    auto *phraseLabel = new QLabel(m_collection.columns.at(0).title, &dialog);
    auto *phraseEdit = new QLineEdit(&dialog);
    phraseEdit->setClearButtonEnabled(true);

    auto *replacementLabel = new QLabel(m_collection.columns.at(1).title, &dialog);
    auto *replacementEdit = new QPlainTextEdit(&dialog);
    replacementEdit->setMinimumHeight(240);
    replacementEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);

    if (editing) {
        phraseEdit->setText(phrase(m_records.at(row)));
        replacementEdit->setPlainText(replacement(m_records.at(row)));
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *saveButton = buttons->button(QDialogButtonBox::Ok);
    saveButton->setText(QStringLiteral("Save"));
    saveButton->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));
    QPushButton *deleteButton = nullptr;
    if (editing) {
        deleteButton = buttons->addButton(QStringLiteral("Delete"), QDialogButtonBox::DestructiveRole);
        deleteButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
    }

    layout->addWidget(phraseLabel);
    layout->addWidget(phraseEdit);
    layout->addSpacing(8);
    layout->addWidget(replacementLabel);
    layout->addWidget(replacementEdit, 1);
    layout->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(saveButton, &QPushButton::clicked, &dialog, [&] {
        const QVariantMap updated{{m_collection.columns.at(0).id, phraseEdit->text().trimmed()},
                                  {m_collection.columns.at(1).id, replacementEdit->toPlainText()}};
        QList<QVariantMap> candidate = m_records;
        if (editing) {
            candidate[row] = updated;
        } else {
            candidate.append(updated);
        }
        const QStringList problems = m_collection.validate(candidate);
        if (!problems.isEmpty()) {
            QMessageBox::warning(&dialog,
                                 QStringLiteral("Replacement not saved"),
                                 problems.join(QLatin1Char('\n')));
            return;
        }
        m_records = candidate;
        refreshList();
        m_notifyChanged();
        dialog.accept();
    });
    if (deleteButton) {
        QObject::connect(deleteButton, &QPushButton::clicked, &dialog, [&] {
            m_records.removeAt(row);
            refreshList();
            m_notifyChanged();
            dialog.accept();
        });
    }

    dialog.resize(560, 430);
    phraseEdit->setFocus(Qt::OtherFocusReason);
    dialog.exec();
}

} // namespace speecher
