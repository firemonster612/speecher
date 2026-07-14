#include "ui/SettingsDialog.h"

#include "app/ApplicationController.h"
#include "core/BindingProcessor.h"
#include "core/OutputMethod.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "core/VocabularyLimit.h"
#include "output/YdotoolDelivery.h"
#include "output/YdotoolSetup.h"
#include "platform/PlatformIntegration.h"
#include "providers/OpenAiAuthProvider.h"
#include "providers/ProviderRegistry.h"
#include "ui/Theme.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QtMath>

namespace speecher {

static const char *settingsStyle =
    "QFrame#settingsCard,QFrame#vocabSection,QFrame#bindingSection{background:palette(base);border:1px solid palette(mid);border-radius:12px;}"
    "QFrame#settingsRow{background:transparent;border:0;}"
    "QWidget#rowText{background:transparent;border:0;}"
    "QTableWidget#vocabInput,QListWidget#bindingList{background:palette(base);color:palette(text);border:1px solid palette(mid);border-radius:6px;gridline-color:palette(mid);}"
    "QTableWidget#vocabInput:focus,QListWidget#bindingList:focus{border-color:palette(highlight);}"
    "QFrame#settingsCard QLabel,QFrame#vocabSection QLabel,QFrame#bindingSection QLabel,QLabel#noteText,QLabel#sectionLabel{background:transparent;border:0;}"
    "QFrame#settingsSeparator{background:palette(mid);border:0;margin:0;}"
    "QWidget#bindingRow{background:transparent;border:0;}"
    "QLabel#rowTitle{font-weight:600;}"
    "QLabel#rowDescription,QLabel#statusText,QLabel#noteText{font-weight:400;}"
    "QLabel#sectionLabel{font-weight:600;}";

static QTableWidgetItem *makeVocabularyItem(const QString &text = QString());

class VocabularyTable : public QTableWidget {
public:
    explicit VocabularyTable(QWidget *parent = nullptr)
        : QTableWidget(parent)
    {
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            const int nextRow = qMax(currentRow() + 1, 0);
            if (nextRow >= rowCount()) {
                insertRow(rowCount());
                setItem(rowCount() - 1, 0, makeVocabularyItem());
            }
            setCurrentCell(nextRow, 0);
            editItem(item(nextRow, 0));
            event->accept();
            return;
        }
        QTableWidget::keyPressEvent(event);
    }
};

static QTableWidgetItem *makeVocabularyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled);
    return item;
}

static QIcon informationIcon(QWidget *widget)
{
    QIcon icon = QIcon::fromTheme(QStringLiteral("dialog-information-symbolic"));
    if (icon.isNull()) {
        icon = QIcon::fromTheme(QStringLiteral("dialog-information"));
    }
    if (icon.isNull() && widget) {
        icon = widget->style()->standardIcon(QStyle::SP_MessageBoxInformation, nullptr, widget);
    }
    return icon;
}

static QPixmap tintedInformationPixmap(QWidget *widget, const QSize &size, const QColor &color)
{
    const QIcon icon = informationIcon(widget);
    QPixmap source = icon.pixmap(size - QSize(2, 2));
    if (source.isNull()) {
        return source;
    }

    QPixmap tinted(size);
    tinted.fill(Qt::transparent);
    QPainter painter(&tinted);
    painter.drawPixmap((size.width() - source.width()) / 2, (size.height() - source.height()) / 2, source);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color);
    return tinted;
}

static QPixmap warningInformationPixmap(QWidget *widget, int logicalSize, const QColor &color)
{
    const qreal dpr = widget ? widget->devicePixelRatioF() : qApp->devicePixelRatio();
    QPixmap pixmap(qCeil(logicalSize * dpr), qCeil(logicalSize * dpr));
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(dpr, dpr);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QRectF(1.5, 1.5, logicalSize - 3.0, logicalSize - 3.0));

    const QColor cutout = widget ? widget->palette().color(QPalette::Base) : QColor(Qt::black);
    painter.setBrush(cutout);
    painter.drawEllipse(QRectF(logicalSize / 2.0 - 1.1, 4.0, 2.2, 2.2));
    painter.drawRoundedRect(QRectF(logicalSize / 2.0 - 1.1, 7.4, 2.2, 5.3), 1.0, 1.0);
    painter.end();

    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

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

static QFrame *makeSeparator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("settingsSeparator"));
    line->setFixedHeight(1);
    line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return line;
}

static QFrame *makeRow(const QString &label,
                       const QString &description,
                       QWidget *control,
                       QWidget *parent,
                       QWidget *titleAccessory = nullptr)
{
    auto *row = new QFrame(parent);
    row->setObjectName(QStringLiteral("settingsRow"));
    row->setMinimumHeight(92);
    row->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(24);

    auto *text = new QWidget(row);
    text->setObjectName(QStringLiteral("rowText"));
    text->setAttribute(Qt::WA_StyledBackground, false);
    text->setAutoFillBackground(false);
    text->setMinimumWidth(280);
    auto *textLayout = new QVBoxLayout(text);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(4);

    auto *title = new QLabel(label, text);
    title->setObjectName(QStringLiteral("rowTitle"));
    auto *subtitle = new QLabel(description, text);
    subtitle->setObjectName(QStringLiteral("rowDescription"));
    subtitle->setWordWrap(true);
    title->setAttribute(Qt::WA_StyledBackground, false);
    subtitle->setAttribute(Qt::WA_StyledBackground, false);
    title->setAutoFillBackground(false);
    subtitle->setAutoFillBackground(false);
    title->setForegroundRole(QPalette::WindowText);
    subtitle->setForegroundRole(QPalette::WindowText);
    if (titleAccessory) {
        auto *titleRow = new QWidget(text);
        titleRow->setObjectName(QStringLiteral("rowText"));
        titleRow->setAttribute(Qt::WA_StyledBackground, false);
        titleRow->setAutoFillBackground(false);
        auto *titleLayout = new QHBoxLayout(titleRow);
        titleLayout->setContentsMargins(0, 0, 0, 0);
        titleLayout->setSpacing(6);
        titleLayout->addWidget(title, 0, Qt::AlignVCenter);
        titleLayout->addWidget(titleAccessory, 0, Qt::AlignVCenter);
        titleLayout->addStretch();
        textLayout->addWidget(titleRow);
    } else {
        textLayout->addWidget(title);
    }
    if (!description.isEmpty()) {
        textLayout->addWidget(subtitle);
    }

    if (auto *labelControl = qobject_cast<QLabel *>(control)) {
        labelControl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        labelControl->setWordWrap(false);
        labelControl->setAttribute(Qt::WA_StyledBackground, false);
        labelControl->setAutoFillBackground(false);
        labelControl->setMinimumWidth(170);
    } else {
        control->setMinimumWidth(180);
        control->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    layout->addWidget(text, 1, Qt::AlignVCenter);
    layout->addStretch();
    layout->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);
    return row;
}

static void addRow(QVBoxLayout *layout, QFrame *row, QWidget *parent, bool addSeparator = true)
{
    layout->addWidget(row);
    if (addSeparator) {
        layout->addWidget(makeSeparator(parent));
    }
}

