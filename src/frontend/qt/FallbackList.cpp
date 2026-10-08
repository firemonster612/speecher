#include "frontend/qt/FallbackList.h"

#include "app/LocalSetup.h"
#include "providers/ProviderRegistry.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace speecher {

namespace {

// Whether two presentations need the same rows, so a status or an end
// button changing leaves the rows, and the focus in them, where they are.
bool sameRows(const FallbackListPresentation &left, const FallbackListPresentation &right)
{
    const auto sameIds = [](const auto &a, const auto &b, auto id) {
        return std::equal(a.cbegin(), a.cend(), b.cbegin(), b.cend(),
                          [id](const auto &x, const auto &y) { return id(x) == id(y); });
    };
    return left.canAdd == right.canAdd
        && sameIds(left.items, right.items, [](const FallbackItem &item) { return item.providerId; })
        && sameIds(left.addChoices, right.addChoices,
                   [](const RowOption &choice) { return choice.id + QLatin1Char('\n') + choice.label; });
}

// A Negative status reads in the colour scheme's negative text, a Normal one
// as any row's description.
void showStatus(QLabel *label, const FallbackItem &item, const QPalette &palette)
{
    label->setText(item.status);
    label->setVisible(!item.status.isEmpty());
    settings::setDescriptionTone(label, item.tone == StatusTone::Negative, palette);
}

QToolButton *toolButton(const QString &caption, const QString &iconName, QStyle::StandardPixmap standard,
                        const QString &objectName, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setIcon(QIcon::fromTheme(iconName, parent->style()->standardIcon(standard)));
    button->setToolTip(caption);
    button->setAccessibleName(caption);
    button->setAutoRaise(true);
    return button;
}

QStringList &fallbacksOf(AppSettings &settings, ProviderRole role)
{
    return role == ProviderRole::Speech ? settings.speech.fallbackProviderIds
                                        : settings.refinement.fallbackProviderIds;
}

} // namespace

FallbackList::FallbackList(QWidget *parent)
    : QWidget(parent)
    , m_form(new QFormLayout(this))
{
    m_form->setContentsMargins(0, 0, 0, 0);
    m_form->setVerticalSpacing(0);
    settings::configureFormLayout(m_form);
}

void FallbackList::setPresentation(const FallbackListPresentation &list)
{
    const bool rebuilt = !sameRows(m_list, list) || m_form->rowCount() == 0;
    m_list = list;
    if (rebuilt) {
        rebuild();
        return;
    }
    showStatuses();
    for (const FallbackItem &item : std::as_const(m_list.items)) {
        auto *row = findChild<QWidget *>(QStringLiteral("fallback_") + item.providerId);
        row->findChild<QToolButton *>(QStringLiteral("fallbackMoveUp_") + item.providerId)->setEnabled(item.canMoveUp);
        row->findChild<QToolButton *>(QStringLiteral("fallbackMoveDown_") + item.providerId)
            ->setEnabled(item.canMoveDown);
    }
}

void FallbackList::showStatuses()
{
    for (const FallbackItem &item : std::as_const(m_list.items)) {
        auto *row = findChild<QWidget *>(QStringLiteral("fallback_") + item.providerId);
        showStatus(row->findChild<QLabel *>(QStringLiteral("rowDescription")), item, palette());
    }
}

// A Negative status holds a copy of the scheme's colour, which a light or
// dark switch would leave behind.
void FallbackList::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) {
        showStatuses();
    }
}

// The rows are rebuilt when a fallback is added, removed or moved. The
// control that had the focus gets it back in its new place, so a keyboard
// can move one fallback several steps.
void FallbackList::rebuild()
{
    QWidget *focused = focusWidget();
    const QString focusedName = focused && isAncestorOf(focused) ? focused->objectName() : QString();
    while (m_form->rowCount() > 0) {
        m_form->removeRow(0);
    }
    // Queued, because each edit rebuilds the rows and so deletes the button
    // that asked for it.
    for (qsizetype index = 0; index < m_list.items.size(); ++index) {
        const FallbackItem &item = m_list.items.at(index);
        auto *tools = new QWidget(this);
        auto *toolsLayout = new QHBoxLayout(tools);
        toolsLayout->setContentsMargins(0, 0, 0, 0);
        toolsLayout->setSpacing(0);
        QToolButton *up = toolButton(m_list.moveUpCaption, QStringLiteral("go-up"), QStyle::SP_ArrowUp,
                                     QStringLiteral("fallbackMoveUp_") + item.providerId, tools);
        up->setEnabled(item.canMoveUp);
        connect(up, &QToolButton::clicked, this, [this, index] { emit moveRequested(int(index), -1); },
                Qt::QueuedConnection);
        QToolButton *down = toolButton(m_list.moveDownCaption, QStringLiteral("go-down"), QStyle::SP_ArrowDown,
                                       QStringLiteral("fallbackMoveDown_") + item.providerId, tools);
        down->setEnabled(item.canMoveDown);
        connect(down, &QToolButton::clicked, this, [this, index] { emit moveRequested(int(index), 1); },
                Qt::QueuedConnection);
        QToolButton *remove = toolButton(m_list.removeCaption, QStringLiteral("list-remove"),
                                         QStyle::SP_DialogDiscardButton,
                                         QStringLiteral("fallbackRemove_") + item.providerId, tools);
        connect(remove, &QToolButton::clicked, this, [this, index] { emit removeRequested(int(index)); },
                Qt::QueuedConnection);
        for (QToolButton *button : {up, down, remove}) {
            toolsLayout->addWidget(button);
        }
        QFrame *row = settings::makeRow(item.label, item.status, tools, this, nullptr, true);
        row->setObjectName(QStringLiteral("fallback_") + item.providerId);
        settings::addCardRow(m_form, row, this);
    }
    if (m_list.canAdd) {
        auto *add = new QComboBox(this);
        add->setObjectName(QStringLiteral("fallbackAdd"));
        add->addItem(m_list.addPlaceholder);
        for (const RowOption &choice : std::as_const(m_list.addChoices)) {
            add->addItem(choice.label, choice.id);
            settings::setComboItemEnabled(add, add->count() - 1, choice.enabled,
                                          choice.enabled ? QString() : choice.help);
        }
        // The id is read now, while the combo exists; only the request waits.
        connect(add, &QComboBox::activated, this, [this, add](int index) {
            if (index <= 0) {
                return;
            }
            const QString providerId = add->itemData(index).toString();
            QMetaObject::invokeMethod(this, [this, providerId] { emit addRequested(providerId); },
                                      Qt::QueuedConnection);
        });
        settings::addCardRow(m_form, settings::makeRow(m_list.addLabel, m_list.addHelp, add, this), this);
    }
    showStatuses();
    // Rows added to a list already on screen stay hidden until shown.
    for (QWidget *child : findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        child->show();
    }
    restoreFocus(focusedName);
}

