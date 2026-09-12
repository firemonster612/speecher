#include "frontend/win/ShortcutRecorder.h"

#include "app/ApplicationController.h"
#include "core/ShortcutBinding.h"
#include "platform/win/WinGlobalShortcutBinder.h"

#include <QKeySequence>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#pragma pop_macro("GetCurrentTime")

namespace speecher::win {

namespace {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using winrt::Windows::System::VirtualKey;

void bind(PaneHost &host, const VirtualKey key)
{
    const int qtKey = ShortcutRecorder::qtKeyForVirtualKey(static_cast<int>(key));
    if (qtKey == 0) {
        host.shortcutProblem = QStringLiteral("That key cannot be part of a shortcut.");
        return;
    }
    const Qt::KeyboardModifiers modifiers = ShortcutRecorder::heldModifiers();
    // A shortcut with no modifier would swallow the key everywhere on the
    // desktop, including in whatever the dictation is going into.
    if (modifiers == Qt::NoModifier) {
        host.shortcutProblem =
            QStringLiteral("Hold Ctrl, Alt, Shift or Win as part of the shortcut.");
        return;
    }
    QString error;
    if (host.controller->setGlobalShortcut(QKeySequence(QKeyCombination(modifiers, Qt::Key(qtKey))),
                                           &error)) {
        host.shortcutProblem.clear();
        host.shortcutNotice.clear();
        return;
    }
    host.shortcutProblem =
        error.isEmpty() ? QStringLiteral("That shortcut could not be bound.") : error;
}

// The physical key, not the layout's meaning of it: the scancode plus the
// extended byte is the vocabulary's win column, so bare modifiers record and
// left is told from right. Saving is not gated on the typing warning; the
// warning shows inline afterwards.
void bindSingleKey(PaneHost &host, int scanCode)
{
    const PhysicalKey *key = physicalKeyForWin(scanCode);
    if (!key) {
        host.shortcutProblem = QStringLiteral("That key cannot be a dictation key.");
        return;
    }
    const ShortcutBinding binding = ShortcutBinding::singleKey(QString::fromLatin1(key->code));
    const QString reason = host.controller->globalShortcutUnsupportedBindingReason(binding);
    if (!reason.isEmpty()) {
        host.shortcutProblem = reason;
        return;
    }
    QString error;
    if (!host.controller->setGlobalShortcut(binding, &error)) {
        host.shortcutProblem =
            error.isEmpty() ? QStringLiteral("That key could not be bound.") : error;
        return;
    }
    host.shortcutProblem.clear();
    host.shortcutNotice = singleKeyTypingWarning(binding);
}

} // namespace

// The Qt key a Windows virtual key stands for. Qt's enum uses the unshifted
// character for every printable key the binder accepts, so the binder's own
// mapping stays the only list of what Windows can register.
int ShortcutRecorder::qtKeyForVirtualKey(int virtualKey)
{
    if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
        return Qt::Key_F1 + (virtualKey - VK_F1);
    }
    switch (virtualKey) {
    case VK_SPACE:
        return Qt::Key_Space;
    case VK_RETURN:
        return Qt::Key_Return;
    case VK_TAB:
        return Qt::Key_Tab;
    default:
        break;
    }
    const UINT character = MapVirtualKeyW(static_cast<UINT>(virtualKey), MAPVK_VK_TO_CHAR);
    if ((character & 0xFFFF) < 0x20) {
        return 0;
    }
    return QChar(static_cast<char16_t>(character & 0xFFFF)).toUpper().unicode();
}

bool ShortcutRecorder::isModifierKey(int virtualKey)
{
    switch (virtualKey) {
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    default:
        return false;
    }
}

Qt::KeyboardModifiers ShortcutRecorder::heldModifiers()
{
    Qt::KeyboardModifiers modifiers;
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        modifiers |= Qt::ControlModifier;
    }
    if (GetKeyState(VK_MENU) & 0x8000) {
        modifiers |= Qt::AltModifier;
    }
    if (GetKeyState(VK_SHIFT) & 0x8000) {
        modifiers |= Qt::ShiftModifier;
    }
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) {
        modifiers |= Qt::MetaModifier;
    }
    return modifiers;
}

void ShortcutRecorder::setRecording(PaneHost &host, bool recording)
{
    if (host.shortcutRecording == recording || !host.controller) {
        return;
    }
    host.shortcutRecording = recording;
    host.shortcutPendingModifier = 0;
    if (recording) {
        host.controller->suspendGlobalShortcut();
        return;
    }
    const QString error = host.controller->resumeGlobalShortcut();
    if (!error.isEmpty()) {
        host.shortcutProblem = error;
    }
}