static void selectData(QComboBox *combo, const QString &data)
{
    const int index = combo->findData(data);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

static void selectEditableText(QComboBox *combo, const QString &text)
{
    const QString trimmed = text.trimmed();
    const int dataIndex = combo->findData(trimmed);
    if (dataIndex >= 0) {
        combo->setCurrentIndex(dataIndex);
        return;
    }
    const int index = combo->findText(trimmed);
    if (index >= 0) {
        combo->setCurrentIndex(index);
        return;
    }
    combo->addItem(trimmed, trimmed);
    combo->setCurrentIndex(combo->count() - 1);
}

static QString editableComboValue(const QComboBox *combo)
{
    const int index = combo->currentIndex();
    const QString text = combo->currentText().trimmed();
    if (index >= 0 && text == combo->itemText(index)) {
        const QString data = combo->itemData(index).toString().trimmed();
        if (!data.isEmpty()) {
            return data;
        }
    }
    return text;
}

static void setComboItemEnabled(QComboBox *combo, int index, bool enabled, const QString &toolTip = QString())
{
    auto *model = qobject_cast<QStandardItemModel *>(combo->model());
    if (!model || index < 0) {
        return;
    }
    QStandardItem *item = model->item(index);
    if (!item) {
        return;
    }
    item->setEnabled(enabled);
    item->setToolTip(toolTip);
}

static QWidget *makeYdotoolControl(QLabel *status,
                                   QPushButton *setup,
                                   QPushButton *start,
                                   QPushButton *disable,
                                   QPushButton *remove,
                                   QWidget *parent)
{
    auto *control = new QWidget(parent);
    auto *layout = new QVBoxLayout(control);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    status->setObjectName(QStringLiteral("statusText"));
    status->setWordWrap(true);
    status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    status->setForegroundRole(QPalette::WindowText);
    status->setAttribute(Qt::WA_StyledBackground, false);

    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    row->addWidget(setup);
    row->addWidget(start);
    row->addWidget(disable);
    row->addWidget(remove);

    layout->addWidget(status);
    layout->addLayout(row);
    return control;
}

static QWidget *makeAnthropicModelControl(QComboBox *model, QLabel *warning, QWidget **warningRowOut, QWidget *parent)
{
    auto *control = new QWidget(parent);
    auto *layout = new QVBoxLayout(control);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *warningRow = new QWidget(control);
    auto *warningLayout = new QHBoxLayout(warningRow);
    warningLayout->setContentsMargins(0, 0, 0, 0);
    warningLayout->setSpacing(5);
    warningRow->setFixedHeight(18);

    auto *icon = new QLabel(warningRow);
    icon->setAlignment(Qt::AlignCenter);
    icon->setFixedSize(18, 18);
    icon->setPixmap(warningInformationPixmap(parent, 14, QColor(QStringLiteral("#f59e0b"))));

    QFont warningFont = warning->font();
    if (warningFont.pointSize() > 0) {
        warningFont.setPointSize(qMax(warningFont.pointSize() - 2, 8));
    } else {
        warningFont.setPixelSize(qMax(warningFont.pixelSize() - 2, 11));
    }
    warning->setFont(warningFont);
    warning->setWordWrap(false);
    warning->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    warning->setMinimumWidth(0);
    warning->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    warning->setFixedHeight(18);
    warning->setAttribute(Qt::WA_StyledBackground, false);
    warning->setAutoFillBackground(false);

    warningLayout->addWidget(icon, 0, Qt::AlignVCenter);
    warningLayout->addWidget(warning, 1, Qt::AlignVCenter);
    layout->addWidget(model);
    layout->addWidget(warningRow);

    if (warningRowOut) {
        *warningRowOut = warningRow;
    }
    warningRow->setVisible(false);
    return control;
}

static QLabel *makeSectionLabel(const QString &text, QWidget *parent)
{
    auto *section = new QLabel(text, parent);
    section->setObjectName(QStringLiteral("sectionLabel"));
    section->setAttribute(Qt::WA_StyledBackground, false);
    section->setAlignment(Qt::AlignCenter);
    return section;
}

static QFrame *makeSettingsCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("settingsCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    return card;
}

SettingsDialog::SettingsDialog(ApplicationController *controller, QWidget *parent)
    : QDialog(parent)
    , m_controller(controller)
    , m_theme(new QComboBox(this))
    , m_audioDevice(new QComboBox(this))
    , m_captureMode(new QComboBox(this))
    , m_pauseMedia(new QCheckBox(this))
    , m_vadEnabled(new QCheckBox(this))
    , m_provider(new QComboBox(this))
    , m_refinementStyle(new QComboBox(this))
    , m_openAiModel(new QComboBox(this))
    , m_openAiEffort(new QComboBox(this))
    , m_anthropicModel(new QComboBox(this))
    , m_anthropicEffort(new QComboBox(this))
    , m_outputMethod(new QComboBox(this))
    , m_restoreClipboardAfterTyping(new QCheckBox(this))
    , m_authMode(new QComboBox(this))
    , m_anthropicAuthMode(new QComboBox(this))
    , m_authControl(new QStackedWidget(this))
    , m_authStatus(new QLabel(this))
    , m_anthropicWarning(new QLabel(this))
    , m_ydotoolStatus(new QLabel(this))
    , m_vocabLimit(new QLabel(this))
    , m_apiKey(new QLineEdit(this))
    , m_scroll(new QScrollArea(this))
    , m_previewWords(new QSpinBox(this))
    , m_preRollMs(new QSpinBox(this))
    , m_postRollMs(new QSpinBox(this))
    , m_readinessTimeoutMs(new QSpinBox(this))
    , m_vadThreshold(new QSpinBox(this))
    , m_vocab(new VocabularyTable(this))
    , m_bindings(new QListWidget(this))
{
    setWindowTitle(QStringLiteral("Speecher Settings"));
    resize(720, 780);
    setMinimumWidth(640);
    m_theme->addItem(QStringLiteral("System"), QStringLiteral("system"));
    m_theme->addItem(QStringLiteral("Light"), QStringLiteral("light"));
    m_theme->addItem(QStringLiteral("Dark"), QStringLiteral("dark"));
    m_pauseMedia->setText(QStringLiteral("Pause"));
    m_audioDevice->setMinimumContentsLength(28);
    m_audioDevice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_audioDevice->setToolTip(QStringLiteral("Microphone Speecher records from."));
    m_captureMode->addItem(QStringLiteral("On demand"), QStringLiteral("on_demand"));
    m_captureMode->addItem(QStringLiteral("Warm"), QStringLiteral("warm"));
    m_captureMode->setToolTip(QStringLiteral("Warm keeps the microphone stream open between captures for lower startup latency."));
    m_vadEnabled->setText(QStringLiteral("Trim silence"));
    m_vadEnabled->setToolTip(QStringLiteral("Suppress leading, trailing, and long in-between silence before audio is sent."));
    for (const ProviderDescriptor &provider : m_controller->providerRegistry()->refinementProviders()) {
        m_provider->addItem(provider.label, provider.id);
    }
    m_provider->addItem(QStringLiteral("None"), QStringLiteral("none"));
    m_refinementStyle->addItem(QStringLiteral("Strong polish"), QStringLiteral("strong_polish"));
    m_refinementStyle->addItem(QStringLiteral("Balanced"), QStringLiteral("balanced"));
    m_refinementStyle->addItem(QStringLiteral("Light cleanup"), QStringLiteral("light_cleanup"));
    for (const QString &model : {
             QStringLiteral("gpt-5.6-luna"),
             QStringLiteral("gpt-5.6-terra"),
             QStringLiteral("gpt-5.6-sol"),
             QStringLiteral("gpt-5.5"),
             QStringLiteral("gpt-5.4-nano"),
             QStringLiteral("gpt-5.4-mini"),
             QStringLiteral("gpt-5.4"),
         }) {
        m_openAiModel->addItem(model, model);
    }
    m_openAiModel->setEditable(true);
    m_openAiModel->setInsertPolicy(QComboBox::NoInsert);
    m_openAiModel->setMaxVisibleItems(6);
    m_openAiModel->setMinimumContentsLength(16);
    m_openAiModel->setToolTip(QStringLiteral("Defaults to gpt-5.6-luna with no reasoning effort. Select another model or type another model ID."));
    m_openAiModel->view()->setMouseTracking(true);
    if (m_openAiModel->lineEdit()) {
        m_openAiModel->lineEdit()->setClearButtonEnabled(true);
    }
    m_openAiEffort->addItem(QStringLiteral("None"), QStringLiteral("none"));
    m_openAiEffort->addItem(QStringLiteral("Low"), QStringLiteral("low"));
    m_openAiEffort->addItem(QStringLiteral("Medium"), QStringLiteral("medium"));
    m_openAiEffort->addItem(QStringLiteral("High"), QStringLiteral("high"));
    m_openAiEffort->addItem(QStringLiteral("Extra high"), QStringLiteral("xhigh"));
    m_openAiEffort->setToolTip(QStringLiteral("OpenAI Responses reasoning.effort. Supported values vary by model."));
    const QList<QPair<QString, QString>> anthropicModels{
        {QStringLiteral("Claude Opus 4.8"), QStringLiteral("claude-opus-4-8")},
        {QStringLiteral("Claude Sonnet 4.6"), QStringLiteral("claude-sonnet-4-6")},
        {QStringLiteral("Claude Haiku 4.5"), QStringLiteral("claude-haiku-4-5-20251001")},
    };
    for (const auto &model : anthropicModels) {
        m_anthropicModel->addItem(model.first, model.second);
    }
    m_anthropicModel->setEditable(true);
    m_anthropicModel->setInsertPolicy(QComboBox::NoInsert);
    m_anthropicModel->setMaxVisibleItems(8);
    m_anthropicModel->setMinimumContentsLength(24);
    m_anthropicModel->setToolTip(QStringLiteral("Defaults to Claude Sonnet 4.6. Select a model or type another model ID."));
    m_anthropicModel->view()->setMouseTracking(true);
    if (m_anthropicModel->lineEdit()) {
        m_anthropicModel->lineEdit()->setClearButtonEnabled(true);
    }
    m_anthropicEffort->addItem(QStringLiteral("Low"), QStringLiteral("low"));
    m_anthropicEffort->addItem(QStringLiteral("Medium"), QStringLiteral("medium"));
    m_anthropicEffort->addItem(QStringLiteral("High"), QStringLiteral("high"));
    m_anthropicEffort->addItem(QStringLiteral("Extra high"), QStringLiteral("xhigh"));
    m_anthropicEffort->addItem(QStringLiteral("Max"), QStringLiteral("max"));
    m_anthropicEffort->setToolTip(QStringLiteral("Claude effort. Claude Code supports all listed levels; Anthropic API support depends on the selected model."));
    m_outputMethod->addItem(OutputMethod::label(QString::fromLatin1(OutputMethod::Automatic)), QString::fromLatin1(OutputMethod::Automatic));
    m_outputMethod->addItem(OutputMethod::label(QString::fromLatin1(OutputMethod::Ydotool)), QString::fromLatin1(OutputMethod::Ydotool));
    m_outputMethod->addItem(OutputMethod::label(QString::fromLatin1(OutputMethod::WlCopy)), QString::fromLatin1(OutputMethod::WlCopy));
    m_outputMethod->addItem(OutputMethod::label(QString::fromLatin1(OutputMethod::QtClipboard)), QString::fromLatin1(OutputMethod::QtClipboard));
    m_outputMethod->setToolTip(QStringLiteral("How Speecher delivers final text."));
    m_outputMethod->view()->setMouseTracking(true);
    m_restoreClipboardAfterTyping->setText(QStringLiteral("Restore"));
    m_restoreClipboardAfterTyping->setToolTip(QStringLiteral("Restore the previous clipboard after virtual-keyboard paste."));
    m_authMode->addItem(QStringLiteral("Automatic"), QStringLiteral("auto"));
    m_authMode->addItem(QStringLiteral("Codex API key"), QStringLiteral("codex_api_key"));
    m_authMode->addItem(QStringLiteral("Codex OAuth"), QStringLiteral("codex_oauth"));
    m_authMode->addItem(QStringLiteral("OPENAI_API_KEY"), QStringLiteral("env"));
    m_authMode->addItem(QStringLiteral("App settings key"), QStringLiteral("settings"));
    m_anthropicAuthMode->addItem(QStringLiteral("Claude Code session"), QStringLiteral("claude_code"));
    m_anthropicAuthMode->addItem(QStringLiteral("OAuth extra usage"), QStringLiteral("oauth"));
    m_anthropicAuthMode->setToolTip(QStringLiteral("Choose interactive Claude Code subscription usage or direct OAuth API routing."));
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(QStringLiteral("Enter OpenAI API key"));
    m_authControl->addWidget(m_authStatus);
    m_authControl->addWidget(m_apiKey);
    m_previewWords->setRange(1, 40);
    for (QSpinBox *spinBox : {m_preRollMs, m_postRollMs}) {
        spinBox->setRange(0, 1500);
        spinBox->setSingleStep(50);
        spinBox->setSuffix(QStringLiteral(" ms"));
    }
    m_readinessTimeoutMs->setRange(150, 3000);
    m_readinessTimeoutMs->setSingleStep(50);
    m_readinessTimeoutMs->setSuffix(QStringLiteral(" ms"));
    m_vadThreshold->setRange(1, 20);
    m_vadThreshold->setSuffix(QStringLiteral("%"));
    m_vocab->setObjectName(QStringLiteral("vocabInput"));
    m_vocab->setColumnCount(1);
    m_vocab->setHorizontalHeaderLabels({QStringLiteral("Term")});
    m_vocab->horizontalHeader()->hide();
    m_vocab->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_vocab->verticalHeader()->hide();
    m_vocab->setShowGrid(true);
    m_vocab->setAlternatingRowColors(false);
    m_vocab->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_vocab->setSelectionMode(QAbstractItemView::SingleSelection);
    m_vocab->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_vocab->setTabKeyNavigation(false);
    m_vocab->setMinimumHeight(120);
    m_vocabLimit->setObjectName(QStringLiteral("statusText"));
    m_vocabLimit->setForegroundRole(QPalette::WindowText);
    m_vocabLimit->setAttribute(Qt::WA_StyledBackground, false);
    m_bindings->setObjectName(QStringLiteral("bindingList"));
    m_bindings->setUniformItemSizes(false);
    m_bindings->setAlternatingRowColors(false);
    m_bindings->setSelectionMode(QAbstractItemView::SingleSelection);
    m_bindings->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_bindings->setMinimumHeight(180);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(12);

    auto *scroll = m_scroll;
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);

    auto *scrollContent = new QWidget(scroll);
    auto *content = new QVBoxLayout(scrollContent);
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(12);

    auto *generalSection = makeSectionLabel(QStringLiteral("General"), this);
    auto *audioSection = makeSectionLabel(QStringLiteral("Audio"), this);
    auto *refinementSection = makeSectionLabel(QStringLiteral("Refinement"), this);
    auto *outputSection = makeSectionLabel(QStringLiteral("Output"), this);
    auto *openAiSection = makeSectionLabel(QStringLiteral("OpenAI"), this);
    auto *anthropicSection = makeSectionLabel(QStringLiteral("Anthropic"), this);
    auto *vocabularySection = makeSectionLabel(QStringLiteral("Vocabulary"), this);
    auto *bindingsSection = makeSectionLabel(QStringLiteral("Bindings"), this);

    auto *generalCard = makeSettingsCard(this);
    auto *generalLayout = qobject_cast<QVBoxLayout *>(generalCard->layout());
    auto *audioCard = makeSettingsCard(this);
    auto *audioLayout = qobject_cast<QVBoxLayout *>(audioCard->layout());
    auto *refinementCard = makeSettingsCard(this);
    auto *refinementLayout = qobject_cast<QVBoxLayout *>(refinementCard->layout());
    auto *outputCard = makeSettingsCard(this);
    auto *outputLayout = qobject_cast<QVBoxLayout *>(outputCard->layout());
    auto *openAiCard = makeSettingsCard(this);
    auto *openAiLayout = qobject_cast<QVBoxLayout *>(openAiCard->layout());
    auto *anthropicCard = makeSettingsCard(this);
    auto *anthropicLayout = qobject_cast<QVBoxLayout *>(anthropicCard->layout());

    auto *primaryOutput = new QLabel(m_controller->primaryOutputStatus(), this);
    m_ydotoolSetupButton = new QPushButton(QStringLiteral("Set up"), this);
    m_ydotoolStartButton = new QPushButton(QStringLiteral("Start service"), this);
    m_ydotoolDisableButton = new QPushButton(QStringLiteral("Disable in Speecher"), this);
    m_ydotoolRemoveButton = new QPushButton(QStringLiteral("Remove setup"), this);
    for (QPushButton *button : {m_ydotoolSetupButton, m_ydotoolStartButton, m_ydotoolDisableButton, m_ydotoolRemoveButton}) {
        button->setMinimumWidth(button->fontMetrics().horizontalAdvance(button->text()) + 36);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    }
    m_authStatus->setObjectName(QStringLiteral("statusText"));
    m_authStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_authStatus->setWordWrap(false);
    m_authStatus->setMinimumWidth(170);
    m_authStatus->setAttribute(Qt::WA_StyledBackground, false);
    m_authStatus->setAutoFillBackground(false);
    m_anthropicWarning->setObjectName(QStringLiteral("statusText"));
    m_anthropicWarning->setForegroundRole(QPalette::WindowText);
    m_anthropicInfoButton = new QPushButton(this);
    m_anthropicInfoButton->setIcon(informationIcon(this));
    m_anthropicInfoButton->setIconSize(QSize(14, 14));
    m_anthropicInfoButton->setFlat(true);
    m_anthropicInfoButton->setCursor(Qt::PointingHandCursor);
    m_anthropicInfoButton->setFixedSize(22, 22);
    m_anthropicInfoButton->setToolTip(QStringLiteral("Compare Claude Code session and OAuth extra usage."));
    m_anthropicInfoButton->setAccessibleName(QStringLiteral("Anthropic auth info"));
    m_anthropicInfoButton->setStyleSheet(QStringLiteral(
        "QPushButton{border:0;border-radius:4px;padding:2px;background:transparent;}"
        "QPushButton:hover{background:palette(midlight);}"
        "QPushButton:pressed{background:palette(mid);}"));
    primaryOutput->setObjectName(QStringLiteral("statusText"));
    for (QLabel *label : {m_authStatus, primaryOutput}) {
        label->setForegroundRole(QPalette::WindowText);
    }

    addRow(generalLayout, makeRow(QStringLiteral("Theme"), QStringLiteral("App colors."), m_theme, generalCard), generalCard);
    addRow(generalLayout, makeRow(QStringLiteral("Pause media"), QStringLiteral("Pause currently playing media while transcribing."), m_pauseMedia, generalCard), generalCard);
    addRow(generalLayout, makeRow(QStringLiteral("Preview words"), QStringLiteral("Trailing words shown in the popup."), m_previewWords, generalCard), generalCard);
    addRow(generalLayout, makeRow(QStringLiteral("Clipboard output"), QStringLiteral("Current platform clipboard path."), primaryOutput, generalCard), generalCard, false);

    addRow(audioLayout,
           makeRow(QStringLiteral("Microphone"),
                   QStringLiteral("Input device used for dictation."),
                   m_audioDevice,
                   audioCard),
           audioCard);
    addRow(audioLayout,
           makeRow(QStringLiteral("Capture mode"),
                   QStringLiteral("Open the microphone only while listening, or keep it warm between captures."),
                   m_captureMode,
                   audioCard),
           audioCard);
    addRow(audioLayout,
           makeRow(QStringLiteral("Pre-roll"),
                   QStringLiteral("Audio kept before speech or before a warm capture starts."),
                   m_preRollMs,
                   audioCard),
           audioCard);
    addRow(audioLayout,
           makeRow(QStringLiteral("Post-roll"),
                   QStringLiteral("Audio kept after stop or after speech falls quiet."),
                   m_postRollMs,
                   audioCard),
           audioCard);
    addRow(audioLayout,
           makeRow(QStringLiteral("Readiness timeout"),
                   QStringLiteral("How long Speecher waits for the first microphone sample."),
                   m_readinessTimeoutMs,
                   audioCard),
           audioCard);
    addRow(audioLayout,
           makeRow(QStringLiteral("Silence trimming"),
                   QStringLiteral("Optional VAD gate before sending audio to the speech provider."),
                   m_vadEnabled,
                   audioCard),
           audioCard);
    addRow(audioLayout,
           makeRow(QStringLiteral("Voice threshold"),
                   QStringLiteral("RMS level required before VAD treats audio as speech."),
                   m_vadThreshold,
                   audioCard),
           audioCard,
           false);

    addRow(outputLayout,
           makeRow(QStringLiteral("Method"),
                   QStringLiteral("How Speecher delivers final text."),
                   m_outputMethod,
                   outputCard),
           outputCard);
    addRow(outputLayout,
           makeRow(QStringLiteral("Restore clipboard"),
                   QStringLiteral("After virtual-keyboard paste, put the previous clipboard contents back."),
                   m_restoreClipboardAfterTyping,
                   outputCard),
           outputCard);
    addRow(outputLayout,
           makeRow(QStringLiteral("Virtual keyboard"),
                   QString(),
                   makeYdotoolControl(m_ydotoolStatus,
                                      m_ydotoolSetupButton,
                                      m_ydotoolStartButton,
                                      m_ydotoolDisableButton,
                                      m_ydotoolRemoveButton,
                                      outputCard),
                   outputCard),
           outputCard,
           false);

    addRow(refinementLayout, makeRow(QStringLiteral("Refinement"), QStringLiteral("Clean up dictated text after capture."), m_provider, refinementCard), refinementCard);
    addRow(refinementLayout, makeRow(QStringLiteral("Refinement style"), QStringLiteral("How strongly dictated text is rewritten."), m_refinementStyle, refinementCard), refinementCard, false);

    addRow(openAiLayout, makeRow(QStringLiteral("OpenAI model"), QStringLiteral("Model used for refinement."), m_openAiModel, openAiCard), openAiCard);
    addRow(openAiLayout, makeRow(QStringLiteral("OpenAI effort"), QStringLiteral("Reasoning effort used for refinement."), m_openAiEffort, openAiCard), openAiCard);
    addRow(openAiLayout, makeRow(QStringLiteral("OpenAI auth mode"), QStringLiteral("Credential source used for refinement."), m_authMode, openAiCard), openAiCard);
    addRow(openAiLayout, makeRow(QStringLiteral("OpenAI auth"), QStringLiteral("Current credential source or app settings key."), m_authControl, openAiCard), openAiCard, false);

    addRow(anthropicLayout,
           makeRow(QStringLiteral("Claude model"),
                   QStringLiteral("Model used for Anthropic refinement."),
                   makeAnthropicModelControl(m_anthropicModel, m_anthropicWarning, &m_anthropicWarningRow, anthropicCard),
                   anthropicCard),
           anthropicCard);
    addRow(anthropicLayout,
           makeRow(QStringLiteral("Claude effort"),
                   QStringLiteral("Token spend and reasoning depth for Anthropic refinement."),
                   m_anthropicEffort,
                   anthropicCard),
           anthropicCard);
    addRow(anthropicLayout,
           makeRow(QStringLiteral("Anthropic auth"),
                   QStringLiteral("How Speecher sends refinement requests to Claude."),
                   m_anthropicAuthMode,
                   anthropicCard,
                   m_anthropicInfoButton),
           anthropicCard,
           false);

    auto *vocabSection = new QFrame(this);
    vocabSection->setObjectName(QStringLiteral("vocabSection"));
    auto *vocabLayout = new QVBoxLayout(vocabSection);
    vocabLayout->setContentsMargins(20, 16, 20, 20);
    vocabLayout->setSpacing(8);
    auto *vocabTitle = new QLabel(QStringLiteral("Extra vocabulary"), vocabSection);
    vocabTitle->setObjectName(QStringLiteral("rowTitle"));
    vocabTitle->setAlignment(Qt::AlignCenter);
    vocabTitle->setForegroundRole(QPalette::WindowText);
    vocabTitle->setAttribute(Qt::WA_StyledBackground, false);
    auto *vocabDescription = new QLabel(QStringLiteral("One term per line. Claude voice uses Deepgram Nova-3 keyterms: 500 tokens and 100 keyterms maximum."), vocabSection);
    vocabDescription->setObjectName(QStringLiteral("rowDescription"));
    vocabDescription->setAlignment(Qt::AlignCenter);
    vocabDescription->setWordWrap(true);
    vocabDescription->setAttribute(Qt::WA_StyledBackground, false);
    vocabLayout->addWidget(vocabTitle);
    vocabLayout->addWidget(vocabDescription);
    vocabLayout->addWidget(m_vocab);
    vocabLayout->addWidget(m_vocabLimit);

    auto *bindingSection = new QFrame(this);
    bindingSection->setObjectName(QStringLiteral("bindingSection"));
    auto *bindingLayout = new QVBoxLayout(bindingSection);
    bindingLayout->setContentsMargins(20, 16, 20, 20);
    bindingLayout->setSpacing(8);
    auto *bindingTitle = new QLabel(QStringLiteral("Phrase bindings"), bindingSection);
    bindingTitle->setObjectName(QStringLiteral("rowTitle"));
    bindingTitle->setAlignment(Qt::AlignCenter);
    bindingTitle->setForegroundRole(QPalette::WindowText);
    bindingTitle->setAttribute(Qt::WA_StyledBackground, false);
    auto *bindingDescription = new QLabel(QStringLiteral("Bindings replace spoken aliases after capture. Matching ignores case and treats punctuation as spaces; replacements are inserted exactly."), bindingSection);
    bindingDescription->setObjectName(QStringLiteral("rowDescription"));
    bindingDescription->setAlignment(Qt::AlignCenter);
    bindingDescription->setWordWrap(true);
    bindingDescription->setAttribute(Qt::WA_StyledBackground, false);
    m_addBindingButton = new QPushButton(QStringLiteral("Add binding"), bindingSection);
    m_addBindingButton->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    m_addBindingButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    bindingLayout->addWidget(bindingTitle);
    bindingLayout->addWidget(bindingDescription);
    bindingLayout->addWidget(m_bindings);
    bindingLayout->addWidget(m_addBindingButton, 0, Qt::AlignRight);

    auto *note = new QLabel(QStringLiteral("Automatic OpenAI auth follows the Codex auth mode when available, then falls back to Codex API key, Codex OAuth, OPENAI_API_KEY, and the app settings key. Codex OAuth uses the ChatGPT Codex backend. The app settings key is stored in the desktop keyring through QtKeychain when available."), this);
    note->setObjectName(QStringLiteral("noteText"));
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::WindowText);
    note->setAttribute(Qt::WA_StyledBackground, false);

    content->addWidget(generalSection);
    content->addWidget(generalCard);
    content->addWidget(audioSection);
    content->addWidget(audioCard);
    content->addWidget(outputSection);
    content->addWidget(outputCard);
    content->addWidget(refinementSection);
    content->addWidget(refinementCard);
    content->addWidget(openAiSection);
    content->addWidget(openAiCard);
    content->addWidget(anthropicSection);
    content->addWidget(anthropicCard);
    content->addWidget(vocabularySection);
    content->addWidget(vocabSection);
    content->addWidget(bindingsSection);
    content->addWidget(bindingSection);
    content->addWidget(note);
    scroll->setWidget(scrollContent);
    root->addWidget(scroll, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    buttons->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    root->addWidget(buttons, 0, Qt::AlignRight);

    generalCard->setStyleSheet(QString::fromLatin1(settingsStyle));
    audioCard->setStyleSheet(QString::fromLatin1(settingsStyle));
    outputCard->setStyleSheet(QString::fromLatin1(settingsStyle));
    refinementCard->setStyleSheet(QString::fromLatin1(settingsStyle));
    openAiCard->setStyleSheet(QString::fromLatin1(settingsStyle));
    anthropicCard->setStyleSheet(QString::fromLatin1(settingsStyle));
    vocabSection->setStyleSheet(QString::fromLatin1(settingsStyle));
    bindingSection->setStyleSheet(QString::fromLatin1(settingsStyle));
    generalSection->setStyleSheet(QString::fromLatin1(settingsStyle));
    audioSection->setStyleSheet(QString::fromLatin1(settingsStyle));
    outputSection->setStyleSheet(QString::fromLatin1(settingsStyle));
    refinementSection->setStyleSheet(QString::fromLatin1(settingsStyle));
    openAiSection->setStyleSheet(QString::fromLatin1(settingsStyle));
    anthropicSection->setStyleSheet(QString::fromLatin1(settingsStyle));
    vocabularySection->setStyleSheet(QString::fromLatin1(settingsStyle));
    bindingsSection->setStyleSheet(QString::fromLatin1(settingsStyle));
    note->setStyleSheet(QString::fromLatin1(settingsStyle));

    if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok)) {
        m_okButton = ok;
        ok->setDefault(true);
        ok->setAutoDefault(true);
        ok->setIcon(QIcon());
    }
    if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel)) {
        cancel->setAutoDefault(false);
        cancel->setIcon(QIcon());
    }
    if (QPushButton *apply = buttons->button(QDialogButtonBox::Apply)) {
        m_applyButton = apply;
        apply->setAutoDefault(false);
        apply->setIcon(QIcon());
    }

    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (save()) {
            accept();
        }
    });
    connect(m_applyButton, &QPushButton::clicked, this, [this] {
        save();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_theme, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_pauseMedia, &QCheckBox::toggled, this, &SettingsDialog::updateButtonState);
    connect(m_audioDevice, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_captureMode, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_vadEnabled, &QCheckBox::toggled, this, [this] {
        updateAudioControls();
        updateButtonState();
    });
    connect(m_preRollMs, &QSpinBox::valueChanged, this, &SettingsDialog::updateButtonState);
    connect(m_postRollMs, &QSpinBox::valueChanged, this, &SettingsDialog::updateButtonState);
    connect(m_readinessTimeoutMs, &QSpinBox::valueChanged, this, &SettingsDialog::updateButtonState);
    connect(m_vadThreshold, &QSpinBox::valueChanged, this, &SettingsDialog::updateButtonState);
    connect(m_provider, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_refinementStyle, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_openAiModel, &QComboBox::currentTextChanged, this, &SettingsDialog::updateButtonState);
    connect(m_openAiEffort, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_anthropicModel, &QComboBox::currentTextChanged, this, [this] {
        updateAnthropicControls();
        updateButtonState();
    });
    connect(m_anthropicEffort, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_anthropicAuthMode, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_anthropicInfoButton, &QPushButton::clicked, this, &SettingsDialog::showAnthropicAuthInfo);
    connect(m_restoreClipboardAfterTyping, &QCheckBox::toggled, this, &SettingsDialog::updateButtonState);
    connect(m_outputMethod, &QComboBox::currentIndexChanged, this, [this] {
        if (m_outputMethod->currentData().toString() == QString::fromLatin1(OutputMethod::Ydotool)) {
            const YdotoolSetupStatus status = YdotoolSetup::probe(m_controller->settings()->ydotoolEnabled());
            if (!status.ready() || !m_controller->settings()->ydotoolEnabled()) {
                QSignalBlocker blocker(m_outputMethod);
                selectData(m_outputMethod, m_controller->settings()->outputMethod());
                QToolTip::showText(m_outputMethod->mapToGlobal(m_outputMethod->rect().bottomLeft()),
                                   QStringLiteral("Set up ydotool first"),
                                   m_outputMethod);
                return;
            }
        }
        updateButtonState();
    });
    connect(m_authMode, &QComboBox::currentIndexChanged, this, [this] {
        updateAuthControl();
        updateButtonState();
    });
    connect(m_apiKey, &QLineEdit::textChanged, this, &SettingsDialog::updateButtonState);
    connect(m_previewWords, &QSpinBox::valueChanged, this, &SettingsDialog::updateButtonState);
    connect(m_ydotoolSetupButton, &QPushButton::clicked, this, &SettingsDialog::setupOrEnableYdotool);
    connect(m_ydotoolStartButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (!YdotoolSetup::startUserService(&error)) {
            QMessageBox::warning(this, QStringLiteral("ydotool service"), error);
        }
        refreshOutputControls();
    });
    connect(m_ydotoolDisableButton, &QPushButton::clicked, this, &SettingsDialog::disableYdotool);
    connect(m_ydotoolRemoveButton, &QPushButton::clicked, this, &SettingsDialog::removeYdotoolSetup);
    connect(m_vocab, &QTableWidget::itemChanged, this, [this] {
        updateVocabularyLimit();
        updateButtonState();
    });
    connect(m_addBindingButton, &QPushButton::clicked, this, [this] {
        editBinding(-1);
    });
    connect(m_bindings, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item) {
            editBinding(item->data(Qt::UserRole).toInt());
        }
    });
    load();
}

