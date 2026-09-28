#pragma once

#include "app/UpdateController.h"

#include <QObject>
#include <QString>

#include <optional>

namespace speecher {

class DictationSession;
enum class UpdateChannel;
struct SettingsSchema;

// What the update presentation is derived from: UpdateController's state and
// the dictation session, read at one moment.
struct UpdateBannerFacts {
    UpdateController::State state = UpdateController::State::Idle;
    // availableVersionDisplay(): a stable number, or a nightly's build and commit.
    QString version;
    int percent = 0;
    QString error;
    bool bannerVisible = false;
    bool repeatedCheckFailure = false;
    bool manualInstallRequired = false;
    bool stableReplacement = false;
    bool automaticDownloads = true;
    // A dictation is under way (anything but idle or error).
    bool dictating = false;
    // "Later" was chosen for the version on offer.
    bool deferred = false;
};

// What an update banner shows, the same for the dictation popup's chip and the
// settings window's banner on every platform. A front end maps each field to a
// native control and adds no wording of its own. The chip shows the text and
// the primary action; the window banner shows every field.
struct UpdateBannerModel {
    enum class Tone { Information, Positive, Error };

    bool visible = false;
    // The dictation popup's chip shows it too. A manual check's progress and
    // result answer a click in the settings window, so they stay there.
    bool showInPopup = false;
    Tone tone = Tone::Information;
    QString text;
    // 0 to 100 while downloading, otherwise -1.
    int progress = -1;
    // The primary button's caption; empty when the state has nothing to do.
    QString action;
    bool actionEnabled = true;
    // The captions of the secondary buttons, empty when not offered.
    QString later;
    QString dismiss;
};

UpdateBannerModel updateBannerModel(const UpdateBannerFacts &facts);

// The General page's "Check for updates" row. Its caption says what a click
// does in the current state.
struct UpdateCheckRow {
    QString caption;
    QString help;
    bool enabled = true;
};

UpdateCheckRow updateCheckRow(const UpdateBannerFacts &facts, UpdateChannel channel);

// The offer shown after an upgrade, in place of the update banner.
struct WhatsNewBannerModel {
    QString text;
    QString action;
    QString dismiss;
};

WhatsNewBannerModel whatsNewBanner(const QString &currentVersion);

// The update banner's live state. "Later" is remembered here, so hiding the
// banner in one place hides it everywhere until another version is offered.
class UpdateBanner : public QObject {
    Q_OBJECT

public:
    UpdateBanner(UpdateController *updates, DictationSession *session, QObject *parent = nullptr);

    UpdateBannerModel model() const;
    UpdateCheckRow checkRow(UpdateChannel channel) const;

public slots:
    void runAction();
    void later();
    void dismiss();
    // A click on the "Check for updates" row: the banner's action where the
    // row offers it, otherwise a check of this channel.
    void runCheckRow(UpdateChannel channel);

signals:
    void changed();

private:
    UpdateBannerFacts facts() const;

    UpdateController *m_updates;
    DictationSession *m_session;
    std::optional<QString> m_deferredVersion;
};

// Points the schema's checkForUpdates row at the banner's checkRow().
void bindCheckForUpdatesRow(SettingsSchema &schema, const UpdateBanner *banner);

} // namespace speecher