void ShortcutRecorder::appendPane(const StackPanel &column, PaneHost &host)
{
    column.Children().Append([] {
        TextBlock header;
        header.Style(Application::Current()
                         .Resources()
                         .Lookup(box_value(L"SettingsSectionHeaderStyle"))
                         .as<Style>());
        header.Text(L"Shortcut");
        return header;
    }());

    const QString display = host.controller->globalShortcut().displayText();
    Button recorder;
    recorder.Content(box_value(host.shortcutRecording
                                   ? hstring(L"Press a key or key combination…")
                                   : hs(display.isEmpty() ? QStringLiteral("Set shortcut")
                                                          : display)));
    recorder.MinWidth(120);
    recorder.IsEnabled(host.controller->globalShortcutsSupported());
    recorder.Click([&host](const auto &, const auto &) {
        host.shortcutProblem.clear();
        host.shortcutNotice.clear();
        setRecording(host, !host.shortcutRecording);
        host.refresh();
    });
    // The pane is rebuilt to arm the recorder; without focus in the rebuilt
    // subtree the PreviewKeyDown below would never see a key.
    if (host.shortcutRecording) {
        recorder.Loaded([](const IInspectable &sender, const auto &) {
            sender.as<Button>().Focus(FocusState::Programmatic);
        });
    }

    Button reset;
    reset.Content(box_value(L"Reset to Ctrl+Alt+D"));
    reset.IsEnabled(host.controller->globalShortcutsSupported());
    reset.Click([&host](const auto &, const auto &) {
        QString error;
        setRecording(host, false);
        if (host.controller->setGlobalShortcut(WinGlobalShortcutBinder::defaultShortcut(), &error)) {
            host.shortcutProblem.clear();
            host.shortcutNotice.clear();
        } else {
            host.shortcutProblem = error.isEmpty()
                ? QStringLiteral("That shortcut could not be bound.")
                : error;
        }
        host.refresh();
    });

    RowSnapshot recorderRow;
    recorderRow.id = QStringLiteral("shortcutRecorder");
    recorderRow.label = QStringLiteral("Dictation shortcut");
    recorderRow.help = QStringLiteral(
        "Press a key combination, or a single key such as Right Alt or F13.");
    RowSnapshot resetRow;
    resetRow.id = QStringLiteral("shortcutReset");
    resetRow.label = QStringLiteral("Reset");
    resetRow.help = QStringLiteral("Go back to the default shortcut.");

    StackPanel rows;
    rows.Children().Append(rowGrid(recorderRow, recorder, host, false));
    rows.Children().Append(rowGrid(resetRow, reset, host, true));
    StackPanel cards;
    cards.Spacing(4);
    cards.Margin({0, 0, 0, 0});
    cards.Children().Append(cardContainer(rows));
    column.Children().Append(cards);

    // The binder's refusal, what recording is waiting for, or the typing cost
    // of a single key that did save.
    if (!host.shortcutProblem.isEmpty() || !host.shortcutNotice.isEmpty()
        || host.shortcutRecording) {
        InfoBar note;
        note.IsClosable(false);
        note.IsOpen(true);
        note.Margin({0, 8, 0, 0});
        if (!host.shortcutProblem.isEmpty()) {
            note.Severity(InfoBarSeverity::Error);
            note.Message(hs(host.shortcutProblem));
        } else if (host.shortcutRecording) {
            note.Severity(InfoBarSeverity::Informational);
            note.Message(hstring(L"Press a key combination, a bare modifier such as "
                                 L"Right Alt, or Escape to keep the current one."));
        } else {
            note.Severity(InfoBarSeverity::Informational);
            note.Message(hs(host.shortcutNotice));
        }
        column.Children().Append(note);
    }

    // The keys arrive on the pane rather than the button, so moving focus
    // cannot end the recording early. Escape abandons it rather than becoming
    // the shortcut — so Escape itself is not recordable as a single key, like
    // the mac recorder.
    column.PreviewKeyDown([&host](const IInspectable &, const Input::KeyRoutedEventArgs &args) {
        if (!host.shortcutRecording) {
            return;
        }
        args.Handled(true);
        if (args.Key() == VirtualKey::Escape) {
            setRecording(host, false);
            host.refresh();
            return;
        }
        const auto keyStatus = args.KeyStatus();
        if (keyStatus.WasKeyDown) {
            // A held key auto-repeats; only the first press counts.
            return;
        }
        const int scanCode = int(keyStatus.ScanCode) | (keyStatus.IsExtendedKey ? 0xE000 : 0);
        if (isModifierKey(static_cast<int>(args.Key()))) {
            // A lone modifier commits on its release below; a second one makes
            // a modifier-only chord, which is not a valid combination. The
            // exception is AltGr, which Windows delivers as a synthetic Left
            // Ctrl press followed by Right Alt: that pair is one physical key,
            // so Right Alt stays capturable on AltGr layouts.
            const bool altGr = host.shortcutPendingModifier == 0x1D && scanCode == 0xE038;
            host.shortcutPendingModifier =
                host.shortcutPendingModifier == 0 || altGr ? scanCode : -1;
            return;
        }
        if (heldModifiers() != Qt::NoModifier) {
            setRecording(host, false);
            bind(host, args.Key());
            host.refresh();
            return;
        }
        // A bare key with no vocabulary row cannot be a dictation key; stay
        // armed so another key can be tried.
        if (!physicalKeyForWin(scanCode)) {
            host.shortcutProblem = QStringLiteral("That key cannot be a dictation key.");
            host.refresh();
            return;
        }
        setRecording(host, false);
        bindSingleKey(host, scanCode);
        host.refresh();
    });

    column.PreviewKeyUp([&host](const IInspectable &, const Input::KeyRoutedEventArgs &args) {
        if (!host.shortcutRecording) {
            return;
        }
        args.Handled(true);
        const auto keyStatus = args.KeyStatus();
        const int scanCode = int(keyStatus.ScanCode) | (keyStatus.IsExtendedKey ? 0xE000 : 0);
        if (host.shortcutPendingModifier == scanCode) {
            setRecording(host, false);
            bindSingleKey(host, scanCode);
            host.refresh();
            return;
        }
        // Once every modifier is up an abandoned or chorded press is over; the
        // next lone modifier can record again.
        if (heldModifiers() == Qt::NoModifier) {
            host.shortcutPendingModifier = 0;
        }
    });
}

} // namespace speecher::win
