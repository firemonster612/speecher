#pragma once

#include <QSettings>

namespace speecher {

// The settings file one SettingsCodecs reads and writes, with the gate every
// write to disk passes: deleteFile deletes it and closes the gate. From then
// on changes stay in memory, so the running app reads what it set, and
// neither sync nor the save QSettings makes on its own after a change brings
// the file back. QSettings made after it keep their files in a throwaway
// folder, so a new reader can't save its unsaved changes either. The file and
// its folder are the owner's alone, as they can hold the keys SecretStore
// keeps there when no keyring takes them.
// Only the running app's store deletes its file: another SettingsFile's
// deleteFile, and its destructor's, would also erase what that store saved
// afterwards.
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
    bool deleted() const { return m_deleted; }

protected:
    bool event(QEvent *event) override;

private:
    bool m_deleted = false;
    quint64 m_saves = 0;
};

} // namespace speecher
