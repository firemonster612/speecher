#include "frontend/qt/SchemaSettingsPage.h"

#include "app/PlatformComposition.h"
#include "frontend/qt/CollectionRow.h"
#include "frontend/qt/WritingProfileGrid.h"
#include "providers/ProviderRegistry.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
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

SettingsSection mergedSection(const QList<SettingsSection> &sections)
{
    SettingsSection merged;
    for (const SettingsSection &section : sections) {
        merged.rows.append(section.rows);
        if (!section.help.isEmpty()) {
            if (!merged.help.isEmpty()) {
                merged.help += QLatin1Char('\n');
            }
            merged.help += section.help;
        }
    }
    return merged;
}

SettingsRow takeRow(QList<SettingsRow> &rows, const QString &id)
{
    for (int index = 0; index < rows.size(); ++index) {
        if (rows.at(index).id == id) {
            return rows.takeAt(index);
        }
    }
    qFatal("Qt settings layout cannot find row %s", qPrintable(id));
}

int rowIndex(const QList<SettingsRow> &rows, const QString &id)
{
    for (int index = 0; index < rows.size(); ++index) {
        if (rows.at(index).id == id) {
            return index;
        }
    }
    return -1;
}

SettingsRow takeRow(QList<SettingsSection> &sections, const QString &id)
{
    for (SettingsSection &section : sections) {
        const int index = rowIndex(section.rows, id);
        if (index >= 0) {
            return section.rows.takeAt(index);
        }
    }
    qFatal("Qt settings layout cannot find row %s", qPrintable(id));
}

int sectionWithRow(const QList<SettingsSection> &sections, const QString &id)
{
    for (int index = 0; index < sections.size(); ++index) {
        if (rowIndex(sections.at(index).rows, id) >= 0) {
            return index;
        }
    }
    return -1;
}

struct QtPageLayout {
    SettingsPage page;
    QString centeredSeparatorAfterRow;
};

// The schema groups rows for native macOS forms. Keep the established compact
// KDE order when the same descriptors are rendered by Qt.
QtPageLayout qtPageLayout(SettingsPage page)
{
    if (page.id == QStringLiteral("audio")) {
        // One compact card for the everyday rows; Advanced keeps its own card
        // and title so the timing controls read as optional.
        QList<SettingsSection> everyday;
        QList<SettingsSection> advanced;
        for (SettingsSection &section : page.sections) {
            (section.title == QStringLiteral("Advanced") ? advanced : everyday).append(section);
        }
        SettingsSection everydayCard = mergedSection(everyday);
        everydayCard.title = QStringLiteral("Speech to text");
        page.sections = {std::move(everydayCard)};
        page.sections.append(advanced);
        return {std::move(page), {}};
    }

    if (page.id == QStringLiteral("output")) {
        SettingsSection clipboardAndKeyboard{
            QStringLiteral("Clipboard"), QString(),
            {takeRow(page.sections, QStringLiteral("restoreClipboardAfterTyping"))}};
        const int virtualKeyboardSection =
            sectionWithRow(page.sections, QStringLiteral("virtualKeyboard"));
        if (virtualKeyboardSection >= 0) {
            clipboardAndKeyboard.title = QStringLiteral("Clipboard & virtual keyboard");
            clipboardAndKeyboard.rows.append(
                takeRow(page.sections[virtualKeyboardSection].rows, QStringLiteral("virtualKeyboard")));
            if (page.sections[virtualKeyboardSection].rows.isEmpty()) {
                page.sections.removeAt(virtualKeyboardSection);
            }
        }
        // The app recognition rules stay last: they are the reference table
        // the paste rules above point at.
        int recognition = -1;
        for (int index = 0; index < page.sections.size(); ++index) {
            if (page.sections.at(index).title == QStringLiteral("Application recognition")) {
                recognition = index;
            }
        }
        page.sections.insert(recognition >= 0 ? recognition : page.sections.size(),
                             std::move(clipboardAndKeyboard));
        return {std::move(page), {}};
    }

    if (page.id == QStringLiteral("refinement")) {
        SettingsSection refinement = mergedSection(page.sections);
        const SettingsRow profile =
            takeRow(refinement.rows, QStringLiteral("writingProfileBehavior"));
        const int targetContext = rowIndex(refinement.rows, QStringLiteral("targetContextControl"));
        if (targetContext < 0) {
            qFatal("Qt settings layout cannot find row targetContextControl");
        }
        refinement.rows.insert(targetContext, profile);
        refinement.title = QStringLiteral("Refinement");
        page.sections = {std::move(refinement)};
        return {std::move(page), QStringLiteral("writingProfileBehavior")};
    }
    return {std::move(page), {}};
}

