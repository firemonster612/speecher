#include "frontend/qt/SchemaSettingsPage.h"

#include "app/PlatformComposition.h"
#include "frontend/qt/WritingProfileGrid.h"
#include "providers/ProviderRegistry.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMediaDevices>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>

namespace speecher {

namespace {

QStringList optionIds(const QList<RowOption> &options)
{
    QStringList ids;
    ids.reserve(options.size());
    for (const RowOption &option : options) {
        ids.append(option.id);
    }
    return ids;
}

QStringList itemIds(const QComboBox *combo)
{
    QStringList ids;
    ids.reserve(combo->count());
    for (int index = 0; index < combo->count(); ++index) {
        ids.append(combo->itemData(index).toString());
    }
    return ids;
}

// Rebuilding a combo resets its selection, so leave one that already offers the
// same choices alone: the caller selects the value straight after.
void setOptions(QComboBox *combo, const QList<RowOption> &options)
{
    if (itemIds(combo) == optionIds(options)) {
        return;
    }
    const QSignalBlocker blocker(combo);
    combo->clear();
    for (const RowOption &option : options) {
        combo->addItem(option.label, option.id);
        if (!option.enabled) {
            settings::setComboItemEnabled(combo, combo->count() - 1, false, option.help);
        }
    }
}

QList<RowOption> providerOptions(const QList<ProviderDescriptor> &providers)
{
    QList<RowOption> options;
    options.reserve(providers.size());
    for (const ProviderDescriptor &provider : providers) {
        options.append({provider.id, provider.label});
    }
    return options;
}

SchemaCustomRow customRowFor(const QString &id, QWidget *parent, std::function<void()> notifyChanged)
{
    if (id == QStringLiteral("writingProfileBehavior")) {
        return makeWritingProfileGrid(parent, std::move(notifyChanged));
    }
    qFatal("the Qt front end has no widget for custom settings row %s", qPrintable(id));
}

} // namespace

SchemaContext qtSchemaContext(const PlatformComposition &platform,
                              const ProviderRegistry &providers,
                              const QString &primaryOutputStatus)
{
    return {
        providerOptions(providers.speechProviders()),
        providerOptions(providers.refinementProviders()),
        [&platform] {
            return settings::audioInputDeviceOptions(platform.availableAudioInputDevices());
        },
        primaryOutputStatus,
    };
}

SchemaSettingsPage::SchemaSettingsPage(const SettingsPage &page, QWidget *parent)
    : QScrollArea(parent)
{
    auto *title = settings::makePageTitle(page.title, this);
    auto *pageLayout = settings::makeSettingsPage(this);
    pageLayout->setSpacing(0);
    pageLayout->addWidget(title);
    pageLayout->addSpacing(settings::sectionGap());
    for (int index = 0; index < page.sections.size(); ++index) {
        if (index > 0) {
            pageLayout->addSpacing(settings::groupGap());
        }
        addSection(page.sections.at(index), pageLayout);
    }
    pageLayout->addStretch();
}

void SchemaSettingsPage::addSection(const SettingsSection &section, QVBoxLayout *pageLayout)
{
    pageLayout->addWidget(settings::makeSectionLabel(section.title, this));
    pageLayout->addSpacing(settings::tightSpacing());
    QWidget *card = settings::makeSettingsCard(this);
    for (int index = 0; index < section.rows.size(); ++index) {
        addRow(section.rows.at(index), card, index + 1 < section.rows.size());
    }
    pageLayout->addWidget(card);
}

void SchemaSettingsPage::addRow(const SettingsRow &descriptor, QWidget *card, bool separator)
{
    auto *form = qobject_cast<QFormLayout *>(card->layout());
    Row row;
    row.descriptor = descriptor;

    if (descriptor.kind == RowKind::Custom) {
        auto *container = new QWidget(card);
        container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto *containerLayout = new QVBoxLayout(container);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(0);
        auto *header = new QWidget(container);
        auto *headerLayout = new QVBoxLayout(header);
        headerLayout->setContentsMargins(14, 12, 14, 10);
        headerLayout->setSpacing(2);
        auto *headerTitle = new QLabel(descriptor.label, header);
        headerTitle->setObjectName(QStringLiteral("subsectionLabel"));
        auto *headerHelp = new QLabel(descriptor.help, header);
        headerHelp->setObjectName(QStringLiteral("rowDescription"));
        headerHelp->setWordWrap(true);
        headerLayout->addWidget(headerTitle);
        headerLayout->addWidget(headerHelp);
        containerLayout->addWidget(header);

        const SchemaCustomRow custom = customRowFor(descriptor.id, container, [this] {
            refreshEnabledRows();
            emit changed();
        });
        containerLayout->addWidget(custom.widget);
        form->addRow(container);
        row.frame = container;
        row.control = custom.widget;
        row.value = custom.value;
        row.setValue = custom.setValue;
        m_rows.append(row);
        applyRow(m_rows.last(), AppSettings{});
        return;
    }

    row.control = makeControl(descriptor, card, row);
    row.control->setObjectName(descriptor.id);
    if (!descriptor.tooltip.isEmpty()) {
        row.control->setToolTip(descriptor.tooltip);
    }
    QFrame *frame = settings::makeRow(descriptor.label, descriptor.help, row.control, card);
    settings::addRow(form, frame, card, separator);
    row.frame = frame;
    m_rows.append(row);
    if (descriptor.id == QStringLiteral("audioDevice")) {
        auto *mediaDevices = new QMediaDevices(this);
        connect(mediaDevices, &QMediaDevices::audioInputsChanged, this, [this] {
            if (!m_expensiveRowsLoaded) {
                return;
            }
            for (Row &candidate : m_rows) {
                if (candidate.descriptor.id != QStringLiteral("audioDevice")) {
                    continue;
                }
                AppSettings current = m_loaded;
                candidate.descriptor.apply(current, candidate.value());
                const QSignalBlocker blocker(candidate.control);
                applyRow(candidate, current);
                return;
            }
        });
    }
    if (!descriptor.expensive) {
        applyRow(m_rows.last(), AppSettings{});
    }
}

QWidget *SchemaSettingsPage::makeControl(const SettingsRow &descriptor, QWidget *card, Row &row)
{
    const auto announce = [this] {
        refreshEnabledRows();
        emit changed();
    };
    switch (descriptor.kind) {
    case RowKind::Choice: {
        auto *combo = new QComboBox(card);
        if (descriptor.contentWidthHint > 0) {
            combo->setMinimumContentsLength(descriptor.contentWidthHint);
        }
        if (descriptor.expensive) {
            // Its choices land after the page is on screen, so hold the width
            // the hint asked for instead of growing to fit whatever arrives.
            combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        }
        connect(combo, &QComboBox::currentIndexChanged, this, announce);
        row.value = [combo] { return combo->currentData(); };
        row.setValue = [combo](const QVariant &value) {
            settings::selectData(combo, value.toString());
        };
        return combo;
    }
    case RowKind::Toggle: {
        auto *check = new QCheckBox(card);
        connect(check, &QCheckBox::toggled, this, announce);
        row.value = [check] { return check->isChecked(); };
        row.setValue = [check](const QVariant &value) { check->setChecked(value.toBool()); };
        return check;
    }
    case RowKind::Number: {
        auto *spin = new QSpinBox(card);
        spin->setRange(descriptor.range.minimum, descriptor.range.maximum);
        spin->setSingleStep(descriptor.range.step);
        spin->setSuffix(descriptor.range.suffix);
        connect(spin, &QSpinBox::valueChanged, this, announce);
        row.value = [spin] { return spin->value(); };
        row.setValue = [spin](const QVariant &value) { spin->setValue(value.toInt()); };
        return spin;
    }
    case RowKind::Text: {
        auto *edit = new QLineEdit(card);
        connect(edit, &QLineEdit::textEdited, this, announce);
        row.value = [edit] { return edit->text(); };
        row.setValue = [edit](const QVariant &value) { edit->setText(value.toString()); };
        return edit;
    }
    case RowKind::Action: {
        auto *button = new QPushButton(descriptor.actionLabel, card);
        connect(button, &QPushButton::clicked, this, [this, id = descriptor.id] {
            emit actionTriggered(id);
        });
        return button;
    }
    case RowKind::Info: {
        auto *label = new QLabel(card);
        label->setForegroundRole(QPalette::WindowText);
        row.setValue = [label](const QVariant &value) { label->setText(value.toString()); };
        return label;
    }
    case RowKind::Custom:
        break;
    }
    qFatal("settings row %s has no control kind", qPrintable(descriptor.id));
}

void SchemaSettingsPage::applyRow(const Row &row, const AppSettings &settings)
{
    if (row.descriptor.options) {
        setOptions(qobject_cast<QComboBox *>(row.control), row.descriptor.options(settings));
    }
    if (row.descriptor.value && row.setValue) {
        row.setValue(row.descriptor.value(settings));
    }
}

void SchemaSettingsPage::load(const AppSettings &settings)
{
    m_loaded = settings;
    for (const Row &row : std::as_const(m_rows)) {
        if (!row.descriptor.expensive) {
            applyRow(row, settings);
        }
    }
    refreshEnabledRows();
}

void SchemaSettingsPage::loadExpensiveRows(const AppSettings &settings)
{
    m_loaded = settings;
    m_expensiveRowsLoaded = true;
    for (const Row &row : std::as_const(m_rows)) {
        if (row.descriptor.expensive) {
            applyRow(row, settings);
        }
    }
    refreshEnabledRows();
}

void SchemaSettingsPage::appendToDraft(AppSettings &draft) const
{
    for (const Row &row : m_rows) {
        if (!row.descriptor.apply || !row.value) {
            continue;
        }
        // An expensive row still waiting for its choices has nothing to say,
        // and must not overwrite the saved value with its empty one.
        const QVariant value = row.value();
        if (value.isValid()) {
            row.descriptor.apply(draft, value);
        }
    }
}

bool SchemaSettingsPage::hasChanges(const AppSettings &settings) const
{
    AppSettings draft = settings;
    appendToDraft(draft);
    for (const Row &row : m_rows) {
        if (row.descriptor.value && row.descriptor.value(draft) != row.descriptor.value(settings)) {
            return true;
        }
    }
    return false;
}

void SchemaSettingsPage::setCapabilities(const Capabilities &capabilities)
{
    m_capabilities = capabilities;
    refreshEnabledRows();
}

void SchemaSettingsPage::refreshEnabledRows()
{
    AppSettings draft = m_loaded;
    appendToDraft(draft);
    for (const Row &row : std::as_const(m_rows)) {
        if (!row.descriptor.enabled) {
            continue;
        }
        const bool live = row.descriptor.enabled(draft, m_capabilities);
        row.frame->setEnabled(live);
        row.control->setToolTip(live ? row.descriptor.tooltip : row.descriptor.disabledHelp);
    }
}

} // namespace speecher