void SettingsDialog::load()
{
    SettingsStore *settings = m_controller->settings();
    selectData(m_theme, settings->theme());
    m_pauseMedia->setChecked(settings->pauseMediaDuringTranscription());
    const AudioCaptureSettings audio = settings->audioCaptureSettings();
    refreshAudioDeviceList(audio.deviceId);
    selectData(m_captureMode, audio.mode);
    m_vadEnabled->setChecked(audio.vadEnabled);
    m_preRollMs->setValue(audio.preRollMs);
    m_postRollMs->setValue(audio.postRollMs);
    m_readinessTimeoutMs->setValue(audio.readinessTimeoutMs);
    m_vadThreshold->setValue(audio.vadThresholdPercent);
    selectData(m_provider, settings->refinementProvider());
    selectData(m_refinementStyle, settings->refinementStyle());
    selectEditableText(m_openAiModel, settings->openAiModel());
    selectData(m_openAiEffort, settings->openAiEffort());
    selectEditableText(m_anthropicModel, settings->anthropicModel());
    selectData(m_anthropicEffort, settings->anthropicEffort());
    selectData(m_outputMethod, settings->outputMethod());
    m_restoreClipboardAfterTyping->setChecked(settings->restoreClipboardAfterTyping());
    selectData(m_authMode, settings->openAiAuthMode());
    selectData(m_anthropicAuthMode, settings->anthropicAuthMode());
    m_previewWords->setValue(settings->previewWords());
    m_apiKey->setText(m_controller->secretStore()->apiKey());
    setVocabularyRows(settings->customVocabulary());
    setBindingRules(settings->bindingRules());
    updateVocabularyLimit();
    updateAudioControls();
    updateAuthControl();
    updateAnthropicControls();
    refreshOutputControls();
    updateButtonState();
}

