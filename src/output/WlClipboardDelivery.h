#pragma once

#include "output/ClipboardSnapshot.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace speecher {

struct DeliveryContent;
class WaylandClipboardOwner;

class WlClipboardDelivery : public QObject {
    Q_OBJECT

public:
    explicit WlClipboardDelivery(QObject *parent = nullptr);
    ~WlClipboardDelivery() override;
    bool copy(const DeliveryContent &content, bool *htmlAvailable = nullptr,
              QString *error = nullptr);
    static bool isAvailable();
    static bool isWaylandSession();
    static bool canSnapshot();
    static bool readText(QString *text, QString *error = nullptr);
    static QStringList snapshotMimeTypes(const QStringList &offeredMimeTypes);
    static bool capture(ClipboardSnapshot *snapshot, QString *error = nullptr);
    // Restores the snapshot unless something copied over our dictation in the
    // meantime; sets keptNewerCopy when it deliberately leaves that copy alone.
    // Consumes the record of what was copied: the question is answered once
    // per copy, and a later delivery that copies nothing owns no clipboard.
    bool restorePreservingNewCopy(const ClipboardSnapshot &snapshot, QString *error = nullptr,
                                  bool *keptNewerCopy = nullptr);
    // Whether the parts now on the clipboard are still the copy we published.
    static bool copyStillOnClipboard(const QList<ClipboardMimePart> &copied,
                                     const QList<ClipboardMimePart> &current);
    static bool restore(const ClipboardSnapshot &snapshot, QString *error = nullptr);

private:
    std::unique_ptr<WaylandClipboardOwner> m_owner;
    QList<ClipboardMimePart> m_copiedParts;
};

} // namespace speecher
