#include "ui/settings/BindingsSettingsPage.h"

#include "core/BindingProcessor.h"

#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
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

class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget *parent = nullptr)
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
        painter.drawText(rect(), alignment(), fontMetrics().elidedText(text(), Qt::ElideRight, width()));
    }
};

static QString bindingPreview(const QString &replacement)
{
    QString preview = replacement;
    preview.replace(QLatin1Char('\n'), QStringLiteral(" / "));
    return preview.simplified();
}

BindingsSettingsPage::BindingsSettingsPage(QWidget *parent)
    : QFrame(parent)
    , m_bindings(new QListWidget(this))
{
    setObjectName(QStringLiteral("bindingSection"));
    m_bindings->setObjectName(QStringLiteral("bindingList"));
    m_bindings->setUniformItemSizes(false);
    m_bindings->setAlternatingRowColors(false);
    m_bindings->setSelectionMode(QAbstractItemView::SingleSelection);
    m_bindings->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_bindings->setMinimumHeight(180);

    auto *layout = new QVBoxLayout(this);
    auto *title = new QLabel(QStringLiteral("Replacements & snippets"), this);
    title->setObjectName(QStringLiteral("subsectionLabel"));
    title->setForegroundRole(QPalette::WindowText);
    title->setAttribute(Qt::WA_StyledBackground, false);
    auto *description = new QLabel(
        QStringLiteral("Replace a spoken phrase with exact text, including multi-line snippets. Matching ignores case and treats punctuation as spaces."),
        this);
    description->setObjectName(QStringLiteral("rowDescription"));
    description->setWordWrap(true);
    description->setAttribute(Qt::WA_StyledBackground, false);
    auto *addButton = new QPushButton(QStringLiteral("Add replacement"), this);
    auto *importButton = new QPushButton(QStringLiteral("Import snippets JSON"), this);
    addButton->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(importButton);
    buttons->addWidget(addButton);
    layout->addWidget(title);
    layout->addWidget(description);
    layout->addWidget(m_bindings);
    layout->addLayout(buttons);

    connect(addButton, &QPushButton::clicked, this, [this] {
        editBinding(-1);
    });
    connect(importButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("Import snippets"),
            QString(),
            QStringLiteral("JSON files (*.json);;All files (*)"));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this,
                                 QStringLiteral("Snippets not imported"),
                                 QStringLiteral("Could not read %1.").arg(path));
            return;
        }
        QString error;
        const QList<BindingRule> imported = BindingProcessor::parseJsonImport(file.readAll(), &error);
        if (!error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Snippets not imported"), error);
            return;
        }
        const BindingValidationResult merged = BindingProcessor::validateRules(m_bindingRules + imported);
        if (!merged.ok()) {
            QMessageBox::warning(this,
                                 QStringLiteral("Snippets not imported"),
                                 merged.messages().join(QStringLiteral("\n")));
            return;
        }
        m_bindingRules = merged.rules;
        refreshBindingList();
        emit changed();
    });
    connect(m_bindings, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item) {
            editBinding(item->data(Qt::UserRole).toInt());
        }
    });
}

void BindingsSettingsPage::load(const QList<BindingRule> &rules)
{
    m_bindingRules = rules;
    refreshBindingList();
}

bool BindingsSettingsPage::validate(QList<BindingRule> *validatedRules, bool showError)
{
    const BindingValidationResult validation = BindingProcessor::validateRules(m_bindingRules);
    if (!validation.ok()) {
        if (showError) {
            QMessageBox::warning(this,
                                 QStringLiteral("Replacements not saved"),
                                 validation.messages().join(QStringLiteral("\n")));
        }
        return false;
    }
    *validatedRules = validation.rules;
    return true;
}

bool BindingsSettingsPage::hasChanges(const QList<BindingRule> &rules) const
{
    return m_bindingRules != rules;
}

