#include "frontend/qt/SchemaSettingsPage.h"

#include "app/PlatformComposition.h"
#include "frontend/qt/CollectionRow.h"
#include "frontend/qt/WritingProfileList.h"
#include "providers/ProviderRegistry.h"
#include "providers/TranscriptRefinementPrompt.h"
#include "ui/InlineMessage.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMediaDevices>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <memory>
#include <optional>

namespace speecher {

namespace {

bool offersExactly(const QComboBox *combo, const QList<RowOption> &options)
{
    if (combo->count() != options.size()) {
        return false;
    }
    for (int index = 0; index < combo->count(); ++index) {
        if (combo->itemData(index).toString() != options.at(index).id
            || combo->itemText(index) != options.at(index).label) {
            return false;
        }
    }
    return true;
}

// Rebuilding a combo resets its selection, so leave one that already offers the
// same choices alone: the caller selects the value straight after.
void setOptions(QComboBox *combo, const QList<RowOption> &options)
{
    if (!offersExactly(combo, options)) {
        const QSignalBlocker blocker(combo);
        combo->clear();
        for (const RowOption &option : options) {
            combo->addItem(option.label, option.id);
        }
    }
    // Which choices are open can change while the choices stay the same, as
    // Ultrafast does with the OpenAI model.
    for (int index = 0; index < options.size(); ++index) {
        const RowOption &option = options.at(index);
        settings::setComboItemEnabled(combo, index, option.enabled,
                                      option.enabled ? QString() : option.help);
    }
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
        return makeWritingProfileList(descriptor.collection, parent, std::move(notifyChanged));
    }
    // The Fallbacks lists need LocalSetup's facts, so SettingsPageSet supplies
    // them (fallbackListRows); a schema page rendered whole without that
    // factory leaves them out.
    if (descriptor.id == QStringLiteral("speechFallbackList")
        || descriptor.id == QStringLiteral("refinementFallbackList")) {
        auto *placeholder = new QWidget(parent);
        placeholder->hide();
        return {placeholder};
    }
    qFatal("the Qt front end has no widget for settings row %s", qPrintable(descriptor.id));
}

// The page notice that explains a row's gate: the gate many rows share, or
// else the action that lifts it. Empty when the row explains its own gate.
QString gateNoticeKey(const SettingsRow &descriptor)
{
    return descriptor.sharedGate.isEmpty() ? descriptor.disabledAction : descriptor.sharedGate;
}

} // namespace

QList<RowOption> providerOptions(const QList<ProviderDescriptor> &providers)
{
    QList<RowOption> options;
    options.reserve(providers.size());
    for (const ProviderDescriptor &provider : providers) {
        options.append({provider.id, provider.label, provider.summary});
    }
    return options;
}

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
        {},
        {},
        builtInDictationSystemPrompt(),
    };
}

SchemaSettingsPage::SchemaSettingsPage(const QList<SettingsSection> &sections,
                                       QWidget *parent,
                                       SchemaCustomRowFactory customRows,
                                       const QString &intro)
    : QScrollArea(parent)
    , m_customRows(std::move(customRows))
{
    auto *pageLayout = settings::makeSettingsPage(this);
    pageLayout->setSpacing(0);
    if (!intro.isEmpty()) {
        // In a holder, as a section's note is in its column: a wrapping label
        // laid straight into the page keeps the height of its narrowest width.
        auto *holder = new QWidget(this);
        auto *holderLayout = new QVBoxLayout(holder);
        holderLayout->setContentsMargins(settings::gridUnit(), 0, settings::gridUnit(), settings::groupGap());
        auto *label = new QLabel(intro, holder);
        label->setObjectName(QStringLiteral("pageIntro"));
        label->setWordWrap(true);
        holderLayout->addWidget(label);
        pageLayout->addWidget(settings::centerColumn(holder, this));
    }
    for (const SettingsSection &section : sections) {
        for (const SettingsRow &row : section.rows) {
            addGateNotice(row, pageLayout);
        }
    }
    for (int index = 0; index < sections.size(); ++index) {
        addSection(sections.at(index), pageLayout, index > 0);
    }
    pageLayout->addStretch();
}