bool SettingsDialog::save()
{
    SettingsStore *settings = m_controller->settings();
    const QList<BindingRule> bindingRules = currentBindingRules();
    const BindingValidationResult bindingValidation = BindingProcessor::validateRules(bindingRules);
    if (!bindingValidation.ok()) {
        QMessageBox::warning(this,
                             QStringLiteral("Bindings not saved"),
                             bindingValidation.messages().join(QStringLiteral("\n")));
        return false;
    }

    settings->setTheme(m_theme->currentData().toString());
    Theme::apply(settings->theme());
    settings->setPauseMediaDuringTranscription(m_pauseMedia->isChecked());
    settings->setAudioCaptureSettings({
        m_audioDevice->currentData().toString(),
        m_captureMode->currentData().toString(),
        m_vadEnabled->isChecked(),
        m_preRollMs->value(),
        m_postRollMs->value(),
        m_readinessTimeoutMs->value(),
        m_vadThreshold->value(),
    });
    settings->setRefinementProvider(m_provider->currentData().toString());
    settings->setRefinementStyle(m_refinementStyle->currentData().toString());
    settings->setOpenAiModel(editableComboValue(m_openAiModel));
    selectEditableText(m_openAiModel, settings->openAiModel());
    settings->setOpenAiEffort(m_openAiEffort->currentData().toString());
    settings->setAnthropicModel(editableComboValue(m_anthropicModel));
    selectEditableText(m_anthropicModel, settings->anthropicModel());
    settings->setAnthropicEffort(m_anthropicEffort->currentData().toString());
    settings->setOutputMethod(m_outputMethod->currentData().toString());
    settings->setRestoreClipboardAfterTyping(m_restoreClipboardAfterTyping->isChecked());
    settings->setOpenAiAuthMode(m_authMode->currentData().toString());
    settings->setAnthropicAuthMode(m_anthropicAuthMode->currentData().toString());
    settings->setPreviewWords(m_previewWords->value());
    settings->setCustomVocabulary(currentVocabulary());
    settings->setBindingRules(bindingValidation.rules);
    setVocabularyRows(settings->customVocabulary());
    setBindingRules(settings->bindingRules());
    updateVocabularyLimit();
    if (settings->openAiAuthMode() == QStringLiteral("settings")) {
        if (!m_controller->secretStore()->saveApiKey(m_apiKey->text().trimmed())) {
            QMessageBox::warning(this,
                                 QStringLiteral("OpenAI key not saved"),
                                 m_controller->secretStore()->status());
            return false;
        }
    }
    updateAuthControl();
    updateAnthropicControls();
    refreshOutputControls();
    updateButtonState();
    return true;
}

