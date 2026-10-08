#include "frontend/qt/PhoneTransferDialog.h"

#include "app/PhoneTransfer.h"
#include "app/PhoneTransferPresentation.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

namespace speecher {

namespace {

// The code's side in grid units: large enough to scan from across a desk.
constexpr int kCodeGridUnits = 14;

QString htmlList(const char *tag, const QStringList &items)
{
    QString html = QStringLiteral("<%1>").arg(QLatin1String(tag));
    for (const QString &item : items) {
        html += QStringLiteral("<li>%1</li>").arg(item.toHtmlEscaped());
    }
    return html + QStringLiteral("</%1>").arg(QLatin1String(tag));
}

QLabel *wrappedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    return label;
}

QGroupBox *listGroup(const QString &heading, const QStringList &items, QWidget *parent)
{
    auto *group = new QGroupBox(heading, parent);
    auto *layout = new QVBoxLayout(group);
    layout->addWidget(wrappedLabel(htmlList("ul", items), group));
    return group;
}

} // namespace

void openPhoneTransferDialog(const AppSettings &settings, QWidget *parent)
{
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName(QStringLiteral("phoneTransferDialog"));
    auto *transfer = new PhoneTransfer(settings, dialog);
    const PhoneTransferText text = phoneTransferText(settings, transfer->state());
    dialog->setWindowTitle(text.title);
    dialog->setMinimumWidth(settings::cardMaximumWidth());

    auto *code = new QLabel(dialog);
    code->setAlignment(Qt::AlignTop);
    if (const QString link = transfer->link(); !link.isEmpty()) {
        const qreal ratio = dialog->devicePixelRatioF();
        QImage image = qrCodeImage(link, qRound(kCodeGridUnits * settings::gridUnit() * ratio));
        image.setDevicePixelRatio(ratio);
        code->setPixmap(QPixmap::fromImage(image));
    } else {
        code->hide();
    }

    auto *details = new QVBoxLayout;
    details->setSpacing(settings::relatedSpacing());
    if (!text.steps.isEmpty()) {
        details->addWidget(wrappedLabel(htmlList("ol", text.steps), dialog));
    }
    details->addWidget(listGroup(text.includedHeading, text.included, dialog));
    if (!text.stays.isEmpty()) {
        details->addWidget(listGroup(text.staysHeading, text.stays, dialog));
    }
    QLabel *never = wrappedLabel(text.neverIncluded, dialog);
    never->setFont(settings::smallFont(never->font()));
    never->setForegroundRole(QPalette::PlaceholderText);
    details->addWidget(never);

    auto *top = new QHBoxLayout;
    top->setSpacing(settings::largeSpacing());
    top->addWidget(code, 0, Qt::AlignTop);
    top->addLayout(details, 1);

    QLabel *status = wrappedLabel(text.status, dialog);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    auto *layout = new QVBoxLayout(dialog);
    layout->addLayout(top);
    layout->addWidget(status);
    layout->addWidget(buttons);

    // Once sent, the code is spent: the style greys it out.
    QObject::connect(transfer, &PhoneTransfer::stateChanged, dialog, [=] {
        status->setText(phoneTransferText(settings, transfer->state()).status);
        code->setEnabled(transfer->state() == PhoneTransferState::Waiting);
    });
    dialog->open();
}

} // namespace speecher
