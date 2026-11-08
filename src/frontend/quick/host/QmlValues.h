#pragma once

// Helpers the host's singletons share at the QML boundary: looking up a row
// of a table keyed by the id QML uses, an optional number as QML's null, a
// Recording's line as QML reads it, saving an edit core may refuse with the
// refusal's name the pages word, and the one undo a window's toast offers.

#include "core/SettingsEdit.h"
#include "core/SettingsStore.h"
#include "core/history/HistoryEntry.h"

#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <cstddef>
#include <optional>
#include <utility>

namespace speecher {

// The row whose id is id, or nullptr. Rows are any struct with a C string id.
template <typename Row, std::size_t count>
const Row *findRow(const Row (&rows)[count], const QString &id)
{
    for (const Row &row : rows) {
        if (id == QLatin1String(row.id)) {
            return &row;
        }
    }
    return nullptr;
}

// The number, or null where there is none, which QML reads as null rather
// than undefined.
template <typename Number>
QVariant optionalNumber(const std::optional<Number> &value)
{
    return value ? QVariant::fromValue(*value) : QVariant::fromValue(nullptr);
}

// A Recording's line as QML reads it: { atMs, speaker, text }.
inline QVariantMap lineRecord(const HistoryLine &line)
{
    return {{QStringLiteral("atMs"), line.atMs},
            {QStringLiteral("speaker"), line.speaker},
            {QStringLiteral("text"), line.text}};
}

// What QML reads for a record it handed over that the list can't take.
inline const QString invalidRecord = QStringLiteral("invalid");

// The refusal as the pages' refusal wording keys it: empty when saved.
inline QString refusalName(EditRefusal refusal)
{
    switch (refusal) {
    case EditRefusal::None:
        return {};
    case EditRefusal::Blank:
        return QStringLiteral("blank");
    case EditRefusal::Taken:
        return QStringLiteral("taken");
    case EditRefusal::Incomplete:
        return QStringLiteral("incomplete");
    case EditRefusal::Missing:
        return QStringLiteral("missing");
    case EditRefusal::BuiltIn:
        return QStringLiteral("builtIn");
    }
    return invalidRecord;
}

// Saves what edit makes of the settings when it refuses nothing, and names
// the refusal as refusalName does: empty when saved.
inline QString saveUnlessRefused(SettingsStore &settings, const std::function<EditRefusal(AppSettings &)> &edit)
{
    EditRefusal refusal = EditRefusal::None;
    settings.edit([&](AppSettings &s) { return (refusal = edit(s)) == EditRefusal::None; });
    return refusalName(refusal);
}

// The newest removal's undo. A window has one toast, which offers only the
// newest Undo, so keeping one forgets the one before, and its token works
// once.
class UndoSlot {
public:
    // Keeps undo and returns its token, or -1, keeping nothing, for no undo.
    int keep(const SettingsEdit &undo)
    {
        if (!undo) {
            return -1;
        }
        m_undo = undo;
        return ++m_last;
    }
    // The undo kept under token, which it then forgets; empty for an older
    // token or one taken already.
    SettingsEdit take(int token) { return token == m_last ? std::exchange(m_undo, {}) : SettingsEdit(); }

private:
    SettingsEdit m_undo;
    int m_last = -1;
};

} // namespace speecher
