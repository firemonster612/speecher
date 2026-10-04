#include "frontend/win/ShortcutRecorder.h"

#include "app/ApplicationController.h"
#include "core/ShortcutBinding.h"
#include "core/settings/SettingsSchema.h"
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

Capture combinationCapture(PaneHost &host, const VirtualKey key, GlobalShortcutRole role)
{
    const int qtKey = ShortcutRecorder::qtKeyForVirtualKey(static_cast<int>(key));
    if (qtKey == 0) {
        return {{}, QStringLiteral("That key cannot be part of a shortcut."), {}};
    }
    const Qt::KeyboardModifiers modifiers = ShortcutRecorder::heldModifiers();
    const ShortcutBinding binding(QKeySequence(QKeyCombination(modifiers, Qt::Key(qtKey))));
    const QString reason = host.controller->globalShortcutUnsupportedBindingReason(binding, role);
    return reason.isEmpty() ? Capture{binding, {}, {}} : Capture{{}, reason, {}};
}

// The physical key, not the layout's meaning of it: the scancode plus the
// extended byte is the vocabulary's win column, so bare modifiers record and
// left is told from right. Saving is not gated on the typing warning.
Capture singleKeyCapture(PaneHost &host, int scanCode, GlobalShortcutRole role)
{
    const PhysicalKey *key = physicalKeyForWin(scanCode);
    if (!key) {
        return {{}, QStringLiteral("That key cannot be a dictation key."), {}};
    }
    const ShortcutBinding binding = ShortcutBinding::singleKey(QString::fromLatin1(key->code));
    const QString reason = host.controller->globalShortcutUnsupportedBindingReason(binding, role);
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
// modifier held, Tab and Enter are keys like any other. Escape is the key
// people reach for to cancel, so the Cancel and Pause recorders take it as a
// shortcut; the dialog's Cancel button still abandons.
bool leftToDialog(const VirtualKey key, GlobalShortcutRole role)
{
    if (key == VirtualKey::Escape) {
        return role == GlobalShortcutRole::Dictation;
    }
    return (key == VirtualKey::Tab || key == VirtualKey::Enter)
        && ShortcutRecorder::heldModifiers() == Qt::NoModifier;
}

// Binds `binding`, saying in host.shortcutProblem why when the binder refuses.
void bind(PaneHost &host, const ShortcutBinding &binding,
          GlobalShortcutRole role = GlobalShortcutRole::Dictation)
{
    host.shortcutNoteRole = role;
    QString error;
    if (!host.controller->setGlobalShortcut(binding, &error, role)) {
        host.shortcutNotice.clear();
        host.shortcutProblem = error.isEmpty() ? globalShortcutBindFailedText() : error;
    }
}

} // namespace

