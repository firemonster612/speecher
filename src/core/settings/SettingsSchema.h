#pragma once

#include "core/AppSettings.h"

#include <QList>
#include <QString>
#include <QVariant>

#include <functional>

namespace speecher {

enum class RowKind {
    Choice,
    Toggle,
    Text,
    Number,
    Action,
    Info,
    Custom,
};

struct RowOption {
    QString id;
    QString label;
    QString help;
    bool enabled = true;
};

struct NumberRange {
    int minimum = 0;
    int maximum = 0;
    int step = 1;
    QString suffix;
};

// What the machine Speecher is running on can do, for rows that are only
// meaningful when it can. Grows a member when a row needs one, not before.
struct Capabilities {
    bool targetAccessibility = false;
};

struct SettingsRow {
    // Stable across front ends: a renderer uses it to name its control, and a
    // Custom or Action row is recognised by it.
    QString id;
    QString label;
    QString help;
    RowKind kind = RowKind::Info;
    // The caption of an Action row's control, which is not its label.
    QString actionLabel;
    NumberRange range;
    // Room to reserve for a Choice row's value, in characters, so a list that
    // arrives late does not resize the row under the reader. Zero sizes the
    // control to whatever it holds.
    int contentWidthHint = 0;
    // Shown on the control itself, where help sits beneath the label.
    QString tooltip;
    // Replaces tooltip while enabled says no.
    QString disabledHelp;
    std::function<QVariant(const AppSettings &)> value;
    std::function<void(AppSettings &, const QVariant &)> apply;
    std::function<QList<RowOption>(const AppSettings &)> options;
    std::function<bool(const AppSettings &, const Capabilities &)> enabled;
    // Populating this row reaches for something slow — a device enumeration, a
    // keyring — so a front end leaves it until it has painted once.
    bool expensive = false;
};

struct SettingsSection {
    QString title;
    QString help;
    QList<SettingsRow> rows;
};

struct SettingsPage {
    QString id;
    QString title;
    QString iconName;   // freedesktop icon theme name
    QString symbolName; // SF Symbol name
    QList<SettingsSection> sections;
};

struct SettingsSchema {
    QList<SettingsPage> pages;

    const SettingsPage &page(const QString &id) const;
};

// What the descriptors need to be built. A value type, so a test can make one
// without a registry, a sound server or a window.
struct SchemaContext {
    QList<RowOption> speechProviders;
    QList<RowOption> refinementProviders;
    std::function<QList<RowOption>()> audioInputDevices;
    QString primaryOutputStatus;
};

SettingsSchema buildSettingsSchema(const SchemaContext &context);

// The microphone choice as it is offered: a system-default entry ahead of the
// devices that exist, and a disabled placeholder standing in for a saved device
// that has gone away. Shared with the setup assistant's own device list.
QList<RowOption> audioDeviceOptions(const QList<RowOption> &devices,
                                    const QString &selectedDeviceId);

} // namespace speecher
