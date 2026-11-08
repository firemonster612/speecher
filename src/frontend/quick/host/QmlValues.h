#pragma once

// Helpers the host's singletons share at the QML boundary: looking up a row
// of a table keyed by the id QML uses, an optional number as QML's null, and
// why core refused an edit, by the name the pages word.

#include "core/SettingsEdit.h"

#include <QString>
#include <QVariant>

#include <cstddef>
#include <optional>

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

} // namespace speecher
