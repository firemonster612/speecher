#include "frontend/win/ShortcutRecorder.h"

#include "app/ApplicationController.h"

#include <QKeySequence>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
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

const QKeySequence kDefaultShortcut(Qt::CTRL | Qt::ALT | Qt::Key_D);

// The Qt key a Windows virtual key stands for. Qt's enum uses the unshifted
// character for every printable key the binder accepts, so the binder's own
// table stays the only list of what Windows can register.
int qtKeyForVirtualKey(int virtualKey)
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
    if (character == 0) {
        return 0;
    }
    return QChar(static_cast<char16_t>(character & 0xFFFF)).toUpper().unicode();
}

bool isModifierKey(VirtualKey key)
{
    switch (key) {
    case VirtualKey::Control:
    case VirtualKey::LeftControl:
    case VirtualKey::RightControl:
    case VirtualKey::Menu:
    case VirtualKey::LeftMenu:
    case VirtualKey::RightMenu:
    case VirtualKey::Shift:
    case VirtualKey::LeftShift:
    case VirtualKey::RightShift:
    case VirtualKey::LeftWindows:
    case VirtualKey::RightWindows:
        return true;
    default:
        return false;
    }
}

Qt::KeyboardModifiers heldModifiers()
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

void bind(PaneHost &host, const VirtualKey key)
{
    const int qtKey = qtKeyForVirtualKey(static_cast<int>(key));
    if (qtKey == 0) {
        host.shortcutProblem = QStringLiteral("That key cannot be part of a shortcut.");
        return;
    }
    const Qt::KeyboardModifiers modifiers = heldModifiers();
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
        return;
    }
    host.shortcutProblem =
        error.isEmpty() ? QStringLiteral("That shortcut could not be bound.") : error;
}

} // namespace

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

    const QString display = host.controller->globalShortcut().toString(QKeySequence::NativeText);
    Button recorder;
    recorder.Content(box_value(host.shortcutRecording
                                   ? hstring(L"Type a shortcut…")
                                   : hs(display.isEmpty() ? QStringLiteral("Record shortcut")
                                                          : display)));
    recorder.MinWidth(120);
    recorder.IsEnabled(host.controller->globalShortcutsSupported());
    recorder.Click([&host](const auto &, const auto &) {
        host.shortcutRecording = !host.shortcutRecording;
        host.shortcutProblem.clear();
        host.refresh();
    });

    Button reset;
    reset.Content(box_value(L"Reset to Ctrl+Alt+D"));
    reset.IsEnabled(host.controller->globalShortcutsSupported());
    reset.Click([&host](const auto &, const auto &) {
        QString error;
        host.shortcutRecording = false;
        if (host.controller->setGlobalShortcut(kDefaultShortcut, &error)) {
            host.shortcutProblem.clear();
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
    recorderRow.help = QStringLiteral("Hold it to dictate while it is down, or press and "
                                      "release to start and press again to stop.");
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

    // The binder's refusal, or what recording is waiting for.
    if (!host.shortcutProblem.isEmpty() || host.shortcutRecording) {
        InfoBar note;
        note.IsClosable(false);
        note.IsOpen(true);
        note.Margin({0, 8, 0, 0});
        if (!host.shortcutProblem.isEmpty()) {
            note.Severity(InfoBarSeverity::Error);
            note.Message(hs(host.shortcutProblem));
        } else {
            note.Severity(InfoBarSeverity::Informational);
            note.Message(L"Press the keys you want, or Escape to keep the current one.");
        }
        column.Children().Append(note);
    }

    // The chord arrives on the pane rather than the button, so moving focus
    // cannot end the recording early. Escape abandons it rather than becoming
    // the shortcut.
    column.PreviewKeyDown([&host](const IInspectable &, const Input::KeyRoutedEventArgs &args) {
        if (!host.shortcutRecording) {
            return;
        }
        if (isModifierKey(args.Key())) {
            return;
        }
        args.Handled(true);
        host.shortcutRecording = false;
        if (args.Key() != VirtualKey::Escape) {
            bind(host, args.Key());
        }
        host.refresh();
    });
}

} // namespace speecher::win
