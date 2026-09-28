#include "frontend/win/ShortcutRecorder.h"

#include "app/ApplicationController.h"
#include "core/ShortcutBinding.h"
#include "platform/win/WinGlobalShortcutBinder.h"

#include <QDebug>
#include <QKeySequence>
#include <QStringList>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
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

// What a captured key means as a binding, or why it cannot be one.
struct Capture {
    ShortcutBinding binding;
    QString problem;
    // A caution that does not stop saving, such as a single key that types.
    QString notice;
};

Capture combinationCapture(PaneHost &host, const VirtualKey key)
{
    const int qtKey = ShortcutRecorder::qtKeyForVirtualKey(static_cast<int>(key));
    if (qtKey == 0) {
        return {{}, QStringLiteral("That key cannot be part of a shortcut."), {}};
    }
    const Qt::KeyboardModifiers modifiers = ShortcutRecorder::heldModifiers();
    const ShortcutBinding binding(QKeySequence(QKeyCombination(modifiers, Qt::Key(qtKey))));
    const QString reason = host.controller->globalShortcutUnsupportedBindingReason(binding);
    return reason.isEmpty() ? Capture{binding, {}, {}} : Capture{{}, reason, {}};
}

// The physical key, not the layout's meaning of it: the scancode plus the
// extended byte is the vocabulary's win column, so bare modifiers record and
// left is told from right. Saving is not gated on the typing warning.
Capture singleKeyCapture(PaneHost &host, int scanCode)
{
    const PhysicalKey *key = physicalKeyForWin(scanCode);
    if (!key) {
        return {{}, QStringLiteral("That key cannot be a dictation key."), {}};
    }
    const ShortcutBinding binding = ShortcutBinding::singleKey(QString::fromLatin1(key->code));
    const QString reason = host.controller->globalShortcutUnsupportedBindingReason(binding);
    if (!reason.isEmpty()) {
        return {{}, reason, {}};
    }
    return {binding, {}, singleKeyTypingWarning(binding)};
}

// The dialog's key capture, shared by its key handlers and its buttons.
struct Recording {
    Capture capture;
    // The scancode of the one modifier held so far, which its release
    // captures as a single key; -1 once a second key joined it, since a
    // modifier-only chord is not a combination.
    int pendingModifier = 0;
    // A key other than a modifier was pressed while modifiers were held, so
    // the capture, not the held modifiers, is what to show.
    bool chordKeyed = false;
};

// The modifiers being held, as the start of a combination: "Ctrl+Shift+…".
QString heldModifiersText()
{
    const Qt::KeyboardModifiers modifiers = ShortcutRecorder::heldModifiers();
    QStringList names;
    if (modifiers & Qt::ControlModifier) {
        names << QStringLiteral("Ctrl");
    }
    if (modifiers & Qt::AltModifier) {
        names << QStringLiteral("Alt");
    }
    if (modifiers & Qt::ShiftModifier) {
        names << QStringLiteral("Shift");
    }
    if (modifiers & Qt::MetaModifier) {
        names << QStringLiteral("Win");
    }
    return names.isEmpty() ? QString() : names.join(QLatin1Char('+')) + QStringLiteral("+…");
}

// Keys the capture box leaves to the dialog: Escape is Cancel, a bare Tab
// moves to the buttons and a bare Enter presses the default one. With a
// modifier held, Tab and Enter are keys like any other.
bool leftToDialog(const VirtualKey key)
{
    if (key == VirtualKey::Escape) {
        return true;
    }
    return (key == VirtualKey::Tab || key == VirtualKey::Enter)
        && ShortcutRecorder::heldModifiers() == Qt::NoModifier;
}