void SchemaSettingsPage::addSection(const SettingsSection &section, QVBoxLayout *pageLayout, bool spaced)
{
    Section entry;
    // The gap above a section goes with its card, so a hidden card leaves none.
    if (spaced) {
        entry.gap = new QWidget(this);
        entry.gap->setFixedHeight(settings::groupGap());
        pageLayout->addWidget(entry.gap);
    }
    entry.rowStart = m_rows.size();
    // A section is one column: its title, then its card of rows. The column is
    // centred and capped so every section on the page shares the same edges.
    auto *column = new QWidget(this);
    column->setObjectName(QStringLiteral("settingsSection"));
    auto *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(settings::tightSpacing());
    // An untitled card that opens with a named block is titled by that block:
    // the name goes above the card like every other section header, not inside.
    const bool leadingBlock = !section.rows.isEmpty()
        && (section.rows.first().kind == RowKind::Collection
            || section.rows.first().kind == RowKind::Custom);
    const QString title = section.title.isEmpty() && leadingBlock ? section.rows.first().label
                                                                    : section.title;
    if (!title.isEmpty()) {
        entry.label = settings::makeSectionLabel(title, column);
        columnLayout->addWidget(entry.label);
    }
    QFrame *card = settings::makeSettingsCard(column);
    columnLayout->addWidget(card);
    QWidget *form = settings::cardFormLayout(card)->parentWidget();
    QString previousGroup;
    QString previousDialog;
    QWidget *dialogForm = nullptr;
    for (const SettingsRow &descriptor : section.rows) {
        // Rows of a group share one gate, so the first of them says why.
        const bool repeatsGroup = !descriptor.groupId.isEmpty() && descriptor.groupId == previousGroup;
        previousGroup = descriptor.groupId;
        if (descriptor.dialog.title != previousDialog) {
            previousDialog = descriptor.dialog.title;
            dialogForm = previousDialog.isEmpty() ? nullptr : addDialog(descriptor.dialog, form);
        }
        addRow(descriptor, dialogForm ? dialogForm : form,
               descriptor.enabled && gateNoticeKey(descriptor).isEmpty() && !repeatsGroup);
        if (dialogForm) {
            m_rows.last().opener = m_dialogs.last().button;
        }
    }
    // The card's title already names its leading block, so that block's own
    // heading stays hidden; its description still explains it.
    if (leadingBlock && !title.isEmpty() && section.rows.first().label == title
        && entry.rowStart < m_rows.size()) {
        if (auto *heading = m_rows.at(entry.rowStart).frame->findChild<QLabel *>(QStringLiteral("subsectionLabel"))) {
            heading->hide();
        }
    }
    // A row may title and footnote its section itself.
    for (int index = entry.rowStart; index < m_rows.size(); ++index) {
        const Row &row = m_rows.at(index);
        if (row.header) {
            delete entry.label;
            row.header->setParent(column);
            columnLayout->insertWidget(0, row.header);
            entry.label = row.header;
        }
        if (row.footer) {
            row.footer->setParent(column);
            columnLayout->addWidget(row.footer);
            entry.note = row.footer;
        }
    }
    if (!section.help.isEmpty()) {
        auto *note = new QLabel(section.help, column);
        note->setObjectName(QStringLiteral("noteText"));
        note->setWordWrap(true);
        note->setForegroundRole(QPalette::PlaceholderText);
        note->setFont(settings::smallFont(note->font()));
        note->setContentsMargins(settings::gridUnit(), 0, settings::gridUnit(), 0);
        note->setAttribute(Qt::WA_StyledBackground, false);
        columnLayout->addWidget(note);
        entry.note = note;
    }
    pageLayout->addWidget(settings::centerColumn(column, this));
    entry.card = card;
    entry.rowEnd = m_rows.size();
    m_sections.append(entry);
}

