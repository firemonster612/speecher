#include "platform/win/WinTargetProvider.h"

#include "platform/win/WinCorrectionObserver.h"
#include "output/win/WinPasteDelivery.h"

#include <QFileInfo>
#include <QSet>

#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>

#include <optional>
#include <string>

namespace speecher {
namespace {

using Microsoft::WRL::ComPtr;

QString bstrString(BSTR value)
{
    if (!value) {
        return {};
    }
    const QString text = QString::fromWCharArray(value, SysStringLen(value));
    SysFreeString(value);
    return text;
}

QString currentText(IUIAutomationElement *element)
{
    if (!element) {
        return {};
    }
    ComPtr<IUIAutomationTextPattern> textPattern;
    if (SUCCEEDED(element->GetCurrentPatternAs(
            UIA_TextPatternId, IID_PPV_ARGS(&textPattern))) && textPattern) {
        ComPtr<IUIAutomationTextRange> document;
        BSTR text = nullptr;
        if (SUCCEEDED(textPattern->get_DocumentRange(&document)) && document
            && SUCCEEDED(document->GetText(-1, &text))) {
            return bstrString(text);
        }
    }
    ComPtr<IUIAutomationValuePattern> valuePattern;
    BSTR value = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(
            UIA_ValuePatternId, IID_PPV_ARGS(&valuePattern)))
        && valuePattern
        && SUCCEEDED(valuePattern->get_CurrentValue(&value))) {
        return bstrString(value);
    }
    return {};
}

std::optional<int> rangeOffset(IUIAutomationTextRange *document,
                               IUIAutomationTextRange *range,
                               TextPatternRangeEndpoint endpoint)
{
    if (!document || !range) {
        return std::nullopt;
    }
    ComPtr<IUIAutomationTextRange> prefix;
    BSTR text = nullptr;
    if (FAILED(document->Clone(&prefix))
        || !prefix
        || FAILED(prefix->MoveEndpointByRange(
            TextPatternRangeEndpoint_End, range, endpoint))
        || FAILED(prefix->GetText(-1, &text))) {
        if (text) {
            SysFreeString(text);
        }
        return std::nullopt;
    }
    const int offset = int(SysStringLen(text));
    SysFreeString(text);
    return offset;
}

// Whether the selected text is read-only, from the text pattern: what a
// control without a value pattern, such as a read-only rich text box, says
// about itself. The selection is what Replace would change, so a protected
// stretch inside an editable document counts too, as does a selection only
// partly read-only, since Replace would run into that part. Nothing when
// there is no selection or the control doesn't say.
std::optional<bool> selectionReadOnly(IUIAutomation *automation, IUIAutomationElement *element)
{
    ComPtr<IUIAutomationTextPattern> textPattern;
    ComPtr<IUIAutomationTextRangeArray> selections;
    ComPtr<IUIAutomationTextRange> selection;
    int count = 0;
    if (FAILED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&textPattern))) || !textPattern
        || FAILED(textPattern->GetSelection(&selections)) || !selections
        || FAILED(selections->get_Length(&count)) || count < 1
        || FAILED(selections->GetElement(0, &selection)) || !selection) {
        return std::nullopt;
    }
    VARIANT value;
    VariantInit(&value);
    if (FAILED(selection->GetAttributeValue(UIA_IsReadOnlyAttributeId, &value))) {
        return std::nullopt;
    }
    std::optional<bool> readOnly;
    if (value.vt == VT_BOOL) {
        readOnly = value.boolVal != VARIANT_FALSE;
    } else if (value.vt == VT_UNKNOWN) {
        // UI Automation's reserved values are singletons, compared by identity.
        ComPtr<IUnknown> mixed;
        if (SUCCEEDED(automation->get_ReservedMixedAttributeValue(&mixed)) && mixed
            && value.punkVal == mixed.Get()) {
            readOnly = true;
        }
    }
    VariantClear(&value);
    return readOnly;
}

