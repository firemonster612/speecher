#include "frontend/qt/LocalModelRows.h"

#include "app/LocalSetup.h"
#include "providers/LocalModelStore.h"
#include "ui/InlineMessage.h"
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
        m_list->setItemDelegate(new BadgeDelegate(m_list));
        m_list->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // As wide as its longest row with its badge, so nothing is elided; the
        // detail takes the rest.
        m_list->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Maximum);
        for (const LocalModel &model : localModelCatalog()) {
            auto *item = new QListWidgetItem(m_list);
            item->setData(Qt::UserRole, model.id);
            item->setData(BadgeDelegate::TextRole, modelRatingLabel(model.rating));
            item->setData(BadgeDelegate::ToneRole, int(modelRatingTone(model.rating)));
            // The badge is only painted, so a screen reader hears it here.
            item->setData(Qt::AccessibleTextRole,
                          QStringLiteral("%1, %2").arg(model.name, modelRatingLabel(model.rating)));
        }
        columns->addWidget(m_list, 0, Qt::AlignTop);
        columns->addWidget(makeFacts(), 1, Qt::AlignTop);
        layout->addLayout(columns);
        // The rest runs the card's full width, below the list and the facts,
        // so none of it wraps into the facts' narrow column.
        layout->addWidget(settings::makeSeparator(this));
        m_prosCons = factLabel(this);
        m_prosCons->setFont(settings::smallFont(m_prosCons->font()));
        layout->addWidget(m_prosCons);
        m_problem = new InlineMessage(this);
        m_problem->setType(InlineMessage::Type::Warning);
        m_problem->setCloseButtonVisible(false);
        m_problem->setObjectName(QStringLiteral("localModelProblem"));
        layout->addWidget(m_problem);
        layout->addWidget(settings::makeSeparator(this));
        layout->addLayout(makeActions());

        // Only refresh() moves the selection with signals blocked, so any
        // change that arrives here is the person's, by mouse or keyboard.
        connect(m_list, &QListWidget::currentRowChanged, this, [this] {
            m_userPicked = true;
            refresh();
        });
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
    QWidget *makeFacts()
    {
        auto *detail = new QWidget(this);
        auto *layout = new QVBoxLayout(detail);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(settings::smallSpacing());

        auto *title = new QHBoxLayout;
        title->setSpacing(settings::smallSpacing() * 2);
        m_name = new QLabel(detail);
        m_name->setObjectName(QStringLiteral("localModelName"));
        QFont bold = m_name->font();
        bold.setBold(true);
        m_name->setFont(bold);
        title->addWidget(m_name);
        m_rating = new Badge(QString(), Badge::Tone::Neutral, detail);
        m_rating->setObjectName(QStringLiteral("localModelRating"));
        title->addWidget(m_rating, 0, Qt::AlignVCenter);
        title->addStretch();
        layout->addLayout(title);
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
        const LocalModelFactLabels names;
        m_bestFor = addFact(names.bestFor);
        m_bestFor->setObjectName(QStringLiteral("localModelBestFor"));
        m_size = addFact(names.download);
        m_speed = addFact(names.speedHere);
        m_wer = addFact(names.wordErrorRate);
        m_textShows = addFact(names.textShows);
        m_language = addFact(names.language);
        m_licence = addFact(names.license);
        m_wer->setToolTip(wordErrorRateSources());
        layout->addLayout(facts);
        return detail;
    }

    QHBoxLayout *makeActions()
    {
        auto *actions = new QHBoxLayout;
        actions->setSpacing(settings::relatedSpacing());
        m_state = new QLabel(this);
        m_state->setObjectName(QStringLiteral("localModelState"));
        actions->addWidget(m_state);
        m_progress = new QProgressBar(this);
        m_progress->setObjectName(QStringLiteral("localModelProgress"));
        m_progress->setTextVisible(false);
        m_progress->setMaximumWidth(settings::gridUnit() * 8);
        actions->addWidget(m_progress);
        actions->addStretch();
        m_download = addButton(actions, QStringLiteral("localModelDownload"), this);
        m_cancel = addButton(actions, QStringLiteral("localModelCancel"), this, QStringLiteral("Cancel"));
        m_use = addButton(actions, QStringLiteral("localModelUse"), this, QStringLiteral("Use this model"));
        m_test = addButton(actions, QStringLiteral("localModelTestSpeed"), this, QStringLiteral("Test speed"));
        m_delete = addButton(actions, QStringLiteral("localModelDelete"), this, QStringLiteral("Delete"));

        connect(m_download, &QPushButton::clicked, this, [this] { m_setup.download(selected()); });
        connect(m_cancel, &QPushButton::clicked, this, [this] { m_setup.cancelDownload(selected().id); });
        connect(m_use, &QPushButton::clicked, this, [this] {
            m_inUse = selected().id;
            refresh();
            m_notifyChanged();
        });
        connect(m_test, &QPushButton::clicked, this, [this] { m_setup.runSpeedTest(selected().id); });
        connect(m_delete, &QPushButton::clicked, this, [this] { m_setup.removeModel(selected()); });
        return actions;
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
        const QString suggested = m_setup.suggestedModel().id;
        m_hardware->setText(m_setup.hardwareLine());
        // Name, size and error rate only, so the list stays narrow and the
        // facts beside it get the width; the fit verdict is in the facts.
        for (int row = 0; row < m_list->count(); ++row) {
            QListWidgetItem *item = m_list->item(row);
            const LocalModel &model = *findLocalModel(item->data(Qt::UserRole).toString());
            item->setText(QStringLiteral("%1\n%2 · %3 WER")
                              .arg(model.name, downloadSizeText(model.sizeBytes),
                                   werLine(model.librispeechCleanWer)));
            const bool downloaded = m_setup.modelState(model).downloaded;
            item->setIcon(QIcon::fromTheme(downloaded ? QStringLiteral("dialog-ok")
                                                      : QStringLiteral("download")));
        }
        showDetail(selected(), suggested);
    }

    void showDetail(const LocalModel &model, const QString &suggested)
    {
        const auto state = m_setup.modelState(model);
        const bool tooLarge = m_setup.fit(model) == ModelFit::TooLarge;
        m_name->setText(model.name);
        m_rating->setBadge(modelRatingLabel(model.rating), modelRatingTone(model.rating));
        m_bestFor->setText(model.bestFor);
        m_subtitle->setText(model.id == suggested && m_setup.hardwareKnown()
                                ? QStringLiteral("Suggested for this computer")
                                : model.fileName);
        m_size->setText(QStringLiteral("%1 · %2").arg(downloadSizeText(model.sizeBytes), m_setup.fitLabel(model)));
        m_speed->setText(state.speedDetail);
        m_wer->setText(QStringLiteral("%1 clear speech\n%2 everyday speech")
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

        m_problem->setText(state.problem);
        m_problem->setVisible(!state.problem.isEmpty());
        const auto progress = m_setup.downloadProgress(model.id);
        const bool downloaded = state.downloaded;
        const bool inUse = state.inUse;
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
        m_download->setEnabled(!tooLarge);
        m_download->setText(tooLarge
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
    Badge *m_rating = nullptr;
    QLabel *m_subtitle = nullptr;
    QLabel *m_bestFor = nullptr;
    QLabel *m_size = nullptr;
    QLabel *m_speed = nullptr;
    QLabel *m_wer = nullptr;
    QLabel *m_textShows = nullptr;
    QLabel *m_language = nullptr;
    QLabel *m_licence = nullptr;
    QLabel *m_prosCons = nullptr;
    InlineMessage *m_problem = nullptr;
    QLabel *m_state = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_download = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_use = nullptr;
    QPushButton *m_test = nullptr;
    QPushButton *m_delete = nullptr;
};

} // namespace

Badge::Tone modelRatingTone(ModelRating rating)
{
    switch (rating) {
    case ModelRating::Recommended:
        return Badge::Tone::Accent;
    case ModelRating::NotRecommended:
        return Badge::Tone::Negative;
    case ModelRating::Good:
    case ModelRating::Situational:
        break;
    }
    return Badge::Tone::Neutral;
}

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