QWidget *SchemaSettingsPage::addDialog(const RowDialog &dialog, QWidget *cardForm)
{
    QPushButton *opener = settings::makeButtonRow(dialog.title, QString(), cardForm, true);
    opener->setObjectName(QStringLiteral("dialogRow"));
    settings::addCardRow(qobject_cast<QFormLayout *>(cardForm->layout()), opener, cardForm);
    m_dialogs.append({opener, dialog.summary});

    auto *window = new QDialog(this);
    window->setObjectName(QStringLiteral("settingsDialog"));
    window->setWindowTitle(dialog.title);
    window->setMinimumWidth(settings::cardMaximumWidth());
    auto *layout = new QVBoxLayout(window);
    QFrame *card = settings::makeSettingsCard(window);
    layout->addWidget(card, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, window);
    connect(buttons, &QDialogButtonBox::rejected, window, &QDialog::reject);
    layout->addWidget(buttons);
    connect(opener, &QPushButton::clicked, window, &QDialog::open);
    return settings::cardFormLayout(card)->parentWidget();
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
// widgets do not always receive hover, and nothing says how to fix it. A gate
// that many rows share or an action can lift is explained once, at the top of
// the page, with any action beside it; any other gate is explained in the
// row's description.
void SchemaSettingsPage::addGateNotice(const SettingsRow &descriptor, QVBoxLayout *pageLayout)
{
    const QString key = gateNoticeKey(descriptor);
    if (key.isEmpty()) {
        return;
    }
    for (const GateNotice &notice : std::as_const(m_gateNotices)) {
        if (notice.key == key) {
            return;
        }
    }
    auto *holder = new QWidget(this);
    auto *holderLayout = new QVBoxLayout(holder);
    holderLayout->setContentsMargins(0, 0, 0, settings::groupGap());
    auto *message = new InlineMessage(holder);
    message->setObjectName(QStringLiteral("gateNote"));
    // A closed gate stops settings from working, as on Home's notice.
    message->setType(InlineMessage::Type::Warning);
    message->label()->setObjectName(QStringLiteral("gateNoteText"));
    message->setCloseButtonVisible(false);
    if (!descriptor.disabledAction.isEmpty()) {
        auto *action = new QPushButton(descriptor.disabledActionLabel, message);
        action->setObjectName(QStringLiteral("gateAction"));
        connect(action, &QPushButton::clicked, this, [this, id = descriptor.disabledAction] {
            emit actionTriggered(id);
        });
        message->addAction(action);
    }
    holderLayout->addWidget(message);
    holder->hide();
    pageLayout->addWidget(settings::centerColumn(holder, this));
    m_gateNotices.append({key, holder, message});
}

void SchemaSettingsPage::addRow(const SettingsRow &descriptor, QWidget *host, bool explainsGate)
{
    auto *form = qobject_cast<QFormLayout *>(host->layout());
    Row row;
    row.descriptor = descriptor;
    row.explainsGate = explainsGate;
    const bool dynamicDescription = bool(descriptor.helpValue) || explainsGate;

    const auto announce = [this] {
        refreshRows();
        emit changed();
    };

    if (descriptor.kind == RowKind::Collection) {
        const SchemaCustomRow editor = supplyRow(descriptor, host, announce);
        settings::addCardRow(form, editor.widget, host);
        row.frame = editor.widget;
        row.control = editor.widget;
        row.description = editor.widget->findChild<QLabel *>(QStringLiteral("rowDescription"));
        row.value = editor.value;
        row.setValue = editor.setValue;
        row.refresh = editor.refresh;
        row.setEditable = editor.setEditable;
        m_rows.append(row);
        applyRow(m_rows.last(), AppSettings{});
        return;
    }

    if (descriptor.kind == RowKind::Custom) {
        const SchemaCustomRow custom = supplyRow(descriptor, host, announce);
        row.control = custom.widget;
        row.value = custom.value;
        row.setValue = custom.setValue;
        row.refresh = custom.refresh;
        row.setEditable = custom.setEditable;
        row.header = custom.header;
        row.footer = custom.footer;
        if (custom.cardRows) {
            settings::addCardRow(form, custom.widget, host);
            row.frame = custom.widget;
            m_rows.append(row);
            applyRow(m_rows.last(), AppSettings{});
            return;
        }
        if (!custom.fullWidth) {
            custom.widget->setObjectName(descriptor.id);
            QFrame *frame = settings::makeRow(descriptor.label,
                                              descriptor.help,
                                              custom.widget,
                                              host,
                                              custom.titleAccessory,
                                              dynamicDescription);
            if (custom.detail) {
                frame->findChild<QWidget *>(QStringLiteral("rowLabelCell"))->layout()->addWidget(custom.detail);
            }
            settings::addRow(form, frame, host, false);
            row.frame = frame;
            row.title = frame->findChild<QLabel *>(QStringLiteral("rowTitle"));
            row.description = frame->findChild<QLabel *>(QStringLiteral("rowDescription"));
            m_rows.append(row);
            applyRow(m_rows.last(), AppSettings{});
            return;
        }
        auto *container = new QWidget(host);
        container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto *containerLayout = new QVBoxLayout(container);
        // Same inset as a card row so the block's text lines up with row titles.
        containerLayout->setContentsMargins(settings::rowPadding());
        containerLayout->setSpacing(settings::relatedSpacing());
        if (!descriptor.label.isEmpty() || !descriptor.help.isEmpty() || dynamicDescription) {
            auto *header = new QWidget(container);
            header->setObjectName(QStringLiteral("blockHeader"));
            auto *headerLayout = new QVBoxLayout(header);
            headerLayout->setContentsMargins(0, 0, 0, 0);
            headerLayout->setSpacing(settings::tightSpacing());
            if (!descriptor.label.isEmpty()) {
                row.title = new QLabel(descriptor.label, header);
                row.title->setObjectName(QStringLiteral("subsectionLabel"));
                headerLayout->addWidget(row.title);
            }
            row.description = new QLabel(descriptor.help, header);
            row.description->setObjectName(QStringLiteral("rowDescription"));
            row.description->setWordWrap(true);
            row.description->setVisible(!descriptor.help.isEmpty());
            headerLayout->addWidget(row.description);
            containerLayout->addWidget(header);
        }
        containerLayout->addWidget(custom.widget);
        settings::addCardRow(form, container, host);
        row.frame = container;
        m_rows.append(row);
        applyRow(m_rows.last(), AppSettings{});
        return;
    }

    if (descriptor.kind == RowKind::Action && !descriptor.targetPage.isEmpty()) {
        // A row that opens a subpage is itself the button, with the trailing
        // arrow, and its description says what the subpage holds.
        QPushButton *button = settings::makeButtonRow(descriptor.label, descriptor.help, host, dynamicDescription);
        button->setObjectName(descriptor.id);
        connect(button, &QPushButton::clicked, this, [this, id = descriptor.id] {
            emit actionTriggered(id);
        });
        settings::addCardRow(form, button, host);
        row.frame = button;
        row.control = button;
        row.description = button->findChild<QLabel *>(QStringLiteral("rowDescription"));
        m_rows.append(row);
        applyRow(m_rows.last(), AppSettings{});
        return;
    }

    if (descriptor.kind == RowKind::Action) {
        // The label names what the row is about and the button says what a
        // click does, as on macOS and Windows.
        auto *button = new QPushButton(descriptor.actionLabel, host);
        button->setObjectName(descriptor.id);
        if (!descriptor.tooltip.isEmpty()) {
            button->setToolTip(descriptor.tooltip);
        }
        connect(button, &QPushButton::clicked, this, [this, id = descriptor.id] {
            emit actionTriggered(id);
        });
        row.control = button;
    } else {
        row.control = makeControl(descriptor, host, row);
        row.control->setObjectName(descriptor.id);
        if (!descriptor.tooltip.isEmpty()) {
            row.control->setToolTip(descriptor.tooltip);
        }
    }
    QFrame *frame = settings::makeRow(descriptor.label,
                                      descriptor.help,
                                      row.control,
                                      host,
                                      nullptr,
                                      dynamicDescription);
    settings::addRow(form, frame, host, false);
    row.frame = frame;
    row.title = frame->findChild<QLabel *>(QStringLiteral("rowTitle"));
    row.description = frame->findChild<QLabel *>(QStringLiteral("rowDescription"));
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
        refreshRows();
        emit changed();
    };
    switch (descriptor.kind) {
    case RowKind::Choice: {
        auto *combo = new QComboBox(card);
        if (descriptor.expensive) {
            // Its choices land after the page is on screen, so hold the width
            // the hint asked for instead of growing to fit whatever arrives —
            // but never more than half the card, or the reserve crushes the
            // description into wrapping while the combo sits empty. A combo
            // whose choices are already known needs no reserve at all: its
            // natural size fits every item, and hint width beyond that only
            // steals room from the description.
            if (descriptor.contentWidthHint > 0) {
                combo->setMinimumContentsLength(descriptor.contentWidthHint);
            }
            combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            combo->setMaximumWidth(settings::cardMaximumWidth() / 2);
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
        // While its gate is closed the setting does nothing, so the box shows
        // unticked; the stored value is held for saving and for reopening.
        auto held = std::make_shared<std::optional<bool>>();
        row.value = [check, held] { return held->value_or(check->isChecked()); };
        row.setValue = [check, held](const QVariant &value) {
            if (held->has_value()) {
                *held = value.toBool();
            } else {
                check->setChecked(value.toBool());
            }
        };
        row.setEditable = [check, held](bool editable) {
            check->setEnabled(editable);
            if (editable != held->has_value()) {
                return;
            }
            const QSignalBlocker blocker(check);
            if (editable) {
                check->setChecked(held->value());
                held->reset();
            } else {
                *held = check->isChecked();
                check->setChecked(false);
            }
        };
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
        if (descriptor.multiline) {
            auto *edit = new QPlainTextEdit(card);
            edit->setPlaceholderText(descriptor.placeholder);
            connect(edit, &QPlainTextEdit::textChanged, this, announce);
            row.value = [edit] { return edit->toPlainText(); };
            // Every edit reloads the page, and setPlainText would move the
            // cursor out from under the person typing.
            row.setValue = [edit](const QVariant &value) {
                if (!edit->hasFocus() && edit->toPlainText() != value.toString()) {
                    const QSignalBlocker blocker(edit);
                    edit->setPlainText(value.toString());
                }
            };
            return edit;
        }
        if (!descriptor.suggestions) {
            auto *edit = new QLineEdit(card);
            edit->setPlaceholderText(descriptor.placeholder);
            if (descriptor.secret) {
                edit->setEchoMode(QLineEdit::Password);
            }
            connect(edit, &QLineEdit::textEdited, this, announce);
            row.value = [edit] { return edit->text(); };
            // setText moves the cursor to the end even for the same text, and
            // an automatic connection check can reload the page mid-typing.
            row.setValue = [edit](const QVariant &value) {
                if (edit->text() != value.toString()) edit->setText(value.toString());
            };
            return edit;
        }
        // Free text that has values worth offering is an editable combo: the
        // list is a shortcut, not the range of what the row accepts.
        auto *combo = new QComboBox(card);
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        if (descriptor.contentWidthHint > 0) {
            combo->setMinimumContentsLength(descriptor.contentWidthHint);
        }
        combo->view()->setMouseTracking(true);
        combo->lineEdit()->setClearButtonEnabled(true);
        combo->lineEdit()->setPlaceholderText(descriptor.placeholder);
        connect(combo, &QComboBox::currentTextChanged, this, announce);
        row.value = [combo] { return settings::editableComboValue(combo); };
        row.setValue = [combo](const QVariant &value) {
            settings::selectEditableText(combo, value.toString());
        };
        return combo;
    }
    case RowKind::Info: {
        auto *label = new QLabel(card);
        label->setForegroundRole(QPalette::WindowText);
        row.setValue = [label](const QVariant &value) { label->setText(value.toString()); };
        return label;
    }
    case RowKind::Action:
    case RowKind::Collection:
    case RowKind::Custom:
        break;
    }
    qFatal("settings row %s has no control kind", qPrintable(descriptor.id));
}

void SchemaSettingsPage::applyRow(const Row &row, const AppSettings &settings)
{
    const auto &choices = row.descriptor.options ? row.descriptor.options : row.descriptor.suggestions;
    // A Custom row fills its own widget, which need not be a combo box.
    if (choices && row.descriptor.kind != RowKind::Custom) {
        setOptions(qobject_cast<QComboBox *>(row.control), choices(settings));
    }
    // First, so a row whose choices come from the settings offers them
    // before its value is chosen among them.
    if (row.refresh) {
        row.refresh(settings);
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
            if (row.descriptor.secret && row.descriptor.value
                && value == row.descriptor.value(m_loaded)) continue;
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

void SchemaSettingsPage::revealRow(const QString &rowId, bool focusControl)
{
    for (const Row &row : std::as_const(m_rows)) {
        if (row.descriptor.id != rowId) {
            continue;
        }
        // A page just brought forward lays itself out on the next pass.
        // A row kept in a dialog is found at the button row that opens it.
        QWidget *target = row.opener ? row.opener : row.frame;
        QTimer::singleShot(0, this, [this, frame = QPointer<QWidget>(target),
                                     control = QPointer<QWidget>(row.opener ? row.opener : row.control),
                                     focusControl] {
            if (frame) {
                ensureWidgetVisible(frame);
            }
            if (control && focusControl) {
                control->setFocus(Qt::OtherFocusReason);
            }
        });
        return;
    }
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
    // What each page notice says: the first closed gate it explains.
    QHash<QString, QString> noticeText;
    for (int index = 0; index < m_rows.size(); ++index) {
        const Row &row = m_rows.at(index);
        if (row.descriptor.visible) {
            shown[index] = row.descriptor.visible(draft, m_capabilities);
            settings::setCardRowVisible(row.frame, shown[index]);
        }
        if (row.descriptor.kind == RowKind::Info && row.descriptor.value && row.setValue) {
            row.setValue(row.descriptor.value(draft));
        }
        if (row.refresh) {
            row.refresh(draft);
        }
        // The caption follows what a click will do, and a value names what
        // the row is about (the Local Runner found) in place of its label.
        if (row.descriptor.kind == RowKind::Action && row.descriptor.targetPage.isEmpty()) {
            if (row.descriptor.actionLabelValue) {
                qobject_cast<QPushButton *>(row.control)->setText(row.descriptor.actionLabelValue(draft));
            }
            if (row.descriptor.value && row.title) {
                row.title->setText(row.descriptor.value(draft).toString());
                row.title->setVisible(!row.title->text().isEmpty());
            }
        }
        if (row.descriptor.labelValue && row.title) {
            row.title->setText(row.descriptor.labelValue(draft));
            row.control->setAccessibleName(row.title->text());
        }
        const bool live = !row.descriptor.enabled || row.descriptor.enabled(draft, m_capabilities);
        const QString reason = live ? QString()
            : row.descriptor.disabledHelpValue ? row.descriptor.disabledHelpValue(draft, m_capabilities)
                                               : row.descriptor.disabledHelp;
        if (row.description && (row.descriptor.helpValue || row.explainsGate)) {
            const QString description = !live && row.explainsGate ? reason
                : row.descriptor.helpValue                       ? row.descriptor.helpValue(draft)
                                                                 : row.descriptor.help;
            row.description->setText(description);
            row.description->setVisible(!description.isEmpty());
            row.control->setAccessibleDescription(description);
        }
        if (row.descriptor.enabled) {
            // The description stays enabled, so its grey is not dimmed twice.
            if (row.setEditable) {
                row.setEditable(live);
            } else {
                row.control->setEnabled(live);
                if (row.title) {
                    row.title->setEnabled(live);
                }
            }
            row.control->setToolTip(live ? row.descriptor.tooltip : reason);
            const QString noticeKey = gateNoticeKey(row.descriptor);
            if (!live && shown[index] && !noticeKey.isEmpty() && !noticeText.contains(noticeKey)) {
                noticeText.insert(noticeKey, reason);
            }
        }
    }
    for (const DialogOpener &dialog : std::as_const(m_dialogs)) {
        const QString summary = dialog.summary ? dialog.summary(draft) : QString();
        auto *description = dialog.button->findChild<QLabel *>(QStringLiteral("rowDescription"));
        description->setText(summary);
        description->setVisible(!summary.isEmpty());
        dialog.button->setAccessibleDescription(summary);
    }
    for (const GateNotice &notice : std::as_const(m_gateNotices)) {
        const QString text = noticeText.value(notice.key);
        notice.message->setText(text);
        notice.holder->setVisible(!text.isEmpty());
    }

    // Section chrome depends on every row's visibility above, so update it
    // after all row predicates have settled.
    for (const Section &section : std::as_const(m_sections)) {
        bool anyRowVisible = false;
        for (int index = section.rowStart; index < section.rowEnd; ++index) {
            if (shown[index]) {
                anyRowVisible = true;
                break;
            }
        }
        section.card->setVisible(anyRowVisible);
        for (QWidget *chrome : {section.label, section.gap}) {
            if (chrome) {
                chrome->setVisible(anyRowVisible);
            }
        }
        if (section.note) {
            section.note->setVisible(anyRowVisible && !section.note->text().isEmpty());
        }
    }
}

} // namespace speecher