std::optional<QPair<int, int>> selectionOffsets(IUIAutomationElement *element)
{
    ComPtr<IUIAutomationTextPattern> textPattern;
    ComPtr<IUIAutomationTextRange> document;
    if (FAILED(element->GetCurrentPatternAs(
            UIA_TextPatternId, IID_PPV_ARGS(&textPattern)))
        || !textPattern
        || FAILED(textPattern->get_DocumentRange(&document))
        || !document) {
        return std::nullopt;
    }

    ComPtr<IUIAutomationTextRangeArray> selections;
    int selectionCount = 0;
    if (SUCCEEDED(textPattern->GetSelection(&selections)) && selections
        && SUCCEEDED(selections->get_Length(&selectionCount)) && selectionCount > 0) {
        ComPtr<IUIAutomationTextRange> range;
        if (SUCCEEDED(selections->GetElement(0, &range)) && range) {
            const auto start = rangeOffset(document.Get(), range.Get(),
                                           TextPatternRangeEndpoint_Start);
            const auto end = rangeOffset(document.Get(), range.Get(),
                                         TextPatternRangeEndpoint_End);
            if (start && end) {
                return QPair<int, int>(*start, *end);
            }
            return std::nullopt;
        }
    }

    ComPtr<IUIAutomationTextPattern2> textPattern2;
    ComPtr<IUIAutomationTextRange> caret;
    BOOL active = FALSE;
    if (SUCCEEDED(element->GetCurrentPatternAs(
            UIA_TextPattern2Id, IID_PPV_ARGS(&textPattern2)))
        && textPattern2
        && SUCCEEDED(textPattern2->GetCaretRange(&active, &caret)) && active && caret) {
        const auto offset = rangeOffset(document.Get(), caret.Get(),
                                        TextPatternRangeEndpoint_Start);
        if (offset) {
            return QPair<int, int>(*offset, *offset);
        }
    }
    return std::nullopt;
}

QString processImagePath(DWORD processId)
{
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) {
        return {};
    }
    wchar_t path[32768];
    DWORD size = DWORD(sizeof(path) / sizeof(path[0]));
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    return ok ? QString::fromWCharArray(path, int(size)) : QString();
}

QString windowTitle(HWND window)
{
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return {};
    }
    std::wstring title(size_t(length + 1), L'\0');
    const int copied = GetWindowTextW(window, title.data(), int(title.size()));
    return QString::fromWCharArray(title.data(), copied);
}

// The control's text and the selection a write will replace, or nothing when
// the control reports no selection inside its text.
std::optional<InsertionBaseline> baselineOf(IUIAutomationElement *element)
{
    const auto selection = selectionOffsets(element);
    if (!selection) {
        return std::nullopt;
    }
    return InsertionBaseline::forSelection(currentText(element), selection->first, selection->second);
}

const QSet<QString> &terminalExecutables()
{
    static const QSet<QString> names{
        QStringLiteral("windowsterminal"),
        QStringLiteral("wt"),
        QStringLiteral("cmd"),
        QStringLiteral("powershell"),
        QStringLiteral("pwsh"),
        QStringLiteral("wezterm-gui"),
        QStringLiteral("alacritty"),
    };
    return names;
}

} // namespace

struct WinTargetProvider::Native {
    HWND window = nullptr;
    ComPtr<IUIAutomation> automation;
    ComPtr<IUIAutomationElement> focused;
};

WinTargetProvider::WinTargetProvider(QObject *parent)
    : TargetProvider(parent)
    , m_native(std::make_unique<Native>())
{
    CoCreateInstance(CLSID_CUIAutomation8,
                     nullptr,
                     CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&m_native->automation));
}

WinTargetProvider::~WinTargetProvider()
{
    clearCapture();
}

void WinTargetProvider::clearCapture()
{
    if (m_correctionObserver) {
        m_correctionObserver->cancel();
    }
    m_native->window = nullptr;
    m_native->focused.Reset();
    m_insertionBaseline.reset();
}

