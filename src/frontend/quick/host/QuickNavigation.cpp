#include "frontend/quick/host/QuickNavigation.h"

namespace speecher {

QuickNavigation::QuickNavigation(QObject *parent)
    : QObject(parent)
{
}

QString QuickNavigation::current() const
{
    return m_current;
}

QString QuickNavigation::pendingRow() const
{
    return m_pendingRow;
}

void QuickNavigation::setPendingRow(const QString &rowId)
{
    if (rowId == m_pendingRow) {
        return;
    }
    m_pendingRow = rowId;
    emit pendingRowChanged();
}

QString QuickNavigation::returnTo() const
{
    return m_returnTo;
}

void QuickNavigation::go(const QString &pageId)
{
    setReturnTo({});
    setPendingRow({});
    show(pageId);
}

void QuickNavigation::goToRow(const QString &pageId, const QString &rowId)
{
    setReturnTo({});
    if (pageId == m_current) {
        // The page is up already, so only this tells it to land again.
        m_pendingRow = rowId;
        emit pendingRowChanged();
    } else {
        setPendingRow(rowId);
    }
    show(pageId);
}

void QuickNavigation::follow(const QString &pageId, const QString &rowId, const QString &from)
{
    const QString origin = from.isEmpty() ? m_current : from;
    if (rowId.isEmpty()) {
        go(pageId);
    } else {
        goToRow(pageId, rowId);
    }
    // A link to the page it starts from leads nowhere to come back from.
    setReturnTo(origin == pageId ? QString() : origin);
    if (origin == setupOrigin) {
        emit mainRequested();
    }
}

void QuickNavigation::openInMain(const QString &pageId, const QString &rowId)
{
    if (rowId.isEmpty()) {
        go(pageId);
    } else {
        goToRow(pageId, rowId);
    }
    emit mainRequested();
}

void QuickNavigation::goBack()
{
    const QString origin = m_returnTo;
    if (origin.isEmpty()) {
        return;
    }
    if (origin == setupOrigin) {
        setReturnTo({});
        emit setupRequested();
        return;
    }
    go(origin);
}

void QuickNavigation::forgetReturnTo(const QString &from)
{
    if (m_returnTo == from) {
        setReturnTo({});
    }
}

void QuickNavigation::setReturnTo(const QString &from)
{
    if (from == m_returnTo) {
        return;
    }
    m_returnTo = from;
    emit returnToChanged();
}

void QuickNavigation::show(const QString &pageId)
{
    if (pageId != m_current) {
        m_current = pageId;
        emit currentChanged();
    }
    emit requested(pageId);
}

} // namespace speecher
