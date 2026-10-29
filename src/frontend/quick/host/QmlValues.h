#pragma once

// Helpers the host's singletons share at the QML boundary: looking up a row
// of a table keyed by the id QML uses, and an optional number as QML's null.

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

} // namespace speecher