Target WinTargetProvider::capture()
{
    clearCapture();
    Target target;
    const HWND foreground = GetForegroundWindow();
    m_native->window = foreground;
    if (!foreground) {
        return target;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    const QFileInfo executable(processImagePath(processId));
    target.processId = processId;
    target.processName = executable.fileName();
    target.applicationId = executable.completeBaseName().toLower();
    target.applicationName = executable.fileName();
    target.windowTitle = windowTitle(foreground);
    target.toolkit = QStringLiteral("UI Automation");

    if (processId != GetCurrentProcessId() && m_native->automation) {
        ComPtr<IUIAutomationElement> focused;
        int focusedProcessId = 0;
        if (SUCCEEDED(m_native->automation->GetFocusedElement(&focused))
            && focused
            && SUCCEEDED(focused->get_CurrentProcessId(&focusedProcessId))
            && focusedProcessId == int(processId)) {
            m_native->focused = focused;
            target.accessible = true;

            BSTR role = nullptr;
            if (SUCCEEDED(focused->get_CurrentLocalizedControlType(&role))) {
                target.role = bstrString(role);
            }
            BSTR name = nullptr;
            if (SUCCEEDED(focused->get_CurrentName(&name))) {
                target.controlName = bstrString(name);
            }
            BOOL password = FALSE;
            if (SUCCEEDED(focused->get_CurrentIsPassword(&password))) {
                target.secure = password;
            }
            // Read-only only when the control says so, through its value
            // pattern or else its text: a document such as Word's offers no
            // value pattern and is still typed into.
            ComPtr<IUIAutomationValuePattern> valuePattern;
            BOOL readOnly = FALSE;
            if (SUCCEEDED(focused->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&valuePattern)))
                && valuePattern && SUCCEEDED(valuePattern->get_CurrentIsReadOnly(&readOnly))) {
                target.editable = !readOnly;
            } else if (const std::optional<bool> readOnlySelection =
                           selectionReadOnly(m_native->automation.Get(), focused.Get())) {
                target.editable = !*readOnlySelection;
            }

            if (!target.secure) {
                m_insertionBaseline = baselineOf(focused.Get());
                if (m_insertionBaseline) {
                    const auto &[value, start, end] = *m_insertionBaseline;
                    target.caretOffset = start;
                    if (end > start) {
                        target.selectionStart = start;
                        target.selectionEnd = end;
                        target.selectedText = value.mid(start, end - start);
                    }
                    target.nearbyTextBefore = value.left(start).right(nearbyTextChars);
                    target.nearbyTextAfter = value.mid(end, nearbyTextChars);
                }
            }
        }
    }

    target.terminalHost = terminalExecutables().contains(target.applicationId);
    // A terminal shows output, and a paste there goes in at the prompt
    // rather than over the selection, so its text is never edited.
    if (target.terminalHost) {
        target.editable = false;
    }
    return target;
}

bool WinTargetProvider::stillFocused(const Target &target)
{
    if (target.processId <= 0 || !m_native->window || GetForegroundWindow() != m_native->window) {
        return false;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &processId);
    if (processId != DWORD(target.processId)) return false;
    if (!m_native->focused) return true;
    ComPtr<IUIAutomationElement> focused;
    BOOL same = FALSE;
    return m_native->automation
        && SUCCEEDED(m_native->automation->GetFocusedElement(&focused)) && focused
        && SUCCEEDED(m_native->automation->CompareElements(
            focused.Get(), m_native->focused.Get(), &same)) && same;
}

