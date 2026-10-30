#include "frontend/quick/host/QuickNavigation.h"

namespace speecher {

namespace {

QuickNavigation *g_instance = nullptr;

} // namespace

QuickNavigation::QuickNavigation(QObject *parent)
    : QObject(parent)
{
    g_instance = this;
}

QuickNavigation::~QuickNavigation()
{
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

QuickNavigation *QuickNavigation::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(g_instance, "QuickNavigation::create", "the host makes Navigation before loading QML");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
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

void QuickNavigation::go(const QString &pageId)
{
    setPendingRow({});
    show(pageId);
}

void QuickNavigation::goToRow(const QString &pageId, const QString &rowId)
{
    if (pageId == m_current) {
        // The page is up already, so only this tells it to land again.
        m_pendingRow = rowId;
        emit pendingRowChanged();
    } else {
        setPendingRow(rowId);
    }
    show(pageId);
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