bool SettingsDialog::hasChanges() const
{
    const SettingsStore *settings = m_controller->settings();
    const AudioCaptureSettings audio = settings->audioCaptureSettings();
    if (m_theme->currentData().toString() != settings->theme()
        || m_pauseMedia->isChecked() != settings->pauseMediaDuringTranscription()
        || m_audioDevice->currentData().toString() != audio.deviceId
        || m_captureMode->currentData().toString() != audio.mode
        || m_vadEnabled->isChecked() != audio.vadEnabled
        || m_preRollMs->value() != audio.preRollMs
        || m_postRollMs->value() != audio.postRollMs
        || m_readinessTimeoutMs->value() != audio.readinessTimeoutMs
        || m_vadThreshold->value() != audio.vadThresholdPercent
        || m_provider->currentData().toString() != settings->refinementProvider()
        || m_refinementStyle->currentData().toString() != settings->refinementStyle()
        || editableComboValue(m_openAiModel) != settings->openAiModel()
        || m_openAiEffort->currentData().toString() != settings->openAiEffort()
        || editableComboValue(m_anthropicModel) != settings->anthropicModel()
        || m_anthropicEffort->currentData().toString() != settings->anthropicEffort()
        || m_outputMethod->currentData().toString() != settings->outputMethod()
        || m_restoreClipboardAfterTyping->isChecked() != settings->restoreClipboardAfterTyping()
        || m_authMode->currentData().toString() != settings->openAiAuthMode()
        || m_anthropicAuthMode->currentData().toString() != settings->anthropicAuthMode()
        || m_previewWords->value() != settings->previewWords()
        || currentVocabulary() != settings->customVocabulary()
        || currentBindingRules() != settings->bindingRules()) {
        return true;
    }

    return m_authMode->currentData().toString() == QStringLiteral("settings")
        && m_apiKey->text().trimmed() != m_controller->secretStore()->apiKey();
}

