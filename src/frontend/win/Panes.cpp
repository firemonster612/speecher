#include "frontend/win/Panes.h"

#include <QSet>

namespace speecher::win {

// Glyphs from the Segoe Fluent Icons table: Settings, Diamond (the sparkle
// stand-in), Microphone, KeyboardClassic, Font, ClipboardList, AllApps,
// Dictionary, Contact.
const QList<Pane> &allPanes()
{
    static const QList<Pane> panes = {
        {QStringLiteral("general"), QStringLiteral("General"), u'\xE713',
         {QStringLiteral("general")}, PaneLayout::Sections, {
             {QStringLiteral("Appearance"),
              {QStringLiteral("themeControl"), QStringLiteral("pauseMedia"),
               QStringLiteral("soundsEnabled"), QStringLiteral("previewWords")}},
             {QStringLiteral("System"),
              {QStringLiteral("launchAtLogin"), QStringLiteral("clipboardOutputStatus")}},
             {QStringLiteral("Maintenance"), {QStringLiteral("runSetup")}},
             {QStringLiteral("Updates"),
              {QStringLiteral("updateChannel"), QStringLiteral("autoCheckUpdates"),
               QStringLiteral("autoInstallUpdates"), QStringLiteral("checkForUpdates"),
               QStringLiteral("currentVersion"), QStringLiteral("whatsNew")}},
         }},
        {QStringLiteral("whatsNew"), QStringLiteral("What's New"), u'\xE8A9',
         {QStringLiteral("whatsNew")}, PaneLayout::Sections, {}},
        {QStringLiteral("dictation"), QStringLiteral("Dictation"), u'\xE720',
         {QStringLiteral("audio")}, PaneLayout::Sections, {
             {QStringLiteral("Transcription"), {QStringLiteral("speechProvider")}},
             {QStringLiteral("Microphone"),
              {QStringLiteral("audioDevice"), QStringLiteral("captureMode")}},
             {QStringLiteral("Timing"),
              {QStringLiteral("preRollMs"), QStringLiteral("postRollMs"),
               QStringLiteral("readinessTimeoutMs")}},
             {QStringLiteral("Silence"),
              {QStringLiteral("vadEnabled"), QStringLiteral("vadThresholdPercent")}},
         }},
        {QStringLiteral("shortcut"), QStringLiteral("Shortcut"), u'\xE765',
         {}, PaneLayout::Shortcut, {}},
        {QStringLiteral("text"), QStringLiteral("Text"), u'\xE8D2',
         {QStringLiteral("refinement")}, PaneLayout::Sections, {
             {QStringLiteral("Refinement"),
              {QStringLiteral("refinementProvider"), QStringLiteral("defaultWritingProfile"),
               QStringLiteral("targetContextControl"), QStringLiteral("includeScreenshotContext")}},
             {QStringLiteral("Profile Behavior"), {QStringLiteral("writingProfileBehavior")}},
         }},
        {QStringLiteral("delivery"), QStringLiteral("Delivery"), u'\xF0E3',
         {QStringLiteral("output")}, PaneLayout::Sections, {
             {QStringLiteral("Delivery"),
              {QStringLiteral("outputMethod"), QStringLiteral("outputFormat"),
               QStringLiteral("completionStatusDuration"),
               QStringLiteral("restoreClipboardAfterTyping")}},
             {QStringLiteral("Paste Behavior"),
              {QStringLiteral("globalPasteRule"), QStringLiteral("categoryPasteRule_*")}},
             // Only a build that can set up a virtual keyboard has this row,
             // and Windows is not one; an empty group draws nothing.
             {QStringLiteral("Advanced"), {QStringLiteral("virtualKeyboard")}},
         }},
        {QStringLiteral("apps"), QStringLiteral("Apps"), u'\xE71D',
         {QStringLiteral("applications")}, PaneLayout::Alternatives, {
             {QStringLiteral("Application Recognition"), {QStringLiteral("appRecognitionRules")}},
             {QStringLiteral("App-Specific Paste Rules"), {QStringLiteral("applicationPasteRules")}},
         }},
        {QStringLiteral("vocabulary"), QStringLiteral("Vocabulary"), u'\xE82D',
         {QStringLiteral("vocabulary"), QStringLiteral("corrections"), QStringLiteral("bindings")},
         PaneLayout::Alternatives, {
             {QStringLiteral("Terms"),
              {QStringLiteral("vocabularyEntries"), QStringLiteral("vocabularyLimit")}},
             {QStringLiteral("Corrections"),
              {QStringLiteral("correctionLearningControl"), QStringLiteral("learnedCorrections")}},
             {QStringLiteral("Replacements"), {QStringLiteral("bindingRules")}},
         }},
        {QStringLiteral("accounts"), QStringLiteral("Accounts"), u'\xE77B',
         {QStringLiteral("providers")}, PaneLayout::Sections, {
             {QStringLiteral("OpenAI"),
              {QStringLiteral("openAiModel"), QStringLiteral("openAiEffort"),
               QStringLiteral("openAiFastMode"), QStringLiteral("openAiAuthMode"),
               QStringLiteral("openAiCliproxyAccount"), QStringLiteral("openAiAuth")}},
             {QStringLiteral("Anthropic"),
              {QStringLiteral("anthropicModel"), QStringLiteral("anthropicModelCaution"),
               QStringLiteral("anthropicEffort"), QStringLiteral("anthropicFastMode"),
               QStringLiteral("anthropicAuthMode"), QStringLiteral("anthropicCliproxyAccount")}},
         }},
    };
    return panes;
}

const QList<QStringList> &sidebarRuns()
{
    static const QList<QStringList> runs = {
        {QStringLiteral("general")},
        {QStringLiteral("dictation"), QStringLiteral("shortcut"), QStringLiteral("text")},
        {QStringLiteral("delivery"), QStringLiteral("apps")},
        {QStringLiteral("vocabulary"), QStringLiteral("accounts")},
    };
    return runs;
}

const Pane *paneWithId(const QString &id)
{
    for (const Pane &pane : allPanes()) {
        if (pane.id == id) {
            return &pane;
        }
    }
    return nullptr;
}

namespace {

const RowSnapshot *rowWithId(const QString &rowId, const QList<PageSnapshot> &pages)
{
    for (const PageSnapshot &page : pages) {
        for (const SectionSnapshot &section : page.sections) {
            for (const RowSnapshot &row : section.rows) {
                if (row.id == rowId) {
                    return &row;
                }
            }
        }
    }
    return nullptr;
}

/// What a group says about itself, or failing that what the schema says under
/// the section these rows came from, or the help of a row that fills the whole
/// card and so has nowhere else to put it.
QString footnote(const PaneGroup &group,
                 const QList<RowSnapshot> &rows,
                 const QList<PageSnapshot> &pages)
{
    if (!group.help.isEmpty()) {
        return group.help;
    }
    QSet<QString> ids;
    for (const RowSnapshot &row : rows) {
        ids.insert(row.id);
    }
    for (const PageSnapshot &page : pages) {
        for (const SectionSnapshot &section : page.sections) {
            if (section.help.isEmpty()) {
                continue;
            }
            for (const RowSnapshot &row : section.rows) {
                if (ids.contains(row.id)) {
                    return section.help;
                }
            }
        }
    }
    for (const RowSnapshot &row : rows) {
        if (row.collection) {
            return row.help;
        }
    }
    return {};
}

} // namespace

QList<RowSnapshot> rowsMatching(const QStringList &patterns, const QList<PageSnapshot> &pages)
{
    QList<RowSnapshot> found;
    for (const QString &pattern : patterns) {
        if (!pattern.endsWith(QLatin1Char('*'))) {
            if (const RowSnapshot *row = rowWithId(pattern, pages)) {
                found.append(*row);
            }
            continue;
        }
        const QString prefix = pattern.chopped(1);
        for (const PageSnapshot &page : pages) {
            for (const SectionSnapshot &section : page.sections) {
                for (const RowSnapshot &row : section.rows) {
                    if (row.id.startsWith(prefix)) {
                        found.append(row);
                    }
                }
            }
        }
    }
    return found;
}

QList<PaneCard> groupCards(const Pane &pane, const QList<PageSnapshot> &pages)
{
    QList<PaneCard> cards;
    for (const PaneGroup &group : pane.groups) {
        const QList<RowSnapshot> placed = rowsMatching(group.rows, pages);
        cards.append({group.title, footnote(group, placed, pages), placed});
    }
    return cards;
}

QList<PaneCard> unclaimedCards(const Pane &pane, const QList<PageSnapshot> &pages)
{
    QSet<QString> claimed;
    QSet<QString> ownedPages;
    for (const Pane &candidate : allPanes()) {
        for (const PaneGroup &group : candidate.groups) {
            for (const RowSnapshot &row : rowsMatching(group.rows, pages)) {
                claimed.insert(row.id);
            }
        }
        for (const QString &pageId : candidate.schemaPages) {
            ownedPages.insert(pageId);
        }
    }
    QList<PaneCard> cards;
    for (const PageSnapshot &page : pages) {
        const bool belongsHere = pane.schemaPages.contains(page.id)
            || (pane.id == QStringLiteral("general") && !ownedPages.contains(page.id));
        if (!belongsHere) {
            continue;
        }
        for (const SectionSnapshot &section : page.sections) {
            QList<RowSnapshot> unplaced;
            for (const RowSnapshot &row : section.rows) {
                if (page.id == QStringLiteral("whatsNew") || !claimed.contains(row.id)) {
                    unplaced.append(row);
                }
            }
            if (unplaced.isEmpty()) {
                continue;
            }
            cards.append({section.title.isEmpty() ? page.title : section.title,
                          section.help,
                          unplaced});
        }
    }
    return cards;
}

bool paneMatches(const Pane &pane, const QList<PageSnapshot> &pages, const QString &query)
{
    if (query.isEmpty()) {
        return true;
    }
    const auto hit = [needle = query.toLower()](const QString &text) {
        return text.toLower().contains(needle);
    };
    if (hit(pane.title)) {
        return true;
    }
    const QList<PaneCard> cards = groupCards(pane, pages) + unclaimedCards(pane, pages);
    for (const PaneCard &card : cards) {
        if (hit(card.title) || hit(card.help)) {
            return true;
        }
        for (const RowSnapshot &row : card.rows) {
            if (hit(row.label) || hit(row.help)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace speecher::win