// Records the next shortcut in a modal dialog: the hotkey is suspended while it
// is open and given back when it closes, whichever way that happens.
void showRecorderDialog(PaneHost &host, const QString &title, const std::function<void()> &changed)
{
    if (host.shortcutRecording || !host.xamlRoot || !host.xamlRoot()) {
        return;
    }
    host.shortcutProblem.clear();
    host.shortcutNotice.clear();
    ShortcutRecorder::setRecording(host, true);

    auto recording = std::make_shared<Recording>();
    ContentDialog dialog;
    dialog.XamlRoot(host.xamlRoot());
    // The dialog opens in the popup layer, outside the window's RequestedTheme.
    if (host.effectiveTheme) {
        dialog.RequestedTheme(host.effectiveTheme());
    }
    dialog.Title(box_value(hs(title)));
    dialog.PrimaryButtonText(L"Save");
    dialog.SecondaryButtonText(L"Reset to Ctrl+Alt+D");
    dialog.CloseButtonText(L"Cancel");
    dialog.DefaultButton(ContentDialogButton::Primary);
    dialog.IsPrimaryButtonEnabled(false);

    StackPanel content;
    content.Spacing(12);
    content.MinWidth(360);
    content.Children().Append(styledTextBlock(
        QStringLiteral("Press the keys you want: a combination, or a single key such as "
                       "Right Alt or F13."),
        L"BodyTextBlockStyle"));
    // The one focusable thing in the content, and the only place keys are
    // recorded, so the buttons keep their own Enter and Space and the default
    // button keeps its accent. It takes focus when the dialog opens.
    TextBox keys;
    keys.IsReadOnly(true);
    keys.Text(L"Waiting for keys…");
    Automation::AutomationProperties::SetName(keys, L"Shortcut keys");
    content.Children().Append(keys);
    TextBlock note = secondaryTextBlock(QString(), L"CaptionTextBlockStyle", host);
    note.TextWrapping(TextWrapping::Wrap);
    content.Children().Append(note);
    dialog.Content(content);
    dialog.Opened([keys = make_weak(keys)](const auto &, const auto &) {
        if (const auto box = keys.get()) {
            box.Focus(FocusState::Programmatic);
        }
    });

    // Weak, because the dialog these handlers live on holds all three.
    const auto show = [recording, keys = make_weak(keys), note = make_weak(note),
                       dialog = make_weak(dialog)] {
        const Capture &capture = recording->capture;
        const QString held = recording->chordKeyed ? QString() : heldModifiersText();
        if (const auto box = keys.get()) {
            box.Text(hs(!held.isEmpty() ? held
                        : capture.binding.isEmpty() ? QStringLiteral("Waiting for keys…")
                                                    : capture.binding.displayText()));
        }
        if (const auto text = note.get()) {
            text.Text(hs(capture.problem.isEmpty() ? capture.notice : capture.problem));
        }
        if (const auto owner = dialog.get()) {
            owner.IsPrimaryButtonEnabled(!capture.binding.isEmpty());
        }
    };

    // Escape is not recordable as a single key, like the mac recorder.
    keys.PreviewKeyDown([&host, recording, show](const IInspectable &,
                                                 const Input::KeyRoutedEventArgs &args) {
        if (leftToDialog(args.Key())) {
            return;
        }
        args.Handled(true);
        const auto keyStatus = args.KeyStatus();
        if (keyStatus.WasKeyDown) {
            // A held key auto-repeats; only the first press counts.
            return;
        }
        const int scanCode = int(keyStatus.ScanCode) | (keyStatus.IsExtendedKey ? 0xE000 : 0);
        if (ShortcutRecorder::isModifierKey(static_cast<int>(args.Key()))) {
            // AltGr arrives as a synthetic Left Ctrl press followed by Right
            // Alt; that pair is one physical key, so Right Alt stays capturable
            // on AltGr layouts.
            const bool altGr = recording->pendingModifier == 0x1D && scanCode == 0xE038;
            recording->pendingModifier = recording->pendingModifier == 0 || altGr ? scanCode : -1;
            show();
            return;
        }
        recording->pendingModifier = -1;
        const bool combination = ShortcutRecorder::heldModifiers() != Qt::NoModifier;
        recording->chordKeyed = combination;
        recording->capture = combination ? combinationCapture(host, args.Key())
                                         : singleKeyCapture(host, scanCode);
        show();
    });
    keys.PreviewKeyUp([&host, recording, show](const IInspectable &,
                                               const Input::KeyRoutedEventArgs &args) {
        if (leftToDialog(args.Key())) {
            return;
        }
        args.Handled(true);
        const auto keyStatus = args.KeyStatus();
        const int scanCode = int(keyStatus.ScanCode) | (keyStatus.IsExtendedKey ? 0xE000 : 0);
        if (recording->pendingModifier == scanCode) {
            recording->capture = singleKeyCapture(host, scanCode);
        }
        if (ShortcutRecorder::heldModifiers() == Qt::NoModifier) {
            recording->pendingModifier = 0;
            recording->chordKeyed = false;
        }
        show();
    });
    // The one place a recording ends. Saving applies the captured binding,
    // Reset the default one; Cancel, Escape and the window closing apply none.
    // The binding changes before the hotkey comes back, so what comes back,
    // and any error it reports, is the binding now in force.
    dialog.Closed([&host, recording, changed, weak = std::weak_ptr<bool>(host.alive)](
                      const ContentDialog &, const ContentDialogClosedEventArgs &args) {
        if (gone(weak)) {
            return;
        }
        ShortcutBinding chosen;
        if (args.Result() == ContentDialogResult::Primary) {
            chosen = recording->capture.binding;
            host.shortcutNotice = recording->capture.notice;
        } else if (args.Result() == ContentDialogResult::Secondary) {
            chosen = WinGlobalShortcutBinder::defaultShortcut();
        }
        if (!chosen.isEmpty()) {
            QString error;
            if (!host.controller->setGlobalShortcut(chosen, &error)) {
                host.shortcutNotice.clear();
                host.shortcutProblem = error.isEmpty() ? QStringLiteral("That shortcut could not be bound.")
                                                       : error;
            }
        }
        ShortcutRecorder::setRecording(host, false);
        changed();
    });
    try {
        dialog.ShowAsync();
    } catch (const hresult_error &error) {
        // Another dialog is already open, say: nothing was recorded, so the
        // hotkey comes straight back.
        qWarning() << "Global Shortcut dialog failed to open:"
                   << QString::fromWCharArray(error.message().c_str());
        ShortcutRecorder::setRecording(host, false);
    }
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
    if (recording) {
        host.controller->suspendGlobalShortcut();
        return;
    }
    const QString error = host.controller->resumeGlobalShortcut();
    // A binding the dialog could not apply says more than the old one failing
    // to come back.
    if (!error.isEmpty() && host.shortcutProblem.isEmpty()) {
        host.shortcutProblem = error;
    }
}