QStringList SettingsDialog::currentVocabulary() const
{
    QStringList vocabulary;
    for (int row = 0; row < m_vocab->rowCount(); ++row) {
        const QTableWidgetItem *item = m_vocab->item(row, 0);
        const QString term = item ? item->text().trimmed() : QString();
        if (!term.isEmpty()) {
            vocabulary << term;
        }
    }
    return VocabularyLimit::limited(vocabulary);
}

QList<BindingRule> SettingsDialog::currentBindingRules() const
{
    return m_bindingRules;
}

void SettingsDialog::setVocabularyRows(const QStringList &terms)
{
    QSignalBlocker blocker(m_vocab);
    m_updatingVocabulary = true;

    m_vocab->clearContents();
    m_vocab->setRowCount(qMax(terms.size() + 1, 1));
    for (int row = 0; row < m_vocab->rowCount(); ++row) {
        m_vocab->setItem(row, 0, makeVocabularyItem(row < terms.size() ? terms.at(row) : QString()));
    }
    m_vocab->setCurrentCell(0, 0);

    m_updatingVocabulary = false;
}

void SettingsDialog::setBindingRules(const QList<BindingRule> &rules)
{
    m_bindingRules = rules;
    refreshBindingList();
}

void SettingsDialog::refreshBindingList()
{
    QScrollBar *scrollBar = m_scroll ? m_scroll->verticalScrollBar() : nullptr;
    const int scrollValue = scrollBar ? scrollBar->value() : 0;

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
            updateButtonState();
        });
    }

    if (scrollBar) {
        scrollBar->setValue(qMin(scrollValue, scrollBar->maximum()));
        QTimer::singleShot(0, this, [this, scrollValue] {
            QScrollBar *delayedScrollBar = m_scroll ? m_scroll->verticalScrollBar() : nullptr;
            if (delayedScrollBar) {
                delayedScrollBar->setValue(qMin(scrollValue, delayedScrollBar->maximum()));
            }
        });
    }
}

