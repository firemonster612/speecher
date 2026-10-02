#include "frontend/win/LocalModelBrowser.h"

#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "core/LocalModelCatalog.h"
#include "providers/LocalModelStore.h"

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#pragma pop_macro("GetCurrentTime")

namespace speecher::win {

namespace {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using winrt::Microsoft::UI::Xaml::Automation::AutomationProperties;

QString werText(double wer)
{
    return QString::number(wer, 'f', 2) + QLatin1Char('%');
}

void setVisible(const UIElement &element, bool visible)
{
    element.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
}

} // namespace

Grid ratingBadge(ModelRating rating, const PaneHost &host)
{
    const wchar_t *key = L"RatingBadgeNeutral";
    if (rating == ModelRating::Recommended) {
        key = L"RatingBadgeAccent";
    } else if (rating == ModelRating::NotRecommended) {
        key = L"RatingBadgeCritical";
    }
    Grid pill = badge(modelRatingLabel(rating), themeBrush(key, host));
    AutomationProperties::SetName(pill, hs(modelRatingLabel(rating)));
    return pill;
}

LocalModelBrowser::LocalModelBrowser(PaneHost &host)
    : m_host(host)
    , m_setup(*host.controller->localSetup())
{
}

UIElement LocalModelBrowser::element(const RowSnapshot &row)
{
    m_rowId = row.id;
    m_inUse = row.value.toString();
    // Open on the model in use, else on the suggestion, which is only known
    // once the hardware probe has answered.
    if (!m_userPicked) {
        m_selectedId = m_inUse.isEmpty() ? m_setup.suggestedModel().id : m_inUse;
    }

    StackPanel content;
    content.Padding({16, 16, 16, 16});
    content.Spacing(16);
    content.Children().Append(
        secondaryTextBlock(m_setup.hardwareLine(), L"SettingsCardDescriptionStyle", m_host));

    Grid columns;
    columns.ColumnSpacing(24);
    ColumnDefinition listColumn;
    listColumn.Width({0, GridUnitType::Auto});
    ColumnDefinition detailColumn;
    detailColumn.Width({1, GridUnitType::Star});
    columns.ColumnDefinitions().Append(listColumn);
    columns.ColumnDefinitions().Append(detailColumn);

    ListView list;
    list.SelectionMode(ListViewSelectionMode::Single);
    list.VerticalAlignment(VerticalAlignment::Top);
    AutomationProperties::SetName(list, L"Speech models");
    int selectedIndex = -1;
    for (const LocalModel &model : localModelCatalog()) {
        if (model.id == m_selectedId) {
            selectedIndex = int(list.Items().Size());
        }
        list.Items().Append(listItem(model));
    }
    list.SelectedIndex(selectedIndex);
    // A rebuild re-selects the same model, and WinUI may report that late;
    // only a move to another model is the person's.
    list.SelectionChanged([weak = weak_from_this()](const IInspectable &sender, const auto &) {
        if (auto self = weak.lock()) {
            self->pick(sender.as<ListView>().SelectedIndex());
        }
    });
    columns.Children().Append(list);

    StackPanel detail = makeDetail();
    Grid::SetColumn(detail, 1);
    columns.Children().Append(detail);
    content.Children().Append(columns);

    // Download progress arrives many times a second; it updates the detail
    // in place rather than rebuilding the pane.
    QObject::disconnect(&m_setup.models(), nullptr, &m_lifetime, nullptr);
    QObject::connect(&m_setup.models(), &LocalModelStore::downloadProgress, &m_lifetime,
                     [weak = weak_from_this()] {
                         if (auto self = weak.lock()) {
                             self->showDetail();
                         }
                     });
    showDetail();
    return content;
}

UIElement LocalModelBrowser::listItem(const LocalModel &model)
{
    Grid item;
    item.ColumnSpacing(12);
    item.Padding({0, 6, 0, 6});
    ColumnDefinition iconColumn;
    iconColumn.Width({0, GridUnitType::Auto});
    ColumnDefinition textColumn;
    textColumn.Width({1, GridUnitType::Star});
    item.ColumnDefinitions().Append(iconColumn);
    item.ColumnDefinitions().Append(textColumn);
    FontIcon icon;
    // Segoe Fluent Icons: CheckMark once on disk, Download before.
    icon.Glyph(state(model).downloaded ? L"\uE73E" : L"\uE896");
    icon.FontSize(16);
    icon.VerticalAlignment(VerticalAlignment::Center);
    item.Children().Append(icon);
    StackPanel text;
    StackPanel title;
    title.Orientation(Orientation::Horizontal);
    title.Spacing(8);
    TextBlock name = styledTextBlock(model.name, L"SettingsCardBodyStyle");
    name.VerticalAlignment(VerticalAlignment::Center);
    title.Children().Append(name);
    title.Children().Append(ratingBadge(model.rating, m_host));
    text.Children().Append(title);
    // The size only, so the list stays as narrow as its names and badges; the
    // error rates and the fit are in the facts.
    text.Children().Append(
        secondaryTextBlock(downloadSizeText(model.sizeBytes), L"SettingsCardDescriptionStyle", m_host));
    Grid::SetColumn(text, 1);
    item.Children().Append(text);
    AutomationProperties::SetName(item, hs(QStringLiteral("%1, %2").arg(model.name, modelRatingLabel(model.rating))));
    return item;
}

StackPanel LocalModelBrowser::makeDetail()
{
    StackPanel detail;
    detail.Spacing(4);
    StackPanel title;
    title.Orientation(Orientation::Horizontal);
    title.Spacing(8);
    m_name = styledTextBlock(QString(), L"BodyStrongTextBlockStyle");
    m_name.VerticalAlignment(VerticalAlignment::Center);
    title.Children().Append(m_name);
    // The badge is rebuilt with each model, so it sits in a holder.
    m_rating = Border();
    title.Children().Append(m_rating);
    detail.Children().Append(title);
    m_subtitle = secondaryTextBlock(QString(), L"SettingsCardDescriptionStyle", m_host);
    detail.Children().Append(m_subtitle);

    Grid facts;
    facts.Margin({0, 8, 0, 8});
    facts.ColumnSpacing(16);
    facts.RowSpacing(4);
    ColumnDefinition keyColumn;
    keyColumn.Width({0, GridUnitType::Auto});
    ColumnDefinition valueColumn;
    valueColumn.Width({1, GridUnitType::Star});
    facts.ColumnDefinitions().Append(keyColumn);
    facts.ColumnDefinitions().Append(valueColumn);
    const auto addFact = [&](const QString &name) {
        const int row = int(facts.RowDefinitions().Size());
        facts.RowDefinitions().Append(RowDefinition());
        TextBlock key = secondaryTextBlock(name, L"SettingsCardBodyStyle", m_host);
        Grid::SetRow(key, row);
        facts.Children().Append(key);
        TextBlock value = styledTextBlock(QString(), L"SettingsCardBodyStyle");
        value.IsTextSelectionEnabled(true);
        Grid::SetRow(value, row);
        Grid::SetColumn(value, 1);
        facts.Children().Append(value);
        return value;
    };
    const LocalModelFactLabels names;
    m_bestFor = addFact(names.bestFor);
    m_size = addFact(names.download);
    m_speed = addFact(names.speedHere);
    m_wer = addFact(names.wordErrorRate);
    ToolTipService::SetToolTip(m_wer, box_value(hs(wordErrorRateSources())));
    m_textShows = addFact(names.textShows);
    addFact(names.language).Text(L"English");
    m_licence = addFact(names.license);
    detail.Children().Append(facts);

    m_prosCons = styledTextBlock(QString(), L"SettingsCardDescriptionStyle");
    detail.Children().Append(m_prosCons);

    m_problem = InfoBar();
    m_problem.Severity(InfoBarSeverity::Warning);
    m_problem.IsClosable(false);
    m_problem.Margin({0, 8, 0, 0});
    detail.Children().Append(m_problem);

    m_actions = StackPanel();
    m_actions.Orientation(Orientation::Horizontal);
    m_actions.Spacing(8);
    m_state = styledTextBlock(QString(), L"SettingsCardBodyStyle");
    m_state.VerticalAlignment(VerticalAlignment::Center);
    m_actions.Children().Append(m_state);
    m_progress = ProgressBar();
    m_progress.Width(160);
    m_progress.Maximum(1000);
    m_progress.VerticalAlignment(VerticalAlignment::Center);
    m_actions.Children().Append(m_progress);
    m_download = addButton(L"");
    m_cancel = addButton(L"Cancel");
    m_use = addButton(hs(localModelText(LocalModelText::UseModel)).c_str());
    m_test = addButton(hs(localModelText(LocalModelText::TestSpeed)).c_str());
    // Delete stands apart at the row's far end, away from the safe actions.
    m_delete = Button();
    m_delete.Content(box_value(hs(localModelText(LocalModelText::DeleteModel))));
    m_delete.VerticalAlignment(VerticalAlignment::Center);
    Grid actionRow;
    actionRow.ColumnSpacing(8);
    actionRow.Margin({0, 12, 0, 0});
    ColumnDefinition safeColumn;
    safeColumn.Width({1, GridUnitType::Star});
    ColumnDefinition deleteColumn;
    deleteColumn.Width({0, GridUnitType::Auto});
    actionRow.ColumnDefinitions().Append(safeColumn);
    actionRow.ColumnDefinitions().Append(deleteColumn);
    actionRow.Children().Append(m_actions);
    Grid::SetColumn(m_delete, 1);
    actionRow.Children().Append(m_delete);
    detail.Children().Append(actionRow);

    const auto on = [weak = weak_from_this()](const Button &button, auto run) {
        button.Click([weak, run](const auto &, const auto &) {
            if (auto self = weak.lock()) {
                run(*self);
            }
        });
    };
    on(m_download, [](LocalModelBrowser &self) { self.m_setup.download(self.selected()); });
    on(m_cancel, [](LocalModelBrowser &self) { self.m_setup.cancelDownload(self.selected().id); });
    on(m_use, [](LocalModelBrowser &self) {
        setValueAndCommit(self.m_host, self.m_rowId, self.selected().id);
    });
    on(m_test, [](LocalModelBrowser &self) { self.m_setup.runSpeedTest(self.selected().id); });
    on(m_delete, [](LocalModelBrowser &self) { self.confirmDelete(); });
    return detail;
}

Button LocalModelBrowser::addButton(const wchar_t *text)
{
    Button button;
    button.Content(box_value(text));
    m_actions.Children().Append(button);
    return button;
}

// The file is gigabytes and gone once deleted, so the person confirms, with
// Cancel the default.
void LocalModelBrowser::confirmDelete()
{
    const LocalModel model = selected();
    ContentDialog dialog;
    dialog.XamlRoot(m_host.xamlRoot());
    // The dialog opens in the popup layer, outside the window's RequestedTheme.
    if (m_host.effectiveTheme) {
        dialog.RequestedTheme(m_host.effectiveTheme());
    }
    dialog.Title(box_value(hs(deleteModelQuestion(model.name))));
    dialog.Content(box_value(hs(localModelText(LocalModelText::DeleteBody))));
    dialog.PrimaryButtonText(hs(localModelText(LocalModelText::DeleteModel)));
    dialog.CloseButtonText(L"Cancel");
    dialog.DefaultButton(ContentDialogButton::Close);
    dialog.Closed([weak = weak_from_this(), model](const ContentDialog &,
                                                    const ContentDialogClosedEventArgs &args) {
        if (args.Result() != ContentDialogResult::Primary) {
            return;
        }
        if (auto self = weak.lock()) {
            self->m_setup.removeModel(model);
        }
    });
    dialog.ShowAsync();
}

const LocalModel &LocalModelBrowser::selected() const
{
    const LocalModel *model = findLocalModel(m_selectedId);
    return model ? *model : m_setup.suggestedModel();
}

void LocalModelBrowser::pick(int index)
{
    if (index < 0 || index >= localModelCatalog().size()
        || localModelCatalog().at(index).id == m_selectedId) {
        return;
    }
    m_userPicked = true;
    m_selectedId = localModelCatalog().at(index).id;
    showDetail();
}

LocalSetup::ModelState LocalModelBrowser::state(const LocalModel &model) const
{
    return m_setup.modelState(model, m_host.model->draft().speech);
}

void LocalModelBrowser::showDetail()
{
    const LocalModel &model = selected();
    const LocalSetup::ModelState state = this->state(model);
    m_name.Text(hs(model.name));
    m_rating.Child(ratingBadge(model.rating, m_host));
    m_bestFor.Text(hs(model.bestFor));
    m_subtitle.Text(hs(state.suggested ? localModelText(LocalModelText::Suggested) : model.fileName));
    m_size.Text(hs(QStringLiteral("%1 · %2").arg(downloadSizeText(model.sizeBytes),
                                                 m_setup.fitLabel(model))));
    m_speed.Text(hs(state.speedDetail));
    m_wer.Text(hs(QStringLiteral("%1 clear speech\n%2 everyday speech")
                      .arg(werText(model.librispeechCleanWer), werText(model.fleursEnglishWer))));
    m_textShows.Text(hs(textShowsValue(model.streams)));
    m_licence.Text(hs(model.licence));
    QStringList notes;
    for (const QString &pro : model.pros) {
        notes.append(QStringLiteral("+ ") + pro);
    }
    for (const QString &con : model.cons) {
        notes.append(QStringLiteral("− ") + con);
    }
    m_prosCons.Text(hs(notes.join(QLatin1Char('\n'))));

    m_problem.Message(hs(state.problem));
    m_problem.IsOpen(!state.problem.isEmpty());

    const auto progress = m_setup.downloadProgress(model.id);
    const bool downloaded = state.downloaded;
    const bool inUse = state.inUse;
    setVisible(m_progress, bool(progress));
    setVisible(m_cancel, bool(progress));
    if (progress) {
        m_progress.Value(progress->second > 0 ? double(progress->first * 1000 / progress->second) : 0);
        m_state.Text(hs(QStringLiteral("%1 of %2").arg(downloadSizeText(progress->first),
                                                        downloadSizeText(model.sizeBytes))));
    } else {
        m_state.Text(inUse ? hs(localModelText(LocalModelText::InUse)) : hstring());
    }
    setVisible(m_state, !m_state.Text().empty());
    setVisible(m_download, !progress && !downloaded);
    m_download.IsEnabled(!state.tooLarge);
    m_download.Content(box_value(hs(state.tooLarge ? localModelText(LocalModelText::TooLarge)
                                                   : downloadCaption(model.sizeBytes))));
    setVisible(m_use, downloaded && !inUse);
    setVisible(m_test, downloaded);
    m_test.IsEnabled(!m_setup.speedTestRunning(model.id));
    setVisible(m_delete, downloaded);
}

} // namespace speecher::win
