#pragma once

#include "frontend/win/SettingsPage.h"

#include <memory>

namespace speecher::win {

// The collection editors — application rules, paste rules, vocabulary, learned
// corrections, replacements — are one ListView driven by the descriptor behind
// whichever row asked for it. The behaviours are the mac editor's: locked
// leading records, hidden keys preserved through edits, add (and, where the
// descriptor offers it, edit) through a dialog so the record is checked before
// it is kept, multi-select delete with undo, the
// two named undo actions, import through the descriptor's parser, and
// validation problems shown in place. Every edit applies immediately.
class CollectionEditor : public std::enable_shared_from_this<CollectionEditor> {
public:
    CollectionEditor(const RowSnapshot &row, PaneHost &host);

    // The editor as one SettingsCard-shaped element: toolbar, header row, the
    // list, and the validation InfoBar. The visuals are rebuilt on every call
    // — a XAML element cannot reliably leave a discarded tree — while the
    // records, the undo history and the problems live here and survive. The
    // row's columns replace the editor's, so the options a column offers are
    // the ones the settings hold now.
    winrt::Microsoft::UI::Xaml::UIElement card(const RowSnapshot &row);

private:
    struct Record {
        QVariantMap values;
        // Built-in records: readable, and neither editable nor deletable.
        bool locked = false;
    };

    void build();
    void rebuildRows();
    void updateToolbar();
    QList<QVariantMap> editableRecords() const;
    static QList<QVariantMap> editableRecords(const QList<Record> &records);
    // Saves the editable records, showing whatever the validator refused.
    void save();
    // Titled only when an import was refused.
    void showProblems(const QStringList &problems, const QString &title = {});
    winrt::Microsoft::UI::Xaml::UIElement cellFor(const CollectionColumnSnapshot &column, int recordIndex);
    // The record dialog: a new record for -1, otherwise the record at index.
    void openRecordDialog(int recordIndex);
    // The record whose row holds element, or -1 when element is inside one of
    // the row's own controls.
    int recordAt(const winrt::Windows::Foundation::IInspectable &element) const;
    winrt::fire_and_forget importFromFile();
    void removeSelected();
    void runAction(const QString &actionId);
    QList<int> selectedIndexes() const;

    QString m_rowId;
    CollectionSnapshot m_collection;
    PaneHost &m_host;
    QList<Record> m_records;
    // The last successful submission, independent of model/scalar refreshes.
    QList<QVariantMap> m_savedRecords;
    // What Delete took and where it stood, newest last, so undo can put it
    // back in its place.
    QList<QPair<qsizetype, Record>> m_deleted;
    QStringList m_lastProblems;
    QString m_lastProblemsTitle;

    winrt::Microsoft::UI::Xaml::Controls::Border m_card{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Grid m_header{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ListView m_list{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::InfoBar m_problems{nullptr};
    // What the editor says in place of the header and list while it holds no
    // records; null when the descriptor has nothing to say.
    winrt::Microsoft::UI::Xaml::Controls::StackPanel m_empty{nullptr};
    // Accented while the editor is empty, as its one next step.
    winrt::Microsoft::UI::Xaml::Controls::Button m_addButton{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button m_editButton{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button m_deleteButton{nullptr};
    QList<QPair<QString, winrt::Microsoft::UI::Xaml::Controls::Button>> m_actionButtons;
};

} // namespace speecher::win