void BindingsSettingsPage::refreshBindingList()
{
    emit preserveScrollRequested(true);

    QSignalBlocker blocker(m_bindings);
    m_bindings->clear();

    for (int row = 0; row < m_bindingRules.size(); ++row) {
        const BindingRule rule = m_bindingRules.at(row);
        auto *item = new QListWidgetItem(m_bindings);
        item->setData(Qt::UserRole, row);
        item->setSizeHint(QSize(0, 56));

        auto *rowWidget = new QWidget(m_bindings);
        rowWidget->setObjectName(QStringLiteral("bindingRow"));
        auto *layout = new QHBoxLayout(rowWidget);
        layout->setContentsMargins(10, 6, 8, 6);
        layout->setSpacing(8);

        auto *phrase = new ElidedLabel(rowWidget);
        phrase->setText(rule.phrase);
        phrase->setToolTip(rule.phrase);
        phrase->setMinimumWidth(120);
        phrase->setForegroundRole(QPalette::WindowText);
        QFont phraseFont = phrase->font();
        phraseFont.setBold(true);
        phrase->setFont(phraseFont);

        auto *arrow = new QLabel(rowWidget);
        arrow->setAlignment(Qt::AlignCenter);
        arrow->setPixmap(style()->standardIcon(QStyle::SP_ArrowRight).pixmap(16, 16));
        arrow->setFixedWidth(18);

        auto *preview = new ElidedLabel(rowWidget);
        preview->setText(bindingPreview(rule.replacement));
        preview->setToolTip(rule.replacement);
        preview->setForegroundRole(QPalette::WindowText);

        auto *edit = new QPushButton(QStringLiteral("Edit"), rowWidget);
        edit->setIcon(QIcon::fromTheme(QStringLiteral("document-edit")));
        edit->setMinimumWidth(edit->fontMetrics().horizontalAdvance(edit->text()) + 32);
        edit->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

        auto *remove = new QPushButton(QStringLiteral("Remove"), rowWidget);
        remove->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
        remove->setMinimumWidth(remove->fontMetrics().horizontalAdvance(remove->text()) + 32);
        remove->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

        layout->addWidget(phrase, 1);
        layout->addWidget(arrow, 0);
        layout->addWidget(preview, 4);
        layout->addWidget(edit, 0);
        layout->addWidget(remove, 0);

        m_bindings->setItemWidget(item, rowWidget);

        connect(edit, &QPushButton::clicked, this, [this, row] {
            editBinding(row);
        });
        connect(remove, &QPushButton::clicked, this, [this, row] {
            if (row < 0 || row >= m_bindingRules.size()) {
                return;
            }
            m_bindingRules.removeAt(row);
            refreshBindingList();
            emit changed();
        });
    }

    emit preserveScrollRequested(false);
}

void BindingsSettingsPage::editBinding(int row)
{
    const bool editing = row >= 0 && row < m_bindingRules.size();

    QDialog dialog(this);
    dialog.setWindowTitle(editing ? QStringLiteral("Edit replacement") : QStringLiteral("Add replacement"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(8);

    auto *phraseLabel = new QLabel(QStringLiteral("Spoken phrase"), &dialog);
    auto *phrase = new QLineEdit(&dialog);
    phrase->setClearButtonEnabled(true);

    auto *replacementLabel = new QLabel(QStringLiteral("Exact replacement or snippet"), &dialog);
    auto *replacement = new QPlainTextEdit(&dialog);
    replacement->setMinimumHeight(240);
    replacement->setLineWrapMode(QPlainTextEdit::WidgetWidth);

    if (editing) {
        phrase->setText(m_bindingRules.at(row).phrase);
        replacement->setPlainText(m_bindingRules.at(row).replacement);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *saveButton = buttons->button(QDialogButtonBox::Ok);
    if (saveButton) {
        saveButton->setText(QStringLiteral("Save"));
        saveButton->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));
    }
    QPushButton *deleteButton = nullptr;
    if (editing) {
        deleteButton = buttons->addButton(QStringLiteral("Delete"), QDialogButtonBox::DestructiveRole);
        deleteButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
    }

    layout->addWidget(phraseLabel);
    layout->addWidget(phrase);
    layout->addSpacing(8);
    layout->addWidget(replacementLabel);
    layout->addWidget(replacement, 1);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (saveButton) {
        connect(saveButton, &QPushButton::clicked, &dialog, [this, row, editing, phrase, replacement, &dialog] {
            QList<BindingRule> candidate = m_bindingRules;
            const BindingRule updated{phrase->text().trimmed(), replacement->toPlainText()};
            if (editing) {
                candidate[row] = updated;
            } else {
                candidate.append(updated);
            }

            const BindingValidationResult validation = BindingProcessor::validateRules(candidate);
            if (!validation.ok()) {
                QMessageBox::warning(&dialog,
                                     QStringLiteral("Replacement not saved"),
                                     validation.messages().join(QStringLiteral("\n")));
                return;
            }

            m_bindingRules = validation.rules;
            refreshBindingList();
            emit changed();
            dialog.accept();
        });
    }
    if (deleteButton) {
        connect(deleteButton, &QPushButton::clicked, &dialog, [this, row, &dialog] {
            if (row >= 0 && row < m_bindingRules.size()) {
                m_bindingRules.removeAt(row);
                refreshBindingList();
                emit changed();
            }
            dialog.accept();
        });
    }

    dialog.resize(560, 430);
    phrase->setFocus(Qt::OtherFocusReason);
    dialog.exec();
}

} // namespace speecher
