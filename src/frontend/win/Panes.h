#pragma once

#include "frontend/win/SettingsModel.h"

#include <QStringList>

namespace speecher::win {

// The settings window's eight regular panes and contextual What's New page,
// the mac pane table (Panes.swift) verbatim with Segoe Fluent glyphs standing
// in for SF Symbols. The schema supplies rows and values; which pane a row
// appears on is decided here.

/// One card a pane asks for: a heading, a footnote, and the schema rows it
/// names. A pattern ending in `*` takes every row whose id starts with it.
struct PaneGroup {
    QString title;
    QStringList rows;
    QString help;
};

/// What a pane's groups are to each other.
enum class PaneLayout {
    /// Sections of one page, shown together.
    Sections,
    /// Views of one idea, one at a time, chosen with a SelectorBar.
    Alternatives,
    /// The shortcut recorder, which has no schema rows behind it.
    Shortcut,
};

struct Pane {
    QString id;
    QString title;
    /// Segoe Fluent Icons code point for the sidebar glyph.
    char16_t glyph;
    /// Schema pages whose otherwise-unmapped rows fall back to this pane.
    QStringList schemaPages;
    PaneLayout layout = PaneLayout::Sections;
    QList<PaneGroup> groups;
};

/// One card a pane actually shows: a group's rows, or a schema section no group
/// claimed. Both rendering and the sidebar's search read a pane as these, so a
/// row cannot be visible on a pane and missing from its search.
struct PaneCard {
    QString title;
    QString help;
    QList<RowSnapshot> rows;
};

const QList<Pane> &allPanes();
/// The sidebar's runs, in order, separated in the pane the way the Settings
/// app separates its groups. What's New appears only while pending or selected.
const QList<QStringList> &sidebarRuns();
const Pane *paneWithId(const QString &id);

/// The rows the patterns name that the schema currently offers, in the order
/// they were named. A pattern ending in `*` takes every row whose id starts
/// with the prefix, which is how the per-category paste rules arrive.
QList<RowSnapshot> rowsMatching(const QStringList &patterns, const QList<PageSnapshot> &pages);

/// A pane's groups, in order, each with the rows the schema currently offers
/// it. A group whose rows are all absent stays in the list and draws nothing,
/// so the index a SelectorBar holds keeps meaning what it did.
QList<PaneCard> groupCards(const Pane &pane, const QList<PageSnapshot> &pages);

/// Schema rows no pane placed explicitly, as the cards the schema itself
/// describes: they appear on the pane that owns their schema page, keeping
/// their section's title and help. A newly added schema page falls back to
/// General.
QList<PaneCard> unclaimedCards(const Pane &pane, const QList<PageSnapshot> &pages);

/// Whether anything on a pane — its name, a card heading, a row, or the help
/// under one — answers to the query. The cards are the ones the pane draws, so
/// nothing visible is unsearchable.
bool paneMatches(const Pane &pane, const QList<PageSnapshot> &pages, const QString &query);

} // namespace speecher::win