// A run of rows that render together inside the card, so one capability can
// gate the whole cluster.
QWidget *addRowGroup(const QString &id, QWidget *form)
{
    auto *group = new QWidget(form);
    group->setObjectName(id);
    auto *layout = new QFormLayout(group);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setVerticalSpacing(0);
    settings::configureFormLayout(layout);
    settings::addCardRow(qobject_cast<QFormLayout *>(form->layout()), group, form);
    return group;
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
    const QtPageLayout layout = qtPageLayout(page);
    auto *title = settings::makePageTitle(layout.page.title, this);
    auto *pageLayout = settings::makeSettingsPage(this);
    pageLayout->setSpacing(0);
    pageLayout->addWidget(title);
    pageLayout->addSpacing(settings::sectionGap());
    for (int index = 0; index < layout.page.sections.size(); ++index) {
        if (index > 0) {
            pageLayout->addSpacing(settings::groupGap());
        }
        addSection(layout.page.sections.at(index), layout.centeredSeparatorAfterRow, pageLayout);
    }
    pageLayout->addStretch();
}

void SchemaSettingsPage::addSection(const SettingsSection &section,
                                    const QString &centeredSeparatorAfterRow,
                                    QVBoxLayout *pageLayout)
{
    Section entry;
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
    QWidget *group = nullptr;
    QWidget *groupNote = nullptr;
    for (int index = 0; index < section.rows.size(); ++index) {
        const SettingsRow &descriptor = section.rows.at(index);
        if (descriptor.groupId.isEmpty()) {
            group = nullptr;
            groupNote = nullptr;
        } else if (!group || group->objectName() != descriptor.groupId) {
            // Rows of a group share one gate, so one note above the group
            // explains it for all of them.
            groupNote = addGateNote(descriptor, form);
            group = addRowGroup(descriptor.groupId, form);
        }
        QWidget *host = group ? group : form;
        addRow(descriptor, host, group, group ? groupNote : addGateNote(descriptor, form));
        if (descriptor.id == centeredSeparatorAfterRow) {
            Row &row = m_rows.last();
            row.separator = settings::makeCenteredSeparator(host);
            qobject_cast<QFormLayout *>(host->layout())->addRow(row.separator);
        }
    }
    // The card's title already names its leading block, so that block's own
    // heading stays hidden (a custom block's whole header, a collection's title).
    if (leadingBlock && !title.isEmpty() && section.rows.first().label == title
        && entry.rowStart < m_rows.size()) {
        QWidget *block = m_rows.at(entry.rowStart).frame;
        if (auto *header = block->findChild<QWidget *>(QStringLiteral("blockHeader"))) {
            header->hide();
        } else if (auto *heading = block->findChild<QLabel *>(QStringLiteral("subsectionLabel"))) {
            heading->hide();
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
// note sits above the gated row (or group) with the explanation and, when the
// schema names one, the action that lifts the gate.
QWidget *SchemaSettingsPage::addGateNote(const SettingsRow &descriptor, QWidget *form)
{
    if (!descriptor.enabled || descriptor.disabledHelp.isEmpty()) {
        return nullptr;
    }
    auto *note = new QWidget(form);
    note->setObjectName(QStringLiteral("gateNote"));
    auto *layout = new QHBoxLayout(note);
    // Same inset as a card row, so the note lines up with the rows it gates.
    layout->setContentsMargins(settings::rowPadding());
    layout->setSpacing(settings::relatedSpacing());
    auto *text = new QLabel(descriptor.disabledHelp, note);
    text->setObjectName(QStringLiteral("gateNoteText"));
    text->setWordWrap(true);
    layout->addWidget(text, 1);
    if (!descriptor.disabledAction.isEmpty()) {
        auto *action = new QPushButton(descriptor.disabledActionLabel, note);
        action->setObjectName(QStringLiteral("gateAction"));
        connect(action, &QPushButton::clicked, this, [this, id = descriptor.disabledAction] {
            emit actionTriggered(id);
        });
        layout->addWidget(action, 0, Qt::AlignTop);
    }
    note->hide();
    qobject_cast<QFormLayout *>(form->layout())->addRow(note);
    return note;
}

void SchemaSettingsPage::addRow(const SettingsRow &descriptor,
                                QWidget *host,
                                QWidget *group,
                                QWidget *gateNote)
{
    auto *form = qobject_cast<QFormLayout *>(host->layout());
    Row row;
    row.descriptor = descriptor;
    row.group = group;
    row.gateNote = gateNote;

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
        m_rows.append(row);
        applyRow(m_rows.last(), AppSettings{});
        return;
    }

    if (descriptor.kind == RowKind::Custom) {
        const SchemaCustomRow custom = supplyRow(descriptor, host, announce);
        if (!custom.fullWidth) {
            custom.widget->setObjectName(descriptor.id);
            QFrame *frame = settings::makeRow(descriptor.label,
                                              descriptor.help,
                                              custom.widget,
                                              host,
                                              custom.titleAccessory,
                                              bool(descriptor.helpValue));
            settings::addRow(form, frame, host, false);
            row.frame = frame;
            row.control = custom.widget;
            row.description = frame->findChild<QLabel *>(QStringLiteral("rowDescription"));
            row.value = custom.value;
            row.setValue = custom.setValue;
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
        auto *header = new QWidget(container);
        header->setObjectName(QStringLiteral("blockHeader"));
        auto *headerLayout = new QVBoxLayout(header);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(settings::tightSpacing());
        auto *headerTitle = new QLabel(descriptor.label, header);
        headerTitle->setObjectName(QStringLiteral("subsectionLabel"));
        auto *headerHelp = new QLabel(descriptor.help, header);
        headerHelp->setObjectName(QStringLiteral("rowDescription"));
        headerHelp->setWordWrap(true);
        headerLayout->addWidget(headerTitle);
        headerLayout->addWidget(headerHelp);
        containerLayout->addWidget(header);

        containerLayout->addWidget(custom.widget);
        settings::addCardRow(form, container, host);
        row.frame = container;
        row.control = custom.widget;
        row.description = headerHelp;
        row.value = custom.value;
        row.setValue = custom.setValue;
        m_rows.append(row);
        applyRow(m_rows.last(), AppSettings{});
        return;
    }

    if (descriptor.kind == RowKind::Action) {
        // FormButtonDelegate: the row itself is the button.
        QPushButton *button = settings::makeButtonRow(
            descriptor.actionLabel.isEmpty() ? descriptor.label : descriptor.actionLabel,
            descriptor.help,
            host);
        button->setObjectName(descriptor.id);
        if (!descriptor.tooltip.isEmpty()) {
            button->setToolTip(descriptor.tooltip);
        }
        connect(button, &QPushButton::clicked, this, [this, id = descriptor.id] {
            emit actionTriggered(id);
        });
        settings::addCardRow(form, button, host);
        row.frame = button;
        row.control = button;
        row.description = button->findChild<QLabel *>(QStringLiteral("rowDescription"));
        m_rows.append(row);
        if (!descriptor.expensive) {
            applyRow(m_rows.last(), AppSettings{});
        }
        return;
    }
    row.control = makeControl(descriptor, host, row);
    row.control->setObjectName(descriptor.id);
    if (!descriptor.tooltip.isEmpty()) {
        row.control->setToolTip(descriptor.tooltip);
    }
    QFrame *frame = settings::makeRow(descriptor.label,
                                      descriptor.help,
                                      row.control,
                                      host,
                                      nullptr,
                                      bool(descriptor.helpValue));
    settings::addRow(form, frame, host, false);
    row.frame = frame;
    row.description = frame->findChild<QLabel *>(QStringLiteral("rowDescription"));
    if (!row.description) {
        row.description = frame->findChild<QLabel *>(QStringLiteral("checkBoxCaption"));
    }
    if (!row.description) {
        row.description = qobject_cast<QCheckBox *>(row.control);
    }
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
        if (!descriptor.suggestions) {
            auto *edit = new QLineEdit(card);
            connect(edit, &QLineEdit::textEdited, this, announce);
            row.value = [edit] { return edit->text(); };
            row.setValue = [edit](const QVariant &value) { edit->setText(value.toString()); };
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
        connect(combo, &QComboBox::currentTextChanged, this, announce);
        row.value = [combo] { return settings::editableComboValue(combo); };
        row.setValue = [combo](const QVariant &value) {
            settings::selectEditableText(combo, value.toString());
        };
        return combo;
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
        if (row.descriptor.helpValue && row.description) {
            const QString description = row.descriptor.helpValue(draft);
            if (auto *label = qobject_cast<QLabel *>(row.description)) {
                label->setText(description);
            } else if (auto *checkBox = qobject_cast<QCheckBox *>(row.description)) {
                checkBox->setText(description);
            }
        }
        if (row.descriptor.enabled) {
            const bool live = row.descriptor.enabled(draft, m_capabilities);
            QWidget *gated = row.group ? row.group : row.frame;
            QWidget *hinted = row.group ? row.group : row.control;
            gated->setEnabled(live);
            hinted->setToolTip(live ? row.descriptor.tooltip : row.descriptor.disabledHelp);
            if (row.gateNote) {
                row.gateNote->setVisible(!live && shown[index]);
            }
        }
        if (row.separator) {
            row.separator->setVisible(shown[index]);
        }
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
        if (section.label) {
            section.label->setVisible(anyRowVisible);
        }
        if (section.note) {
            section.note->setVisible(anyRowVisible);
        }
    }
}

} // namespace speecher
