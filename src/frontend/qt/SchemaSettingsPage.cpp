#include "frontend/qt/SchemaSettingsPage.h"

#include "app/PlatformComposition.h"
#include "frontend/qt/CollectionRow.h"
#include "frontend/qt/WritingProfileGrid.h"
#include "providers/ProviderRegistry.h"
#include "ui/settings/FormCard.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMediaDevices>
#include <QPushButton>
#include <QSignalBlocker>
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

QList<RefinementProvider> refinementProviders(const QList<ProviderDescriptor> &providers)
{
    QList<RefinementProvider> refiners;
    refiners.reserve(providers.size());
    for (const ProviderDescriptor &provider : providers) {
        refiners.append({provider.id, provider.label, provider.supportsScreenshotContext});
    }
    return refiners;
}

SchemaCustomRow builtInRow(const SettingsRow &descriptor,
                           QWidget *parent,
                           std::function<void()> notifyChanged)
{
    if (descriptor.kind == RowKind::Collection) {
        return makeCollectionRow(descriptor, parent, std::move(notifyChanged));
    }
    if (descriptor.id == QStringLiteral("writingProfileBehavior")) {
        return makeWritingProfileGrid(parent, std::move(notifyChanged));
    }
    qFatal("the Qt front end has no widget for settings row %s", qPrintable(descriptor.id));
}

} // namespace

SchemaContext qtSchemaContext(const PlatformComposition &platform,
                              const ProviderRegistry &providers,
                              const QString &lastSeenVersion)
{
    return {
        providerOptions(providers.speechProviders()),
        refinementProviders(providers.refinementProviders()),
        [&platform] {
            return settings::audioInputDeviceOptions(platform.availableAudioInputDevices());
        },
#ifdef SPEECHER_WITH_YDOTOOL
        true,
#else
        false,
#endif
        QStringLiteral(SPEECHER_VERSION),
        lastSeenVersion,
    };
}

SchemaSettingsPage::SchemaSettingsPage(const SettingsPage &page,
                                       QWidget *parent,
                                       SchemaCustomRowFactory customRows)
    : QScrollArea(parent)
    , m_customRows(std::move(customRows))
{
    QVBoxLayout *pageLayout = settings::makeSettingsPage(this);
    QVBoxLayout *column = settings::makeCardColumn(pageLayout, widget());
    for (const SettingsSection &section : page.sections) {
        // A section this build has no rows for, such as a platform's start-at-
        // login switch, has no card either.
        if (!section.rows.isEmpty()) {
            addSection(section, column);
        }
    }
    pageLayout->addStretch();
}

void SchemaSettingsPage::addSection(const SettingsSection &section, QVBoxLayout *column)
{
    Section entry;
    entry.rowStart = m_rows.size();
    auto *card = new settings::SettingsCard(section.title, section.help, widget());
    settings::FormRow *gateNote = nullptr;
    QString gateGroup;
    for (const SettingsRow &descriptor : section.rows) {
        // Rows of a group share one gate, so one note above the group explains
        // it for all of them.
        if (descriptor.groupId.isEmpty() || descriptor.groupId != gateGroup) {
            gateNote = addGateNote(descriptor, card);
            gateGroup = descriptor.groupId;
        }
        addRow(descriptor, card, gateNote);
    }
    column->addWidget(card);
    entry.card = card;
    entry.rowEnd = m_rows.size();
    m_sections.append(entry);
}

SchemaCustomRow SchemaSettingsPage::supplyRow(const SettingsRow &descriptor,
                                              QWidget *host,
                                              const std::function<void()> &notifyChanged)
{
    if (m_customRows) {
        SchemaCustomRow supplied = m_customRows(descriptor, host, notifyChanged);
        if (supplied.widget) {
            return supplied;
        }
    }
    return builtInRow(descriptor, host, notifyChanged);
}

