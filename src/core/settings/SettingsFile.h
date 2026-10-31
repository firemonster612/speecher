#pragma once

#include <QSettings>
#include <QVariantMap>

#include <optional>

namespace speecher {

// The settings file one SettingsCodecs reads and writes, with the gate every
// write to disk passes: deleteFile deletes it and closes the gate. From then
// on changes stay in memory, so the running app reads what it set, and
// neither sync nor the save QSettings makes on its own after a change brings
// the file back. QSettings made after it keep their files in a throwaway
// folder, so a new reader can't save its unsaved changes either. Each save
// keeps the file and its folder from other users as far as it can
// (makeSettingsPrivate).
// Only the running app's store deletes its file: another SettingsFile's
// deleteFile, and its destructor's, would also erase what that store saved
// afterwards.
// Makes the settings' file 0600 and its folder 0700, making them when
// missing, so a key can go in with no other user able to read it: once the
// file is private, QSettings keeps it so whenever it rewrites it. False, with
// why in error, when the permissions won't change or don't read back as
// this user's alone, as for a folder another user owns. True with nothing to
// do outside Unix and for a macOS preferences domain.
bool makeSettingsPrivate(const QSettings &settings, QString *error);

class SettingsFile final : public QSettings {
public:
    SettingsFile(const QString &organization, const QString &application);
    ~SettingsFile() override;

    // save, then QSettings::sync, which also reads what another process
    // saved, unless deleteFile ran. Hides it, so code holding a SettingsFile
    // can't save around the gate.
    void sync();
    // Saves what changed now and says whether it saved: false once deleteFile
    // ran, or when the file won't save. status() can't say: it keeps the
    // first error QSettings meets, a malformed line it read included, for as
    // long as this object lives.
    bool save();
    // How many saves of this object's file went through: save, sync and the
    // save QSettings makes on its own after a change. Every change made
    // through this object before one is in the file. A save through another
    // QSettings on the file isn't counted.
    quint64 saves() const { return m_saves; }
    // Clears every setting, deletes the file and closes the gate. Returns
    // false when the file won't go. The destructor deletes it again, for
    // whatever another QSettings wrote meanwhile.
    bool deleteFile();
    // Closes the gate as deleteFile does, but keeps the file holding what
    // this object holds now, which the destructor writes back over any
    // change made since. QSettings made afterwards read the same.
    void closeKeepingFile();
    // Whether deleteFile or closeKeepingFile ran: nothing is saved since.
    bool deleted() const { return m_deleted; }

protected:
    bool event(QEvent *event) override;

private:
    void closeGate();

    bool m_deleted = false;
    // What closeKeepingFile kept, or nothing when the file was deleted.
    std::optional<QVariantMap> m_kept;
    quint64 m_saves = 0;
};

} // namespace speecher
