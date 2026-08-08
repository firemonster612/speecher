#pragma once

#include <QString>

#ifdef SPEECHER_WITH_ATSPI
#include <atspi/atspi.h>
#endif

namespace speecher::atspi {

class AccessibleHandle {
public:
    AccessibleHandle() = default;
#ifdef SPEECHER_WITH_ATSPI
    explicit AccessibleHandle(AtspiAccessible *accessible);
    AtspiAccessible *get() const;
#endif
    ~AccessibleHandle();

    AccessibleHandle(const AccessibleHandle &) = delete;
    AccessibleHandle &operator=(const AccessibleHandle &) = delete;
    AccessibleHandle(AccessibleHandle &&other) noexcept;
    AccessibleHandle &operator=(AccessibleHandle &&other) noexcept;

    explicit operator bool() const;
    void reset();

private:
    void *m_accessible = nullptr;
};

#ifdef SPEECHER_WITH_ATSPI
void clearError(GError **error);
QString takeString(gchar *value);
bool hasState(AtspiAccessible *object, AtspiStateType state);
#endif

} // namespace speecher::atspi
