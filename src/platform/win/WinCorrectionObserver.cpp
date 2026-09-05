#include "platform/win/WinCorrectionObserver.h"

#include <QCoreApplication>
#include <QMetaObject>

#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <mutex>
#include <thread>
#include <utility>

namespace speecher {
namespace {

using Microsoft::WRL::ComPtr;

struct ObserverState {
    std::mutex mutex;
    WinCorrectionObserver *observer = nullptr;
};

QString elementText(IUIAutomationElement *element)
{
    ComPtr<IUIAutomationTextPattern> textPattern;
    if (SUCCEEDED(element->GetCurrentPatternAs(
            UIA_TextPatternId, IID_PPV_ARGS(&textPattern))) && textPattern) {
        ComPtr<IUIAutomationTextRange> range;
        BSTR text = nullptr;
        if (SUCCEEDED(textPattern->get_DocumentRange(&range)) && range
            && SUCCEEDED(range->GetText(-1, &text)) && text) {
            const QString value = QString::fromWCharArray(text, SysStringLen(text));
            SysFreeString(text);
            return value;
        }
    }

    ComPtr<IUIAutomationValuePattern> valuePattern;
    BSTR value = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(
            UIA_ValuePatternId, IID_PPV_ARGS(&valuePattern)))
        && valuePattern
        && SUCCEEDED(valuePattern->get_CurrentValue(&value)) && value) {
        const QString text = QString::fromWCharArray(value, SysStringLen(value));
        SysFreeString(value);
        return text;
    }
    return {};
}

class TextChangedHandler final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          IUIAutomationEventHandler> {
public:
    explicit TextChangedHandler(std::shared_ptr<ObserverState> state)
        : m_state(std::move(state))
    {
    }

    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement *, EVENTID) override
    {
        if (QCoreApplication *application = QCoreApplication::instance()) {
            QMetaObject::invokeMethod(application, [state = m_state] {
                const std::lock_guard lock(state->mutex);
                if (state->observer) {
                    state->observer->valueChanged();
                }
            }, Qt::QueuedConnection);
        }
        return S_OK;
    }

private:
    std::shared_ptr<ObserverState> m_state;
};

} // namespace

struct WinCorrectionObserver::Native {
    ComPtr<IUIAutomation> automation;
    ComPtr<IUIAutomationElement> element;
    ComPtr<TextChangedHandler> handler;
    std::shared_ptr<ObserverState> state = std::make_shared<ObserverState>();
};

WinCorrectionObserver::WinCorrectionObserver(QObject *parent)
    : QObject(parent)
    , m_native(std::make_unique<Native>())
{
    m_native->state->observer = this;
    m_settle.setSingleShot(true);
    m_deadline.setSingleShot(true);
    connect(&m_settle, &QTimer::timeout, this, [this] { sample(); });
    connect(&m_deadline, &QTimer::timeout, this, [this] { cancel(); });
}

WinCorrectionObserver::~WinCorrectionObserver()
{
    {
        const std::lock_guard lock(m_native->state->mutex);
        m_native->state->observer = nullptr;
    }
    stop();
}

void WinCorrectionObserver::setEnabled(bool enabled)
{
    m_tracker.setEnabled(enabled);
    if (!enabled) {
        stop();
    }
}

void WinCorrectionObserver::cancel()
{
    stop();
    m_tracker.cancel();
}

void WinCorrectionObserver::observe(void *automation,
                                    void *element,
                                    CorrectionWindow window,
                                    CorrectionTracker::Observed observed)
{
    cancel();
    if (!automation || !element) {
        return;
    }
    m_tracker.begin(std::move(window), std::move(observed));
    if (!m_tracker.active()) {
        return;
    }

    m_native->automation = static_cast<IUIAutomation *>(automation);
    m_native->element = static_cast<IUIAutomationElement *>(element);
    m_native->handler = Microsoft::WRL::Make<TextChangedHandler>(m_native->state);
    if (FAILED(m_native->automation->AddAutomationEventHandler(
            UIA_Text_TextChangedEventId,
            m_native->element.Get(),
            TreeScope_Element,
            nullptr,
            m_native->handler.Get()))) {
        cancel();
        return;
    }
    m_deadline.start(correctionWindowMs);
}

void WinCorrectionObserver::valueChanged()
{
    sample();
    if (m_tracker.active()) {
        m_settle.start(correctionSettleMs);
    }
}

void WinCorrectionObserver::sample()
{
    if (m_tracker.active() && m_native->element) {
        m_tracker.sample(elementText(m_native->element.Get()));
    }
}

void WinCorrectionObserver::stop()
{
    m_settle.stop();
    m_deadline.stop();
    if (m_native->automation && m_native->handler) {
        auto automation = std::move(m_native->automation);
        auto element = std::move(m_native->element);
        auto handler = std::move(m_native->handler);
        std::thread([automation = std::move(automation),
                     element = std::move(element),
                     handler = std::move(handler)] {
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            automation->RemoveAutomationEventHandler(
                UIA_Text_TextChangedEventId, element.Get(), handler.Get());
            if (SUCCEEDED(initialized)) {
                CoUninitialize();
            }
        }).detach();
    }
    m_native->handler.Reset();
    m_native->element.Reset();
    m_native->automation.Reset();
}

} // namespace speecher