void SettingsDialog::editBinding(int row)
{
    const bool editing = row >= 0 && row < m_bindingRules.size();

    QDialog dialog(this);
    dialog.setWindowTitle(editing ? QStringLiteral("Edit binding") : QStringLiteral("Add binding"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(8);

    auto *phraseLabel = new QLabel(QStringLiteral("Binding"), &dialog);
    auto *phrase = new QLineEdit(&dialog);
    phrase->setClearButtonEnabled(true);

    auto *replacementLabel = new QLabel(QStringLiteral("Replacement"), &dialog);
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
                                     QStringLiteral("Binding not saved"),
                                     validation.messages().join(QStringLiteral("\n")));
                return;
            }

            m_bindingRules = validation.rules;
            refreshBindingList();
            updateButtonState();
            dialog.accept();
        });
    }
    if (deleteButton) {
        connect(deleteButton, &QPushButton::clicked, &dialog, [this, row, &dialog] {
            if (row >= 0 && row < m_bindingRules.size()) {
                m_bindingRules.removeAt(row);
                refreshBindingList();
                updateButtonState();
            }
            dialog.accept();
        });
    }

    dialog.resize(560, 430);
    phrase->setFocus(Qt::OtherFocusReason);
    dialog.exec();
}

void SettingsDialog::refreshAudioDeviceList(const QString &selectedDeviceId)
{
    const QSignalBlocker blocker(m_audioDevice);
    m_audioDevice->clear();

    const QList<AudioInputDeviceInfo> devices = m_controller->platform()->availableAudioInputDevices();
    if (devices.isEmpty()) {
        m_audioDevice->addItem(QStringLiteral("No microphones found"), QString());
        setComboItemEnabled(m_audioDevice,
                            0,
                            false,
                            QStringLiteral("Connect or enable an input device, then reopen Settings."));
        if (!selectedDeviceId.isEmpty()) {
            m_audioDevice->addItem(QStringLiteral("Missing microphone"), selectedDeviceId);
            setComboItemEnabled(m_audioDevice,
                                1,
                                false,
                                QStringLiteral("This saved microphone is not currently available."));
            selectData(m_audioDevice, selectedDeviceId);
        }
        return;
    }

    m_audioDevice->addItem(QStringLiteral("System default"), QString());
    bool selectedFound = selectedDeviceId.isEmpty();
    for (const AudioInputDeviceInfo &device : devices) {
        const QString label = device.isDefault
            ? QStringLiteral("%1 (default)").arg(device.label)
            : device.label;
        m_audioDevice->addItem(label, device.id);
        selectedFound = selectedFound || device.id == selectedDeviceId;
    }

    if (!selectedFound) {
        m_audioDevice->addItem(QStringLiteral("Missing microphone"), selectedDeviceId);
        const int missingIndex = m_audioDevice->count() - 1;
        setComboItemEnabled(m_audioDevice,
                            missingIndex,
                            false,
                            QStringLiteral("This saved microphone is not currently available."));
    }

    selectData(m_audioDevice, selectedDeviceId);
}

void SettingsDialog::updateAudioControls()
{
    m_vadThreshold->setEnabled(m_vadEnabled->isChecked());
}

void SettingsDialog::updateAuthControl()
{
    const QString mode = m_authMode->currentData().toString();
    if (mode == QStringLiteral("settings")) {
        m_authControl->setCurrentWidget(m_apiKey);
        m_apiKey->setPlaceholderText(m_controller->secretStore()->status());
        return;
    }
    m_authStatus->setText(OpenAiAuthProvider(m_controller->secretStore(), mode).status());
    m_authControl->setCurrentWidget(m_authStatus);
}

void SettingsDialog::updateAnthropicControls()
{
    const QString model = editableComboValue(m_anthropicModel).toCaseFolded();
    const bool haiku = model.contains(QStringLiteral("haiku"));
    if (m_anthropicWarningRow) {
        m_anthropicWarningRow->setVisible(haiku);
    }
    m_anthropicWarning->setText(haiku
                                    ? QStringLiteral("Haiku may treat transcript as instructions.")
                                    : QString());
}

void SettingsDialog::showAnthropicAuthInfo()
{
    QMessageBox::information(
        this,
        QStringLiteral("Anthropic auth"),
        QStringLiteral("Claude Code session starts and keeps an interactive Claude Code session in Speecher's background daemon. Refinement messages are sent to that session, then the session is cleared after each result. This uses your Claude Code subscription usage.\n\n"
                       "OAuth extra usage reads the Claude Code OAuth token from ~/.claude/.credentials.json and calls the Anthropic Messages API directly. Anthropic can route this as extra usage billed at API rates, and this path is the one to use when you want direct API-style routing from Speecher."));
}

void SettingsDialog::refreshOutputControls()
{
    const YdotoolSetupStatus status = YdotoolSetup::probe(m_controller->settings()->ydotoolEnabled());
    const bool ydotoolEnabled = m_controller->settings()->ydotoolEnabled() && status.ready();
    const int ydotoolIndex = m_outputMethod->findData(QString::fromLatin1(OutputMethod::Ydotool));
    setComboItemEnabled(m_outputMethod,
                        ydotoolIndex,
                        ydotoolEnabled,
                        ydotoolEnabled ? QString() : QStringLiteral("Set up ydotool first"));
    if (!ydotoolEnabled && m_outputMethod->currentData().toString() == QString::fromLatin1(OutputMethod::Ydotool)) {
        QSignalBlocker blocker(m_outputMethod);
        selectData(m_outputMethod, QString::fromLatin1(OutputMethod::Automatic));
    }
    m_outputMethod->setToolTip(ydotoolEnabled
                                   ? QStringLiteral("Automatic tries ydotool paste, wl-copy, then Qt clipboard.")
                                   : QStringLiteral("Type with ydotool paste is disabled until virtual keyboard setup passes."));
    m_ydotoolStatus->setText(status.label + QStringLiteral(". ") + status.detail);
    updateYdotoolButtons();
}

