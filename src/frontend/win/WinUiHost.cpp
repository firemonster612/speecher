#include "frontend/win/WinUiHost.h"

#include <windows.h>
#include <winrt/base.h>

#include <WindowsAppSDK-VersionInfo.h>
#include <MddBootstrap.h>
#include <Microsoft.UI.Dispatching.Interop.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#pragma pop_macro("GetCurrentTime")

#include <QCoreApplication>
#include <QDebug>
#include <QFile>

// Q_INIT_RESOURCE must sit outside every namespace. Defensive: a resource
// compiled into a static library can be dropped by the linker when nothing
// references its initializer.
static void speecherInitWinXamlResources()
{
    Q_INIT_RESOURCE(win_xaml);
}

namespace speecher {
namespace {

namespace MddBootstrap =
    ::Microsoft::Windows::ApplicationModel::DynamicDependency::Bootstrap;
using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Markup;

struct XamlApp
    : ApplicationT<XamlApp, IXamlMetadataProvider> {
    IXamlType GetXamlType(Windows::UI::Xaml::Interop::TypeName const &type)
    {
        return m_provider.GetXamlType(type);
    }

    IXamlType GetXamlType(hstring const &fullName)
    {
        return m_provider.GetXamlType(fullName);
    }

    com_array<XmlnsDefinition> GetXmlnsDefinitions()
    {
        return m_provider.GetXmlnsDefinitions();
    }

private:
    XamlTypeInfo::XamlControlsXamlMetaDataProvider m_provider;
};

} // namespace

struct WinUiHost::Native {
    Native()
        : bootstrap(MddBootstrap::Initialize(
              WINDOWSAPPSDK_RELEASE_MAJORMINOR,
              WINDOWSAPPSDK_RELEASE_VERSION_TAG_W,
              WINDOWSAPPSDK_RUNTIME_VERSION_UINT64,
              MddBootstrap::InitializeOptions::OnNoMatch_None))
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueueController::CreateOnCurrentThread();
        application = winrt::make<XamlApp>();
        // A XAML exception nobody catches dies as a stowed exception deep in
        // CoreMessaging; the log line here is the only place its text appears.
        application.UnhandledException(
            [](const winrt::Windows::Foundation::IInspectable &,
               const UnhandledExceptionEventArgs &args) {
                qWarning() << "unhandled XAML exception:"
                           << QString::fromWCharArray(args.Message().c_str());
            });
        xamlManager = winrt::Microsoft::UI::Xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        application.Resources().MergedDictionaries().Append(Controls::XamlControlsResources());
        // The settings-card styles, merged after XamlControlsResources so
        // their StaticResource references resolve against it.
        speecherInitWinXamlResources();
        QFile styles(QStringLiteral(":/win/xaml/styles.xaml"));
        if (!styles.open(QIODevice::ReadOnly)) {
            qFatal("styles.xaml is missing from the win front end's resources");
        }
        const QString xaml = QString::fromUtf8(styles.readAll());
        application.Resources().MergedDictionaries().Append(
            XamlReader::Load(winrt::hstring(reinterpret_cast<const wchar_t *>(xaml.utf16()),
                                            static_cast<uint32_t>(xaml.size())))
                .as<ResourceDictionary>());
    }

    ~Native()
    {
        shutdown();
        winrt::uninit_apartment();
    }

    void shutdown()
    {
        if (stopped) {
            return;
        }
        stopped = true;
        xamlManager.Close();
        application.Exit();
        dispatcher.ShutdownQueue();
        application = nullptr;
        xamlManager = nullptr;
        dispatcher = nullptr;
    }

    MddBootstrap::unique_mddbootstrapshutdown bootstrap;
    winrt::Microsoft::UI::Dispatching::DispatcherQueueController dispatcher{nullptr};
    winrt::Microsoft::UI::Xaml::Hosting::WindowsXamlManager xamlManager{nullptr};
    Application application{nullptr};
    bool stopped = false;
};

WinUiHost::WinUiHost()
    : m_native(std::make_unique<Native>())
{
}

WinUiHost::~WinUiHost() = default;

void WinUiHost::installNativeEventFilter()
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

void WinUiHost::shutdown()
{
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
    m_native->shutdown();
}

bool WinUiHost::nativeEventFilter(const QByteArray &eventType,
                                  void *message,
                                  qintptr *result)
{
    Q_UNUSED(eventType);
    Q_UNUSED(result);
    return ContentPreTranslateMessage(static_cast<MSG *>(message)) != FALSE;
}

} // namespace speecher
