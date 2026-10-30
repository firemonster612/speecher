#pragma once

#include <QQmlEngine>

#include <type_traits>

namespace speecher {

// The instance side of a host singleton: the object the host makes before
// loading any QML, which QML's create() hands the engine. The host owns it,
// so the engine never deletes it. T derives from QObject and from this.
template <typename T>
class HostSingleton {
public:
    // The engine's instance: the one the host made before loading any QML.
    static T *create(QQmlEngine *, QJSEngine *)
    {
        Q_ASSERT_X(s_instance, "HostSingleton::create", "the host makes each singleton before loading QML");
        T *instance = static_cast<T *>(s_instance);
        if (instance) {
            QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
        }
        return instance;
    }

protected:
    HostSingleton()
    {
        // Qt makes its own instance of a default-constructible singleton type
        // and never calls create, so QML would not see the host's.
        static_assert(!std::is_default_constructible_v<T>, "a host singleton's constructor takes what the host gives it");
        s_instance = this;
    }
    ~HostSingleton()
    {
        if (s_instance == this) {
            s_instance = nullptr;
        }
    }
    HostSingleton(const HostSingleton &) = delete;
    HostSingleton &operator=(const HostSingleton &) = delete;

private:
    static inline HostSingleton *s_instance = nullptr;
};

} // namespace speecher
