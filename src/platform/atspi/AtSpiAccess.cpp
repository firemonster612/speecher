#include "platform/atspi/AtSpiAccess.h"

#include <utility>

namespace speecher::atspi {

#ifdef SPEECHER_WITH_ATSPI
AccessibleHandle::AccessibleHandle(AtspiAccessible *accessible)
    : m_accessible(accessible)
{
}

AtspiAccessible *AccessibleHandle::get() const
{
    return ATSPI_ACCESSIBLE(m_accessible);
}
#endif

AccessibleHandle::~AccessibleHandle()
{
    reset();
}

AccessibleHandle::AccessibleHandle(AccessibleHandle &&other) noexcept
    : m_accessible(std::exchange(other.m_accessible, nullptr))
{
}

AccessibleHandle &AccessibleHandle::operator=(AccessibleHandle &&other) noexcept
{
    if (this != &other) {
        reset();
        m_accessible = std::exchange(other.m_accessible, nullptr);
    }
    return *this;
}

AccessibleHandle::operator bool() const
{
    return m_accessible != nullptr;
}

void AccessibleHandle::reset()
{
#ifdef SPEECHER_WITH_ATSPI
    if (m_accessible) {
        g_object_unref(ATSPI_ACCESSIBLE(m_accessible));
    }
#endif
    m_accessible = nullptr;
}

#ifdef SPEECHER_WITH_ATSPI
void clearError(GError **error)
{
    if (error && *error) {
        g_error_free(*error);
        *error = nullptr;
    }
}

QString takeString(gchar *value)
{
    const QString result = QString::fromUtf8(value ? value : "");
    g_free(value);
    return result;
}

bool hasState(AtspiAccessible *object, AtspiStateType state)
{
    AtspiStateSet *states = object ? atspi_accessible_get_state_set(object) : nullptr;
    const bool present = states && atspi_state_set_contains(states, state);
    if (states) {
        g_object_unref(states);
    }
    return present;
}
#endif

} // namespace speecher::atspi