// A move that reaches an end disables its button, so the focus goes to the
// row's other move button, or else its Remove.
void FallbackList::restoreFocus(const QString &name)
{
    QWidget *target = name.isEmpty() ? nullptr : findChild<QWidget *>(name);
    if (!target) {
        return;
    }
    if (!target->isEnabled()) {
        const QList<QToolButton *> buttons =
            target->parentWidget()->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly);
        const auto enabled = std::find_if(buttons.cbegin(), buttons.cend(), [target](QToolButton *button) {
            return button != target && button->isEnabled();
        });
        target = enabled == buttons.cend() ? nullptr : *enabled;
    }
    if (target) {
        target->setFocus(Qt::OtherFocusReason);
    }
}

SchemaCustomRowFactory fallbackListRows(const ProviderRegistry &registry, const LocalSetup &local)
{
    const QList<RowOption> speechProviders = registry.rowOptions(ProviderRole::Speech);
    const QList<RowOption> refinementProviders = registry.rowOptions(ProviderRole::Refinement);
    return [speechProviders, refinementProviders, &local](const SettingsRow &descriptor, QWidget *parent,
                                                          std::function<void()> notifyChanged) {
        const bool speech = descriptor.id == QStringLiteral("speechFallbackList");
        if (!speech && descriptor.id != QStringLiteral("refinementFallbackList")) {
            return SchemaCustomRow{};
        }
        const ProviderRole role = speech ? ProviderRole::Speech : ProviderRole::Refinement;
        const QList<RowOption> providers = speech ? speechProviders : refinementProviders;

        // The heading and its subtitle above the card, the footer under it,
        // as a section's title and footnote sit.
        auto *header = new QWidget(parent);
        auto *headerLayout = new QVBoxLayout(header);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(0);
        QLabel *heading = settings::makeSectionLabel(QString(), header);
        headerLayout->addWidget(heading);
        auto *subtitle = new QLabel(header);
        subtitle->setObjectName(QStringLiteral("fallbackSubtitle"));
        subtitle->setWordWrap(true);
        subtitle->setForegroundRole(QPalette::PlaceholderText);
        subtitle->setFont(settings::smallFont(subtitle->font()));
        subtitle->setContentsMargins(settings::gridUnit(), 0, settings::gridUnit(), settings::smallSpacing());
        headerLayout->addWidget(subtitle);
        auto *footer = new QLabel(parent);
        footer->setObjectName(QStringLiteral("noteText"));
        footer->setWordWrap(true);
        footer->setForegroundRole(QPalette::PlaceholderText);
        footer->setFont(settings::smallFont(footer->font()));
        footer->setContentsMargins(settings::gridUnit(), 0, settings::gridUnit(), 0);

        auto *list = new FallbackList(parent);
        // The ids the row holds, and the page's draft they were last shown in.
        struct State {
            QStringList ids;
            AppSettings draft;
        };
        auto state = std::make_shared<State>();
        const auto edit = [state, notifyChanged](const QStringList &ids) {
            state->ids = ids;
            notifyChanged();
        };
        const auto shown = [state, role] {
            AppSettings settings = state->draft;
            fallbacksOf(settings, role) = state->ids;
            return settings;
        };
        QObject::connect(list, &FallbackList::moveRequested, list, [=](int index, int offset) {
            edit(withFallbackMoved(shown(), role, index, offset));
        });
        QObject::connect(list, &FallbackList::removeRequested, list, [=](int index) {
            edit(withFallbackRemoved(shown(), role, index));
        });
        QObject::connect(list, &FallbackList::addRequested, list, [=](const QString &providerId) {
            edit(withFallbackAdded(shown(), role, providerId));
        });

        SchemaCustomRow row;
        row.widget = list;
        row.cardRows = true;
        row.header = header;
        row.footer = footer;
        row.value = [state] { return QVariant(state->ids); };
        row.setValue = [state](const QVariant &value) { state->ids = value.toStringList(); };
        row.refresh = [=, &local](const AppSettings &draft) {
            state->draft = draft;
            const FallbackListPresentation presentation =
                fallbackListPresentation(role, draft, local.liveFacts(draft), providers, FallbackSurface::Settings);
            heading->setText(presentation.heading);
            subtitle->setText(presentation.subtitle);
            footer->setText(presentation.footer);
            list->setPresentation(presentation);
        };
        return row;
    };
}

} // namespace speecher
