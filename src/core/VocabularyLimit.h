#pragma once

#include <QString>
#include <QStringList>

namespace speecher::VocabularyLimit {

constexpr int maxKeyterms = 100;
constexpr int maxTokens = 500;

int tokenCount(const QString &term);
int tokenCount(const QStringList &terms);
QStringList limited(const QStringList &terms);
QString summary(const QStringList &terms);

} // namespace speecher::VocabularyLimit
