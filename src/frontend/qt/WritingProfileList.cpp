#include "frontend/qt/WritingProfileList.h"

#include "frontend/qt/CollectionRow.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QFormLayout>
#include <QLabel>
#include <QPushButton>

namespace speecher {

namespace {

class WritingProfileList final : public QWidget {
public:
    WritingProfileList(const CollectionDescriptor &grid, QWidget *parent, std::function<void()> notifyChanged)
        : QWidget(parent)
        , m_grid(grid)
        , m_form(new QFormLayout(this))
        , m_notifyChanged(std::move(notifyChanged))
    {
        m_form->setContentsMargins(0, 0, 0, 0);
        m_form->setVerticalSpacing(0);
        settings::configureFormLayout(m_form);
    }

    QList<WritingProfileSettings> profiles() const { return m_draft.refinement.writingProfiles; }

    void setProfiles(const QList<WritingProfileSettings> &profiles)
    {
        m_draft.refinement.writingProfiles = profiles;
        showProfiles();
    }

    // The draft the page last derived, which the descriptions and the
    // dialog's choices depend on: the rules, the fallback, the custom levels
    // and tones.
    void refresh(const AppSettings &draft)
    {
        const QList<WritingProfileSettings> profiles = m_draft.refinement.writingProfiles;
        m_draft = draft;
        m_draft.refinement.writingProfiles = profiles;
        showProfiles();
    }

private:
    // Rebuilt only when the profiles themselves change, so a row keeps its
    // focus while the page reloads around it.
    void showProfiles()
    {
        const QList<RowOption> choices = writingProfileChoices(m_draft.refinement.writingProfiles);
        if (choices.size() != m_rows.size()
            || !std::equal(choices.cbegin(), choices.cend(), m_shown.cbegin(),
                           [](const RowOption &left, const RowOption &right) {
                               return left.id == right.id && left.label == right.label;
                           })) {
            rebuild(choices);
        }
        for (qsizetype index = 0; index < m_rows.size(); ++index) {
            const QString summary = m_grid.recordSummary(m_draft, choices.at(index).id);
            m_rows.at(index)->findChild<QLabel *>(QStringLiteral("rowDescription"))->setText(summary);
            m_rows.at(index)->setAccessibleDescription(summary);
        }
    }

    void rebuild(const QList<RowOption> &choices)
    {
        while (m_form->rowCount() > 0) {
            m_form->removeRow(0);
        }
        m_rows.clear();
        m_shown = choices;
        for (qsizetype index = 0; index < choices.size(); ++index) {
            QPushButton *row = settings::makeButtonRow(choices.at(index).label, QString(), this, true);
            row->setObjectName(QStringLiteral("writingProfile_") + choices.at(index).id);
            row->findChild<QLabel *>(QStringLiteral("rowDescription"))->show();
            connect(row, &QPushButton::clicked, this, [this, index] { edit(index); });
            settings::addCardRow(m_form, row, this);
            m_rows.append(row);
        }
        QPushButton *add = settings::makeButtonRow(m_grid.addLabel, QString(), this);
        add->setObjectName(QStringLiteral("addWritingProfile"));
        connect(add, &QPushButton::clicked, this, [this] { edit(-1); });
        settings::addCardRow(m_form, add, this);
    }

    // A negative index adds a profile.
    void edit(qsizetype index)
    {
        const QList<QVariantMap> records = m_grid.records(m_draft);
        const QString id = index < 0 ? QString() : records.at(index).value(m_grid.identityColumn).toString();
        const bool custom = !isBuiltInWritingProfile(id);
        CollectionDescriptor collection = m_grid;
        if (custom) {
            // A profile the person added is theirs to name, and starts unnamed.
            collection.columns.first().kind = ColumnKind::Text;
            collection.columns.first().title = QStringLiteral("Name");
            collection.blankRecord.insert(collection.columns.first().id, QString());
        }
        const auto apply = [this](const QList<QVariantMap> &edited) {
            AppSettings applied = m_draft;
            m_grid.apply(applied, edited);
            setProfiles(applied.refinement.writingProfiles);
            m_notifyChanged();
        };
        std::function<void()> remove;
        if (index >= 0 && custom) {
            remove = [apply, records, index] {
                QList<QVariantMap> edited = records;
                edited.removeAt(index);
                apply(edited);
            };
        }
        openRecordDialog(this, collection, m_draft, index, [this] { return m_grid.records(m_draft); }, apply,
                         remove, remove ? writingProfileDeletionNotice(m_draft, id) : QString());
    }

    CollectionDescriptor m_grid;
    QFormLayout *m_form;
    // The page's draft with this row's own profiles in it.
    AppSettings m_draft;
    QList<RowOption> m_shown;
    QList<QPushButton *> m_rows;
    std::function<void()> m_notifyChanged;
};

} // namespace

SchemaCustomRow makeWritingProfileList(const CollectionDescriptor &grid,
                                       QWidget *parent,
                                       std::function<void()> notifyChanged)
{
    auto *list = new WritingProfileList(grid, parent, std::move(notifyChanged));
    SchemaCustomRow row;
    row.widget = list;
    row.value = [list] { return QVariant::fromValue(list->profiles()); };
    row.setValue = [list](const QVariant &value) {
        list->setProfiles(value.value<QList<WritingProfileSettings>>());
    };
    row.refresh = [list](const AppSettings &settings) { list->refresh(settings); };
    row.cardRows = true;
    return row;
}

} // namespace speecher