bool WinTargetProvider::canInsertText(const Target &target)
{
    if (target.secure || !m_native->automation || !m_native->focused
        || !stillFocused(target)) {
        return false;
    }
    BOOL enabled = FALSE;
    ComPtr<IUIAutomationValuePattern> valuePattern;
    BOOL readOnly = TRUE;
    return SUCCEEDED(m_native->focused->get_CurrentIsEnabled(&enabled))
        && enabled
        && SUCCEEDED(m_native->focused->GetCurrentPatternAs(
            UIA_ValuePatternId, IID_PPV_ARGS(&valuePattern)))
        && valuePattern
        && SUCCEEDED(valuePattern->get_CurrentIsReadOnly(&readOnly))
        && !readOnly
        && selectionOffsets(m_native->focused.Get()).has_value();
}

bool WinTargetProvider::insertText(const Target &target,
                                   const QString &plainText,
                                   QString *error)
{
    if (!canInsertText(target)) {
        if (error) {
            *error = QStringLiteral("The focused control does not accept direct text insertion");
        }
        return false;
    }

    const std::optional<InsertionBaseline> baseline = baselineOf(m_native->focused.Get());
    if (!baseline) {
        if (error) {
            *error = QStringLiteral("The focused control did not report its insertion point");
        }
        return false;
    }
    const QString value = baseline->expectedAfter(plainText);
    ComPtr<IUIAutomationValuePattern> valuePattern;
    if (FAILED(m_native->focused->GetCurrentPatternAs(
            UIA_ValuePatternId, IID_PPV_ARGS(&valuePattern))) || !valuePattern) {
        if (error) {
            *error = QStringLiteral("The focused control stopped accepting direct text insertion");
        }
        return false;
    }
    BSTR inserted = SysAllocStringLen(
        reinterpret_cast<const wchar_t *>(value.utf16()), UINT(value.size()));
    const HRESULT result = valuePattern->SetValue(inserted);
    SysFreeString(inserted);
    if (FAILED(result)) {
        if (error) {
            *error = QStringLiteral("The focused control rejected the inserted text");
        }
        return false;
    }

    m_insertionBaseline = baseline;
    // SetValue already succeeded. TextDelivery verifies separately; treating
    // unavailable or delayed readback as rejection could insert the text twice.
    return true;
}

bool WinTargetProvider::preparePaste(const Target &target)
{
    m_insertionBaseline.reset();
    if (!stillFocused(target)) {
        return false;
    }
    WinPasteDelivery::waitForReleasedKeys();
    if (!stillFocused(target)) {
        return false;
    }
    if (!target.secure && m_native->focused) {
        m_insertionBaseline = baselineOf(m_native->focused.Get());
    }
    return stillFocused(target);
}

bool WinTargetProvider::verifyInsertion(const Target &target, const QString &plainText)
{
    if (!m_native->focused || !m_insertionBaseline || target.secure || !stillFocused(target)) {
        return false;
    }
    // A copy, because a capture during verify's nested event loop resets the member.
    const InsertionBaseline baseline = *m_insertionBaseline;
    if (!baseline.verify(plainText, [this, &target]() -> std::optional<QString> {
            if (!stillFocused(target)) {
                return std::nullopt;
            }
            return currentText(m_native->focused.Get());
        })) {
        return false;
    }
    if (const auto window = baseline.correctionWindow(target, plainText)) {
        observeCorrections(*window);
    }
    return true;
}

void WinTargetProvider::observeCorrections(const CorrectionWindow &window)
{
    if (!m_correctionObservationEnabled) {
        return;
    }
    if (!m_correctionObserver) {
        m_correctionObserver = std::make_unique<WinCorrectionObserver>(this);
    }
    m_correctionObserver->observe(
        m_native->automation.Get(),
        m_native->focused.Get(),
        window,
        [this](const QString &original,
               const QString &corrected,
               const QString &applicationId,
               double confidence) {
            emit correctionObserved(original, corrected, applicationId, confidence);
        });
}

void WinTargetProvider::setCorrectionObservationEnabled(bool enabled)
{
    m_correctionObservationEnabled = enabled;
    if (m_correctionObserver) {
        m_correctionObserver->setEnabled(enabled);
    }
}

} // namespace speecher
