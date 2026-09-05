#pragma once

#include "frontend/win/SettingsPage.h"

#include <memory>

namespace speecher::win {

// The collection editors — application rules, paste rules, vocabulary, learned
// corrections, replacements — are one ListView driven by the descriptor behind
// whichever row asked for it. The behaviours are the mac editor's: locked
// leading records, hidden keys preserved through edits, add through a dialog so
// the record is checked before it exists, multi-select delete with undo, the
// two named undo actions, import through the descriptor's parser, and
// validation problems shown in place. Every edit applies immediately.
class CollectionEditor : public std::enable_shared_from_this<CollectionEditor> {
public:
    CollectionEditor(const RowSnapshot &row, PaneHost &host);

    // The editor as one SettingsCard-shaped element: toolbar, header row, the
    // list, and the validation InfoBar. Built once; reparented on pane rebuilds
    // so the undo history survives them.
    winrt::Microsoft::UI::Xaml::UIElement card();

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
    // Saves the editable records, showing whatever the validator refused.
    void save();
    void showProblems(const QStringList &problems);
    winrt::Microsoft::UI::Xaml::UIElement cellFor(const CollectionColumnSnapshot &column,
                                                  int recordIndex);
    void openAddDialog();
    winrt::fire_and_forget importFromFile();
    void removeSelected();
    void runAction(const QString &actionId);
    QList<int> selectedIndexes() const;

    QString m_rowId;
    QString m_rowLabel;
    CollectionSnapshot m_collection;
    PaneHost &m_host;
    QList<Record> m_records;
    // What Delete took, newest last, so undo can put it back.
    QList<Record> m_deleted;

    winrt::Microsoft::UI::Xaml::Controls::Border m_card{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ListView m_list{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::InfoBar m_problems{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button m_deleteButton{nullptr};
    QList<QPair<QString, winrt::Microsoft::UI::Xaml::Controls::Button>> m_actionButtons;
};

} // namespace speecher::win