void SettingsDialog::updateYdotoolButtons()
{
    const YdotoolSetupStatus status = YdotoolSetup::probe(m_controller->settings()->ydotoolEnabled());
    const bool ready = status.ready();
    const bool disabled = status.state == YdotoolSetupState::Disabled;
    const bool daemonMissing = status.state == YdotoolSetupState::DaemonNotRunning;
    const bool setupInstalled = status.speecherManagedSetupInstalled || ready || disabled;
    const QString setupFirst = QStringLiteral("Run setup first");
    m_ydotoolSetupButton->setText(disabled && status.speecherManagedSetupInstalled ? QStringLiteral("Enable") : QStringLiteral("Set up"));
    m_ydotoolSetupButton->setVisible(!ready || disabled);
    m_ydotoolSetupButton->setEnabled(status.state != YdotoolSetupState::NeedsSignOut);
    m_ydotoolStartButton->setVisible(daemonMissing);
    m_ydotoolStartButton->setEnabled(setupInstalled);
    m_ydotoolStartButton->setToolTip(setupInstalled ? QString() : setupFirst);
    m_ydotoolDisableButton->setVisible(ready && m_controller->settings()->ydotoolEnabled());
    m_ydotoolRemoveButton->setVisible(status.speecherManagedSetupInstalled);
    m_ydotoolRemoveButton->setEnabled(status.speecherManagedSetupInstalled);
    m_ydotoolRemoveButton->setToolTip(setupInstalled ? QString() : setupFirst);
}

void SettingsDialog::updateButtonState()
{
    const bool changed = hasChanges();
    if (m_okButton) {
        m_okButton->setEnabled(changed);
    }
    if (m_applyButton) {
        m_applyButton->setEnabled(changed);
    }
}

void SettingsDialog::setupOrEnableYdotool()
{
    const YdotoolSetupStatus status = YdotoolSetup::probe(m_controller->settings()->ydotoolEnabled());
    if (status.state == YdotoolSetupState::Disabled && status.speecherManagedSetupInstalled) {
        if (verifyYdotoolTyping()) {
            m_controller->settings()->setYdotoolEnabled(true);
            refreshOutputControls();
            updateButtonState();
        }
        return;
    }

    const int answer = QMessageBox::question(this,
                                             QStringLiteral("Set up virtual keyboard"),
                                             QStringLiteral("Speecher will ask for administrator permission to install ydotool if needed, load uinput, configure a speecher-uinput group, install udev rules, and install a user-level ydotoold service. Speecher itself remains unprivileged at runtime."),
                                             QMessageBox::Cancel | QMessageBox::Ok,
                                             QMessageBox::Ok);
    if (answer != QMessageBox::Ok) {
        return;
    }

    QString error;
    if (!YdotoolSetup::runHelper(YdotoolSetup::HelperAction::Install, &error)) {
        QMessageBox::warning(this, QStringLiteral("ydotool setup failed"), error);
        refreshOutputControls();
        return;
    }
    if (!YdotoolSetup::startUserService(&error)) {
        QMessageBox::warning(this, QStringLiteral("ydotool service"), error);
    }
    if (verifyYdotoolTyping()) {
        m_controller->settings()->setYdotoolEnabled(true);
    }
    refreshOutputControls();
    updateButtonState();
}

void SettingsDialog::disableYdotool()
{
    m_controller->settings()->setYdotoolEnabled(false);
    QSignalBlocker blocker(m_outputMethod);
    selectData(m_outputMethod, m_controller->settings()->outputMethod());
    refreshOutputControls();
    updateButtonState();
}

void SettingsDialog::removeYdotoolSetup()
{
    const int answer = QMessageBox::question(this,
                                             QStringLiteral("Remove virtual keyboard setup"),
                                             QStringLiteral("Speecher will ask for administrator permission to remove the service, udev rule, module-load file, and Speecher-specific group membership it manages. It will not uninstall the distro ydotool package."),
                                             QMessageBox::Cancel | QMessageBox::Ok,
                                             QMessageBox::Cancel);
    if (answer != QMessageBox::Ok) {
        return;
    }
    QString error;
    QString stopError;
    YdotoolSetup::stopUserService(&stopError);
    if (!YdotoolSetup::runHelper(YdotoolSetup::HelperAction::Remove, &error)) {
        QMessageBox::warning(this, QStringLiteral("ydotool removal failed"), error);
        refreshOutputControls();
        return;
    }

    const YdotoolSetupStatus status = YdotoolSetup::probe(false);
    if (status.speecherManagedSetupInstalled) {
        QMessageBox::warning(this,
                             QStringLiteral("ydotool removal incomplete"),
                             QStringLiteral("The privileged helper finished, but Speecher-managed setup files are still detected."));
        refreshOutputControls();
        return;
    }
    m_controller->settings()->setYdotoolEnabled(false);
    QSignalBlocker blocker(m_outputMethod);
    selectData(m_outputMethod, m_controller->settings()->outputMethod());
    refreshOutputControls();
    updateButtonState();
}

bool SettingsDialog::verifyYdotoolTyping()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Verify ydotool"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(QStringLiteral("Keep this field focused while Speecher tests virtual keyboard input."), &dialog);
    label->setWordWrap(true);
    auto *field = new QLineEdit(&dialog);
    field->setClearButtonEnabled(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *run = buttons->addButton(QStringLiteral("Run test"), QDialogButtonBox::AcceptRole);
    layout->addWidget(label);
    layout->addWidget(field);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(run, &QPushButton::clicked, &dialog, [field, &dialog] {
        field->clear();
        field->setFocus(Qt::OtherFocusReason);
        QTimer::singleShot(150, field, [field, &dialog] {
            QString error;
            YdotoolDelivery ydotool;
            const QString expected = QStringLiteral("speecher test");
            if (!ydotool.type(expected, &error)) {
                QMessageBox::warning(&dialog, QStringLiteral("ydotool verification failed"), error);
                return;
            }
            QTimer::singleShot(350, field, [field, expected, &dialog] {
                if (field->text() == expected) {
                    dialog.accept();
                } else {
                    QMessageBox::warning(&dialog,
                                         QStringLiteral("ydotool verification failed"),
                                         QStringLiteral("The test field did not receive the expected text."));
                }
            });
        });
    });
    dialog.resize(420, dialog.sizeHint().height());
    field->setFocus(Qt::OtherFocusReason);
    return dialog.exec() == QDialog::Accepted;
}

void SettingsDialog::updateVocabularyLimit()
{
    if (m_updatingVocabulary) {
        return;
    }

    QStringList vocabulary;
    for (int row = 0; row < m_vocab->rowCount(); ++row) {
        const QTableWidgetItem *item = m_vocab->item(row, 0);
        vocabulary << (item ? item->text() : QString());
    }
    QStringList terms;
    for (const QString &line : vocabulary) {
        const QString term = line.trimmed();
        if (!term.isEmpty()) {
            terms << term;
        }
    }

    QStringList uniqueTerms;
    for (const QString &term : vocabulary) {
        const QString normalized = term.simplified();
        if (!normalized.isEmpty() && !uniqueTerms.contains(normalized)) {
            uniqueTerms << normalized;
        }
    }

    const QStringList limited = VocabularyLimit::limited(vocabulary);
    if (limited != terms
        && (VocabularyLimit::tokenCount(uniqueTerms) > VocabularyLimit::maxTokens || uniqueTerms.size() > VocabularyLimit::maxKeyterms)) {
        setVocabularyRows(limited);
    } else if (m_vocab->rowCount() == 0 || !m_vocab->item(m_vocab->rowCount() - 1, 0)
               || !m_vocab->item(m_vocab->rowCount() - 1, 0)->text().trimmed().isEmpty()) {
        const QSignalBlocker blocker(m_vocab);
        m_vocab->insertRow(m_vocab->rowCount());
        m_vocab->setItem(m_vocab->rowCount() - 1, 0, makeVocabularyItem());
    }

    m_vocabLimit->setText(VocabularyLimit::summary(currentVocabulary()));
}

} // namespace speecher
