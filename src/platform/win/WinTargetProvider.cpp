#include "platform/win/WinTargetProvider.h"

#include "platform/win/WinCorrectionObserver.h"

#include <QEventLoop>
#include <QFileInfo>
#include <QSet>
#include <QTimer>

#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>

#include <optional>
#include <string>

namespace speecher {
namespace {

using Microsoft::WRL::ComPtr;

constexpr int insertionVerificationAttempts = 5;
constexpr int insertionVerificationPauseMs = 30;
constexpr int targetContextCharacters = 240;

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

void spinEventLoop(int milliseconds)
{
    QEventLoop wait;
    QTimer::singleShot(milliseconds, &wait, &QEventLoop::quit);
    wait.exec(QEventLoop::ExcludeUserInputEvents);
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
    m_native->focused.Reset();
    m_valueBeforeInsertion.reset();
    m_insertionOffset.reset();
}

Target WinTargetProvider::capture(const QList<AppRecognitionRule> &recognitionRules)
{
    clearCapture();
    Target target;
    const HWND foreground = GetForegroundWindow();
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

    if (m_native->automation) {
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

            if (!target.secure) {
                const QString value = currentText(focused.Get());
                const auto selection = selectionOffsets(focused.Get());
                if (selection && selection->first >= 0
                    && selection->second >= selection->first
                    && selection->second <= value.size()) {
                    const int start = selection->first;
                    const int end = selection->second;
                    m_valueBeforeInsertion = value;
                    m_insertionOffset = start;
                    target.caretOffset = start;
                    if (end > start) {
                        target.selectionStart = start;
                        target.selectionEnd = end;
                        target.selectedText = value.mid(start, end - start);
                    }
                    target.nearbyTextBefore = value.left(start).right(targetContextCharacters);
                    target.nearbyTextAfter = value.mid(start, targetContextCharacters);
                }
            }
        }
    }

    target.terminalHost = terminalExecutables().contains(target.applicationId);
    target.category = classifyTarget(target, recognitionRules);
    if (target.terminalHost && target.category == AppCategory::General) {
        target.category = AppCategory::Terminal;
    }
    return target;
}

bool WinTargetProvider::stillFocused(const Target &target)
{
    if (target.processId <= 0) {
        return false;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &processId);
    return processId == DWORD(target.processId);
}

bool WinTargetProvider::canInsertText(const Target &target)
{
    if (target.secure || !m_native->automation || !m_native->focused
        || !stillFocused(target)) {
        return false;
    }
    ComPtr<IUIAutomationElement> focused;
    BOOL same = FALSE;
    BOOL enabled = FALSE;
    ComPtr<IUIAutomationValuePattern> valuePattern;
    BOOL readOnly = TRUE;
    return SUCCEEDED(m_native->automation->GetFocusedElement(&focused))
        && focused
        && SUCCEEDED(m_native->automation->CompareElements(
            focused.Get(), m_native->focused.Get(), &same))
        && same
        && SUCCEEDED(m_native->focused->get_CurrentIsEnabled(&enabled))
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

    const QString before = currentText(m_native->focused.Get());
    const auto selection = selectionOffsets(m_native->focused.Get());
    if (!selection || selection->second > before.size()) {
        if (error) {
            *error = QStringLiteral("The focused control did not report its insertion point");
        }
        return false;
    }
    const QString value = before.left(selection->first)
        + plainText + before.mid(selection->second);
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

    m_valueBeforeInsertion = before;
    m_insertionOffset = selection->first;
    if (currentText(m_native->focused.Get()) != value) {
        if (error) {
            *error = QStringLiteral("The focused control did not report the inserted text");
        }
        return false;
    }
    return true;
}

bool WinTargetProvider::verifyInsertion(const Target &target, const QString &plainText)
{
    if (!m_native->focused || plainText.isEmpty() || target.secure
        || !stillFocused(target)) {
        return false;
    }
    for (int attempt = 0; attempt < insertionVerificationAttempts; ++attempt) {
        if (attempt > 0) {
            spinEventLoop(insertionVerificationPauseMs);
        }
        const QString value = currentText(m_native->focused.Get());
        const bool changed = !m_valueBeforeInsertion || value != *m_valueBeforeInsertion;
        if (!changed || !m_insertionOffset
            || value.mid(*m_insertionOffset, plainText.size()) != plainText) {
            continue;
        }
        observeCorrections(target, value, *m_insertionOffset, plainText);
        return true;
    }
    return false;
}

void WinTargetProvider::observeCorrections(const Target &target,
                                           const QString &value,
                                           int insertedAt,
                                           const QString &plainText)
{
    const QString prefix = value.left(insertedAt).right(correctionContextChars);
    const QString suffix = value.mid(insertedAt + plainText.size()).left(correctionContextChars);
    if (!m_correctionObservationEnabled
        || prefix.size() < correctionMinContextChars
        || suffix.size() < correctionMinContextChars) {
        return;
    }
    if (!m_correctionObserver) {
        m_correctionObserver = std::make_unique<WinCorrectionObserver>(this);
    }
    m_correctionObserver->observe(
        m_native->automation.Get(),
        m_native->focused.Get(),
        {target, plainText, prefix, suffix},
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
