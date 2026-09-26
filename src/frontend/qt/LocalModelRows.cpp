#include "frontend/qt/LocalModelRows.h"

#include "app/LocalSetup.h"
#include "providers/LocalModelStore.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace speecher {
namespace {

QLabel *factLabel(QWidget *parent)
{
    auto *label = new settings::WrappingLabel(parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QString werLine(double wer)
{
    return QString::number(wer, 'f', 2) + QLatin1Char('%');
}

class LocalModelBrowser final : public QWidget {
public:
    LocalModelBrowser(LocalSetup &setup, std::function<void()> notifyChanged, QWidget *parent)
        : QWidget(parent)
        , m_setup(setup)
        , m_notifyChanged(std::move(notifyChanged))
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(settings::largeSpacing());

        m_hardware = new QLabel(this);
        m_hardware->setObjectName(QStringLiteral("localModelsHardware"));
        m_hardware->setWordWrap(true);
        m_hardware->setForegroundRole(QPalette::PlaceholderText);
        layout->addWidget(m_hardware);

        auto *columns = new QHBoxLayout;
        columns->setSpacing(settings::largeSpacing());
        m_list = new QListWidget(this);
        m_list->setObjectName(QStringLiteral("localModelList"));
        m_list->setIconSize(QSize(settings::gridUnit(), settings::gridUnit()));
        m_list->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // As wide as its longest row, so no fact is elided; the detail takes
        // the rest.
        m_list->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Maximum);
        for (const LocalModel &model : localModelCatalog()) {
            auto *item = new QListWidgetItem(m_list);
            item->setData(Qt::UserRole, model.id);
        }
        columns->addWidget(m_list, 0, Qt::AlignTop);
        columns->addWidget(makeDetail(), 1);
        layout->addLayout(columns);

        connect(m_list, &QListWidget::itemClicked, this, [this] { m_userPicked = true; });
        connect(m_list, &QListWidget::currentRowChanged, this, [this] { refresh(); });
        connect(&m_setup, &LocalSetup::changed, this, [this] { refresh(); });
        connect(&m_setup.models(), &LocalModelStore::downloadProgress, this, [this] { refresh(); });
        refresh();
    }

    QVariant value() const { return m_inUse; }

    void setValue(const QVariant &value)
    {
        m_inUse = value.toString();
        refresh();
    }

private:
    QWidget *makeDetail()
    {
        auto *detail = new QWidget(this);
        auto *layout = new QVBoxLayout(detail);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(settings::smallSpacing());

        m_name = new QLabel(detail);
        m_name->setObjectName(QStringLiteral("localModelName"));
        QFont bold = m_name->font();
        bold.setBold(true);
        m_name->setFont(bold);
        layout->addWidget(m_name);
        m_subtitle = new QLabel(detail);
        m_subtitle->setForegroundRole(QPalette::PlaceholderText);
        m_subtitle->setFont(settings::smallFont(m_subtitle->font()));
        layout->addWidget(m_subtitle);

        auto *facts = new QFormLayout;
        facts->setContentsMargins(0, settings::smallSpacing(), 0, settings::smallSpacing());
        facts->setHorizontalSpacing(settings::largeSpacing());
        facts->setVerticalSpacing(settings::smallSpacing());
        facts->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        facts->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
        const auto addFact = [&](const QString &name) {
            auto *key = new QLabel(name, detail);
            key->setForegroundRole(QPalette::PlaceholderText);
            QLabel *value = factLabel(detail);
            facts->addRow(key, value);
            return value;
        };
        m_size = addFact(QStringLiteral("Download"));
        m_speed = addFact(QStringLiteral("Speed here"));
        m_wer = addFact(QStringLiteral("Word error rate"));
        m_textShows = addFact(QStringLiteral("Text shows"));
        m_language = addFact(QStringLiteral("Language"));
        m_licence = addFact(QStringLiteral("Licence"));
        layout->addLayout(facts);

        m_prosCons = factLabel(detail);
        m_prosCons->setFont(settings::smallFont(m_prosCons->font()));
        layout->addWidget(m_prosCons);

        m_problem = factLabel(detail);
        m_problem->setObjectName(QStringLiteral("localModelProblem"));
        layout->addWidget(m_problem);

        auto *actions = new QHBoxLayout;
        actions->setSpacing(settings::relatedSpacing());
        m_state = new QLabel(detail);
        m_state->setObjectName(QStringLiteral("localModelState"));
        actions->addWidget(m_state);
        m_progress = new QProgressBar(detail);
        m_progress->setObjectName(QStringLiteral("localModelProgress"));
        m_progress->setTextVisible(false);
        m_progress->setMaximumWidth(settings::gridUnit() * 8);
        actions->addWidget(m_progress);
        m_download = addButton(actions, QStringLiteral("localModelDownload"), detail);
        m_cancel = addButton(actions, QStringLiteral("localModelCancel"), detail, QStringLiteral("Cancel"));
        m_use = addButton(actions, QStringLiteral("localModelUse"), detail, QStringLiteral("Use this model"));
        m_test = addButton(actions, QStringLiteral("localModelTestSpeed"), detail, QStringLiteral("Test speed"));
        m_delete = addButton(actions, QStringLiteral("localModelDelete"), detail, QStringLiteral("Delete"));
        actions->addStretch();
        layout->addLayout(actions);
        layout->addStretch();

        connect(m_download, &QPushButton::clicked, this, [this] { m_setup.download(selected()); });
        connect(m_cancel, &QPushButton::clicked, this, [this] { m_setup.cancelDownload(selected().id); });
        connect(m_use, &QPushButton::clicked, this, [this] {
            m_inUse = selected().id;
            refresh();
            m_notifyChanged();
        });
        connect(m_test, &QPushButton::clicked, this, [this] { m_setup.runSpeedTest(selected().id); });
        connect(m_delete, &QPushButton::clicked, this, [this] { m_setup.removeModel(selected()); });
        return detail;
    }

    QPushButton *addButton(QHBoxLayout *layout, const QString &name, QWidget *parent,
                           const QString &text = {})
    {
        auto *button = new QPushButton(text, parent);
        button->setObjectName(name);
        layout->addWidget(button);
        return button;
    }

    void selectModel(const QString &modelId)
    {
        for (int row = 0; row < m_list->count(); ++row) {
            if (m_list->item(row)->data(Qt::UserRole).toString() == modelId) {
                m_list->setCurrentRow(row);
                return;
            }
        }
    }

    const LocalModel &selected() const
    {
        const QListWidgetItem *item = m_list->currentItem();
        const LocalModel *model = item ? findLocalModel(item->data(Qt::UserRole).toString()) : nullptr;
        return model ? *model : m_setup.suggestedModel();
    }

    void refresh()
    {
        // Open on the model in use, else on the suggestion, which is only
        // known once the hardware probe has answered.
        if (!m_userPicked) {
            const QSignalBlocker blocker(m_list);
            selectModel(m_inUse.isEmpty() ? m_setup.suggestedModel().id : m_inUse);
        }
        const HardwareProfile &hardware = m_setup.hardware().profile;
        const QString suggested = m_setup.suggestedModel().id;
        m_hardware->setText(m_setup.hardwareLine());
        for (int row = 0; row < m_list->count(); ++row) {
            QListWidgetItem *item = m_list->item(row);
            const LocalModel &model = *findLocalModel(item->data(Qt::UserRole).toString());
            const QString verdict = model.id == suggested && m_setup.hardwareKnown()
                ? QStringLiteral("suggested")
                : modelFitLabel(modelFit(model, hardware)).toLower();
            item->setText(QStringLiteral("%1\n%2 · %3 WER · %4")
                              .arg(model.name, downloadSizeText(model.sizeBytes),
                                   werLine(model.librispeechCleanWer), verdict));
            const bool downloaded = m_setup.models().isDownloaded(model);
            item->setIcon(QIcon::fromTheme(downloaded ? QStringLiteral("dialog-ok")
                                                      : QStringLiteral("download")));
        }
        showDetail(selected(), hardware, suggested);
    }

    void showDetail(const LocalModel &model, const HardwareProfile &hardware, const QString &suggested)
    {
        const ModelFit fit = modelFit(model, hardware);
        m_name->setText(model.name);
        m_subtitle->setText(model.id == suggested && m_setup.hardwareKnown()
                                ? QStringLiteral("Suggested for this computer")
                                : model.fileName);
        m_size->setText(QStringLiteral("%1 · %2").arg(downloadSizeText(model.sizeBytes), modelFitLabel(fit)));
        m_speed->setText(m_setup.speedTestRunning(model.id)
                             ? QStringLiteral("Testing…")
                             : localModelSpeedLine(model, hardware, m_setup.measuredSeconds(model.id)));
        m_wer->setText(QStringLiteral("%1 clear speech (LibriSpeech)\n%2 everyday speech (FLEURS)")
                           .arg(werLine(model.librispeechCleanWer), werLine(model.fleursEnglishWer)));
        m_textShows->setText(model.streams ? QStringLiteral("As you speak") : QStringLiteral("After you stop"));
        m_language->setText(QStringLiteral("English"));
        m_licence->setText(model.licence);
        QStringList notes;
        for (const QString &pro : model.pros) {
            notes.append(QStringLiteral("+ ") + pro);
        }
        for (const QString &con : model.cons) {
            notes.append(QStringLiteral("− ") + con);
        }
        m_prosCons->setText(notes.join(QLatin1Char('\n')));

        const QString problem = m_setup.downloadError(model.id).isEmpty() ? m_setup.speedTestError(model.id)
                                                                          : m_setup.downloadError(model.id);
        m_problem->setText(problem);
        m_problem->setVisible(!problem.isEmpty());

        const auto progress = m_setup.downloadProgress(model.id);
        const bool downloaded = !progress && m_setup.models().isDownloaded(model);
        const bool inUse = downloaded && model.id == m_inUse;
        m_progress->setVisible(bool(progress));
        m_cancel->setVisible(bool(progress));
        if (progress) {
            m_progress->setRange(0, 1000);
            m_progress->setValue(progress->second > 0 ? int(progress->first * 1000 / progress->second) : 0);
            m_state->setText(QStringLiteral("%1 of %2").arg(downloadSizeText(progress->first),
                                                            downloadSizeText(model.sizeBytes)));
        } else {
            m_state->setText(inUse ? QStringLiteral("In use") : QString());
        }
        m_state->setVisible(!m_state->text().isEmpty());
        m_download->setVisible(!progress && !downloaded);
        m_download->setEnabled(fit != ModelFit::TooLarge);
        m_download->setText(fit == ModelFit::TooLarge
                                ? QStringLiteral("Too large for this computer")
                                : QStringLiteral("Download %1").arg(downloadSizeText(model.sizeBytes)));
        m_use->setVisible(downloaded && !inUse);
        m_test->setVisible(downloaded);
        m_test->setEnabled(!m_setup.speedTestRunning(model.id));
        m_delete->setVisible(downloaded);
    }

    LocalSetup &m_setup;
    std::function<void()> m_notifyChanged;
    QString m_inUse;
    bool m_userPicked = false;
    QLabel *m_hardware = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_subtitle = nullptr;
    QLabel *m_size = nullptr;
    QLabel *m_speed = nullptr;
    QLabel *m_wer = nullptr;
    QLabel *m_textShows = nullptr;
    QLabel *m_language = nullptr;
    QLabel *m_licence = nullptr;
    QLabel *m_prosCons = nullptr;
    QLabel *m_problem = nullptr;
    QLabel *m_state = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_download = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_use = nullptr;
    QPushButton *m_test = nullptr;
    QPushButton *m_delete = nullptr;
};

} // namespace

SchemaCustomRowFactory localModelRows(LocalSetup &setup)
{
    return [&setup](const SettingsRow &descriptor, QWidget *parent, std::function<void()> notifyChanged) {
        if (descriptor.id != QStringLiteral("localModelBrowser")) {
            return SchemaCustomRow{};
        }
        auto *browser = new LocalModelBrowser(setup, std::move(notifyChanged), parent);
        return SchemaCustomRow{browser,
                               [browser] { return browser->value(); },
                               [browser](const QVariant &value) { browser->setValue(value); },
                               true};
    };
}

} // namespace speecher