void ShortcutRecorder::record(PaneHost &host, const QString &title, std::function<void()> changed,
                              GlobalShortcutRole role)
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
    dialog.CloseButtonText(L"Cancel");
    dialog.DefaultButton(ContentDialogButton::Primary);
    dialog.IsPrimaryButtonEnabled(false);

    StackPanel content;
    content.Spacing(12);
    content.MinWidth(360);
    content.Children().Append(styledTextBlock(role == GlobalShortcutRole::Dictation
                                                  ? globalShortcutPrompt()
                                                  : sessionShortcutPrompt(),
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

    keys.PreviewKeyDown([&host, recording, show, role](const IInspectable &,
                                                 const Input::KeyRoutedEventArgs &args) {
        if (leftToDialog(args.Key(), role)) {
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
        // The Cancel and Pause Shortcuts hold their hot key only while
        // dictating, so a bare key such as C or Escape is a combination
        // without modifiers there and types as usual the rest of the time. A
        // key no hot key can name stays a single key, which the binder vets.
        // Keypad keys stay single keys for these two, modifiers or not: a hot
        // key named by their character or navigation key would take the
        // main key instead. The scancode tells them apart whatever Num Lock
        // says: 7 to . without the E0 prefix, *, and Enter and / with it.
        const int virtualKey = static_cast<int>(args.Key());
        const bool keypad = (scanCode >= 0x47 && scanCode <= 0x53) || scanCode == 0x37
            || scanCode == 0xE01C || scanCode == 0xE035;
        const bool session = role != GlobalShortcutRole::Dictation;
        const bool combination = !(session && keypad)
            && (ShortcutRecorder::heldModifiers() != Qt::NoModifier
                || (session && ShortcutRecorder::qtKeyForVirtualKey(virtualKey) != 0));
        recording->chordKeyed = combination;
        recording->capture = combination ? combinationCapture(host, args.Key(), role)
                                         : singleKeyCapture(host, scanCode, role);
        show();
    });
    keys.PreviewKeyUp([&host, recording, show, role](const IInspectable &,
                                               const Input::KeyRoutedEventArgs &args) {
        if (leftToDialog(args.Key(), role)) {
            return;
        }
        args.Handled(true);
        const auto keyStatus = args.KeyStatus();
        const int scanCode = int(keyStatus.ScanCode) | (keyStatus.IsExtendedKey ? 0xE000 : 0);
        if (recording->pendingModifier == scanCode) {
            recording->capture = singleKeyCapture(host, scanCode, role);
        }
        if (ShortcutRecorder::heldModifiers() == Qt::NoModifier) {
            recording->pendingModifier = 0;
            recording->chordKeyed = false;
        }
        show();
    });
    // The one place a recording ends. Saving applies the captured binding;
    // Cancel, Escape and the window closing apply none. The binding changes
    // before the hotkey comes back, so what comes back, and any error it
    // reports, is the binding now in force.
    dialog.Closed([&host, recording, changed, role, weak = std::weak_ptr<bool>(host.alive)](
                      const ContentDialog &, const ContentDialogClosedEventArgs &args) {
        if (gone(weak)) {
            return;
        }
        if (args.Result() == ContentDialogResult::Primary) {
            host.shortcutNotice = recording->capture.notice;
            bind(host, recording->capture.binding, role);
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

void ShortcutRecorder::reset(PaneHost &host)
{
    host.shortcutProblem.clear();
    host.shortcutNotice.clear();
    bind(host, WinGlobalShortcutBinder::defaultShortcut());
}

// The Qt key a Windows virtual key stands for, read from the binder's own
// mapping, so it stays the only list of what Windows can register.
int ShortcutRecorder::qtKeyForVirtualKey(int virtualKey)
{
    const QKeySequence keys =
        WinGlobalShortcutBinder::keySequenceForHotKey(0, static_cast<quint32>(virtualKey));
    return keys.isEmpty() ? 0 : keys[0].key();
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
    GlobalShortcutRole failedRole = GlobalShortcutRole::Dictation;
    const QString error = host.controller->resumeGlobalShortcut(&failedRole);
    // A binding the dialog could not apply says more than the old one failing
    // to come back.
    if (!error.isEmpty() && host.shortcutProblem.isEmpty()) {
        host.shortcutProblem = error;
        host.shortcutNoteRole = failedRole;
    }
}

StackPanel ShortcutRecorder::element(const RowSnapshot &row, PaneHost &host)
{
    const GlobalShortcutRole role =
        globalShortcutRoleForRow(row.id).value_or(GlobalShortcutRole::Dictation);
    const ShortcutBinding bound = host.controller->globalShortcut(role);
    const QString current = bound.displayText();
    const ShortcutBinding standard = WinGlobalShortcutBinder::defaultShortcut();
    StackPanel control;
    control.Orientation(Orientation::Horizontal);
    control.Spacing(8);
    TextBlock binding = secondaryTextBlock(current.isEmpty() ? globalShortcutUnsetText() : current,
                                           L"SettingsInfoTextStyle", host);
    binding.VerticalAlignment(VerticalAlignment::Center);
    binding.Margin({0, 0, 4, 0});
    control.Children().Append(binding);
    const bool supported = host.controller->globalShortcutsSupported(role);
    Button change;
    change.Content(box_value(hs(globalShortcutChangeCaption())));
    change.IsEnabled(supported);
    change.Click([&host, title = row.label, role](const auto &, const auto &) {
        record(host, title, [&host] { host.refresh(); }, role);
    });
    control.Children().Append(change);
    if (role != GlobalShortcutRole::Dictation && !bound.isEmpty()) {
        Button clear;
        clear.Content(box_value(hs(globalShortcutClearCaption())));
        clear.Click([&host, role](const auto &, const auto &) {
            host.shortcutProblem.clear();
            host.shortcutNotice.clear();
            bind(host, ShortcutBinding(), role);
            host.refresh();
        });
        control.Children().Append(clear);
    } else if (role == GlobalShortcutRole::Dictation && bound != standard) {
        Button reset;
        reset.Content(box_value(hs(globalShortcutResetCaption(standard.displayText()))));
        reset.IsEnabled(supported);
        reset.Click([&host](const auto &, const auto &) {
            ShortcutRecorder::reset(host);
            host.refresh();
        });
        control.Children().Append(reset);
    }

    StackPanel column;
    column.Children().Append(rowGrid(row, control, host, false));
    // What the last change left behind: the binder's refusal, or the typing
    // cost of a single key that did save. Absent, not just closed, while there
    // is nothing to say, so it leaves no gap under the row.
    const bool problem = !host.shortcutProblem.isEmpty();
    if (host.shortcutNoteRole == role && (problem || !host.shortcutNotice.isEmpty())) {
        InfoBar note;
        note.IsClosable(false);
        note.Margin({16, 0, 16, 12});
        note.Severity(problem ? InfoBarSeverity::Error : InfoBarSeverity::Informational);
        note.Message(hs(problem ? host.shortcutProblem : host.shortcutNotice));
        note.IsOpen(true);
        column.Children().Append(note);
    }
    return column;
}

} // namespace speecher::win