// A disabled control with a hover tooltip does not explain itself: disabled
// widgets do not always receive hover, and nothing says how to fix it. The
// note is a row of its own above the gated row (or group), with the reason as
// its title and, when the schema names one, the action that lifts the gate.
settings::FormRow *SchemaSettingsPage::addGateNote(const SettingsRow &descriptor,
                                                   settings::SettingsCard *card)
{
    if (!descriptor.enabled || descriptor.disabledHelp.isEmpty()) {
        return nullptr;
    }
    auto *note = new settings::FormRow(descriptor.disabledHelp, QString(), card->body());
    note->setObjectName(QStringLiteral("gateNote"));
    note->titleLabel()->setObjectName(QStringLiteral("gateNoteText"));
    if (!descriptor.disabledAction.isEmpty()) {
        auto *action = new QPushButton(descriptor.disabledActionLabel, note);
        action->setObjectName(QStringLiteral("gateAction"));
        connect(action, &QPushButton::clicked, this, [this, id = descriptor.disabledAction] {
            emit actionTriggered(id);
        });
        note->setControl(action);
    }
    note->hide();
    card->addRow(note);
    return note;
}

void SchemaSettingsPage::addRow(const SettingsRow &descriptor,
                                settings::SettingsCard *card,
                                settings::FormRow *gateNote)
{
    Row row;
    row.descriptor = descriptor;
    row.gateNote = gateNote;
    auto *frame = new settings::FormRow(descriptor.label, descriptor.help, card->body());
    frame->setProperty("rowId", descriptor.id);

    const auto announce = [this] {
        refreshRows();
        emit changed();
    };

    if (descriptor.kind == RowKind::Collection || descriptor.kind == RowKind::Custom) {
        const SchemaCustomRow custom = supplyRow(descriptor, frame, announce);
        const bool spans = custom.fullWidth || descriptor.kind == RowKind::Collection
            || qobject_cast<QLineEdit *>(custom.widget);
        if (custom.widget->objectName().isEmpty()) {
            custom.widget->setObjectName(descriptor.id);
        }
        custom.widget->setAccessibleName(descriptor.label);
        if (spans) {
            frame->setEditor(custom.widget);
        } else {
            frame->setControl(custom.widget);
        }
        if (custom.detail) {
            frame->setDetail(custom.detail);
        }
        row.control = custom.widget;
        row.value = custom.value;
        row.setValue = custom.setValue;
    } else {
        row.control = makeControl(descriptor, frame, row);
        row.control->setObjectName(descriptor.id);
        row.control->setAccessibleName(descriptor.label);
        if (!descriptor.tooltip.isEmpty()) {
            row.control->setToolTip(descriptor.tooltip);
        }
        if (qobject_cast<QLineEdit *>(row.control)) {
            frame->setEditor(row.control);
        } else {
            frame->setControl(row.control);
        }
    }
    row.frame = frame;
    card->addRow(frame);
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

QWidget *SchemaSettingsPage::makeControl(const SettingsRow &descriptor, QWidget *host, Row &row)
{
    const auto announce = [this] {
        refreshRows();
        emit changed();
    };
    switch (descriptor.kind) {
    case RowKind::Choice: {
        auto *combo = new QComboBox(host);
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
        auto *check = new QCheckBox(host);
        connect(check, &QCheckBox::toggled, this, announce);
        row.value = [check] { return check->isChecked(); };
        row.setValue = [check](const QVariant &value) { check->setChecked(value.toBool()); };
        return check;
    }
    case RowKind::Number: {
        auto *spin = new QSpinBox(host);
        spin->setRange(descriptor.range.minimum, descriptor.range.maximum);
        spin->setSingleStep(descriptor.range.step);
        spin->setSuffix(descriptor.range.suffix);
        connect(spin, &QSpinBox::valueChanged, this, announce);
        row.value = [spin] { return spin->value(); };
        row.setValue = [spin](const QVariant &value) { spin->setValue(value.toInt()); };
        return spin;
    }
    case RowKind::Text: {
        if (!descriptor.suggestions) {
            auto *edit = new QLineEdit(host);
            connect(edit, &QLineEdit::textEdited, this, announce);
            row.value = [edit] { return edit->text(); };
            row.setValue = [edit](const QVariant &value) { edit->setText(value.toString()); };
            return edit;
        }
        // Free text that has values worth offering is an editable combo: the
        // list is a shortcut, not the range of what the row accepts.
        auto *combo = new QComboBox(host);
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        if (descriptor.contentWidthHint > 0) {
            combo->setMinimumContentsLength(descriptor.contentWidthHint);
        }
        combo->view()->setMouseTracking(true);
        combo->lineEdit()->setClearButtonEnabled(true);
        connect(combo, &QComboBox::currentTextChanged, this, announce);
        row.value = [combo] { return settings::editableComboValue(combo); };
        row.setValue = [combo](const QVariant &value) {
            settings::selectEditableText(combo, value.toString());
        };
        return combo;
    }
    case RowKind::Action: {
        auto *button = new QPushButton(descriptor.actionLabel, host);
        connect(button, &QPushButton::clicked, this, [this, id = descriptor.id] {
            emit actionTriggered(id);
        });
        return button;
    }
    case RowKind::Info: {
        auto *label = new QLabel(host);
        label->setForegroundRole(QPalette::WindowText);
        row.setValue = [label](const QVariant &value) { label->setText(value.toString()); };
        return label;
    }
    case RowKind::Collection:
    case RowKind::Custom:
        break;
    }
    qFatal("settings row %s has no control kind", qPrintable(descriptor.id));
}

void SchemaSettingsPage::applyRow(const Row &row, const AppSettings &settings)
{
    const auto &choices = row.descriptor.options ? row.descriptor.options : row.descriptor.suggestions;
    if (choices) {
        setOptions(qobject_cast<QComboBox *>(row.control), choices(settings));
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
    refreshRows();
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
    refreshRows();
}

QStringList SchemaSettingsPage::validate() const
{
    QStringList messages;
    for (const Row &row : m_rows) {
        if (row.descriptor.collection.validate && row.value) {
            messages.append(
                row.descriptor.collection.validate(row.value().value<QList<QVariantMap>>()));
        }
    }
    return messages;
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
    refreshRows();
}

void SchemaSettingsPage::refresh()
{
    refreshRows();
}

// Everything a row can derive from the rest of the page: whether it is worth
// showing, whether it is usable, and what an Info row currently reads.
void SchemaSettingsPage::refreshRows()
{
    AppSettings draft = m_loaded;
    appendToDraft(draft);
    // Recorded rather than read back from the widgets: a row's own frame may
    // sit under a card this same pass is about to show or hide, and Qt's
    // isVisible()/isVisibleTo() would see that ancestor's stale state.
    QList<bool> shown(m_rows.size(), true);
    for (int index = 0; index < m_rows.size(); ++index) {
        const Row &row = m_rows.at(index);
        if (row.descriptor.visible) {
            shown[index] = row.descriptor.visible(draft, m_capabilities);
            row.frame->setVisible(shown[index]);
        }
        if (row.descriptor.kind == RowKind::Info && row.descriptor.value && row.setValue) {
            row.setValue(row.descriptor.value(draft));
        }
        if (row.descriptor.kind == RowKind::Action && row.descriptor.value) {
            if (auto *button = qobject_cast<QPushButton *>(row.control)) {
                button->setText(row.descriptor.value(draft).toString());
            }
        }
        if (row.descriptor.helpValue) {
            row.frame->setSubtitle(row.descriptor.helpValue(draft));
        }
        if (row.descriptor.enabled) {
            const bool live = row.descriptor.enabled(draft, m_capabilities);
            row.frame->setEnabled(live);
            row.control->setToolTip(live ? row.descriptor.tooltip : row.descriptor.disabledHelp);
            if (row.gateNote) {
                row.gateNote->setVisible(!live && shown[index]);
            }
        }
    }

    // Card chrome depends on every row's visibility above, so update it after
    // all row predicates have settled.
    for (const Section &section : std::as_const(m_sections)) {
        bool anyRowVisible = false;
        for (int index = section.rowStart; index < section.rowEnd; ++index) {
            if (shown[index]) {
                anyRowVisible = true;
                break;
            }
        }
        section.card->setVisible(anyRowVisible);
        section.card->updateSeparators();
    }
}

} // namespace speecher
