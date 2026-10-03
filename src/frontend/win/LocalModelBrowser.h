#pragma once

#include "app/LocalSetup.h"
#include "frontend/win/SettingsPage.h"

#include <QObject>

#include <memory>

namespace speecher {

namespace win {

// A model's rating as a badge: Recommended on the accent, Not recommended on
// the critical fill, the rest on the neutral one, each in the host window's
// theme. Shared with the setup assistant's comparison.
winrt::Microsoft::UI::Xaml::Controls::Grid ratingBadge(ModelRating rating, const PaneHost &host);

// The Local models page's list and detail ("localModelBrowser"), as the Qt
// LocalModelRows draws it: the catalog in a ListView, the selected model's
// facts and buttons beside it. Its value is the model dictation uses, empty
// while another provider transcribes. One lives on the PaneHost, so the
// selection survives the pane rebuild every LocalSetup change causes; a
// download's progress updates the detail in place instead.
class LocalModelBrowser : public std::enable_shared_from_this<LocalModelBrowser> {
public:
    explicit LocalModelBrowser(PaneHost &host);

    // Rebuilt on every call, like CollectionEditor::card: a XAML element
    // cannot reliably leave a discarded tree.
    winrt::Microsoft::UI::Xaml::UIElement element(const RowSnapshot &row);

private:
    winrt::Microsoft::UI::Xaml::UIElement listItem(const LocalModel &model);
    winrt::Microsoft::UI::Xaml::Controls::StackPanel makeDetail();
    winrt::Microsoft::UI::Xaml::Controls::Button addButton(const wchar_t *text);
    void confirmDelete();
    const LocalModel &selected() const;
    // What LocalSetup reports about this model against the settings on screen.
    LocalSetup::ModelState state(const LocalModel &model) const;
    void pick(int index);
    void showDetail();

    PaneHost &m_host;
    LocalSetup &m_setup;
    QString m_rowId;
    QString m_inUse;
    QString m_selectedId;
    // Set once the person moves the selection, by mouse or keyboard; until
    // then it follows the model in use, else the suggestion.
    bool m_userPicked = false;
    QObject m_lifetime;

    winrt::Microsoft::UI::Xaml::Controls::StackPanel m_actions{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_name{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Border m_rating{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_subtitle{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_bestFor{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_size{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_speed{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_wer{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_textShows{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_language{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_licence{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_prosCons{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::InfoBar m_problem{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock m_state{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ProgressBar m_progress{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button m_download{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button m_cancel{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button m_use{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button m_test{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button m_delete{nullptr};
};

} // namespace win
} // namespace speecher
