#pragma once

#include "app/PhoneTransfer.h"

#include <QString>
#include <QStringList>

namespace speecher {

// How every front end words the "Copy settings to your phone" dialog
// (docs/adr/0008-phone-transfer-over-the-lan.md).
struct PhoneTransferText {
    QString title;
    // What to do on the phone, one step each; empty when there is no code.
    QStringList steps;
    QString includedHeading;
    QStringList included;
    // What has no counterpart on the phone; empty when there is none.
    QString staysHeading;
    QStringList stays;
    QString neverIncluded;
    QString status;
    QString close;
};

PhoneTransferText phoneTransferText(const AppSettings &settings, PhoneTransferState state);

} // namespace speecher