StackPanel ShortcutRecorder::element(const RowSnapshot &row, PaneHost &host)
{
    StackPanel column;
    const auto shortcutText = [&host] {
        const QString display = host.controller->globalShortcut().displayText();
        return display.isEmpty() ? QStringLiteral("None") : display;
    };
    TextBlock current = secondaryTextBlock(shortcutText(), L"SettingsInfoTextStyle", host);
    InfoBar note;
    note.IsClosable(false);
    note.Margin({16, 0, 16, 12});
    // After the dialog, what it left behind: the binder's refusal, or the
    // typing cost of a single key that did save. Updated in place, and weakly,
    // because the row these handlers live on holds both controls.
    const auto showNote = [&host, note = make_weak(note)] {
        const auto bar = note.get();
        if (!bar) {
            return;
        }
        const bool problem = !host.shortcutProblem.isEmpty();
        bar.Severity(problem ? InfoBarSeverity::Error : InfoBarSeverity::Informational);
        bar.Message(hs(problem ? host.shortcutProblem : host.shortcutNotice));
        bar.IsOpen(problem || !host.shortcutNotice.isEmpty());
    };

    Button set;
    set.Content(box_value(L"Set shortcut…"));
    set.IsEnabled(host.controller->globalShortcutsSupported());
    set.Click([&host, title = row.label, current = make_weak(current), shortcutText,
               showNote](const auto &, const auto &) {
        showRecorderDialog(host, title, [&host, current, shortcutText, showNote] {
            // A rebuild while the dialog was open (a theme flip, news from
            // LocalSetup) replaced this row with one built before the change,
            // so the page is built again to show it.
            const auto text = current.get();
            if (!text || !text.IsLoaded()) {
                host.refresh();
                return;
            }
            text.Text(hs(shortcutText()));
            showNote();
        });
    });
    StackPanel control;
    control.Orientation(Orientation::Horizontal);
    control.Spacing(12);
    current.VerticalAlignment(VerticalAlignment::Center);
    control.Children().Append(current);
    control.Children().Append(set);

    RowSnapshot recorderRow;
    recorderRow.id = QStringLiteral("shortcutRecorder");
    recorderRow.label = QStringLiteral("Dictation key");
    recorderRow.help = row.help;
    column.Children().Append(rowGrid(recorderRow, control, host, false));
    showNote();
    column.Children().Append(note);
    return column;
}

} // namespace speecher::win
