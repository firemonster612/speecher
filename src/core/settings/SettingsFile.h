#pragma once

#include <QSettings>
#include <QVariantMap>

namespace speecher {

// The settings file one SettingsCodecs reads and writes, with the gate every
// write to disk passes: close closes the gate, deleting the file or keeping
// only what it is given. From then on changes stay in memory, so the running
// app reads what it set, and neither sync nor the save QSettings makes on
// its own after a change brings the rest of the file back. QSettings made
// after it keep their files in a throwaway folder, which holds what was kept
// too, so a new reader can't save its unsaved changes either. Each save
// keeps the file and its folder from other users as far as it can
// (makeSettingsPrivate).
// Only the running app's store closes its file: another SettingsFile's
// close, and its destructor's, would also erase what that store saved
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
    // saved, unless close ran. Hides it, so code holding a SettingsFile
    // can't save around the gate.
    void sync();
    // Saves what changed now and says whether it saved: false once close
    // ran, or when the file won't save. status() can't say: it keeps the
    // first error QSettings meets, a malformed line it read included, for as
    // long as this object lives.
    bool save();
    // How many saves of this object's file went through: save, sync and the
    // save QSettings makes on its own after a change. Every change made
    // through this object before one is in the file. A save through another
    // QSettings on the file isn't counted.
    quint64 saves() const { return m_saves; }
    // Closes the gate, leaving the file holding kept alone, which the
    // destructor writes again over any change made since, or deleting it
    // when nothing is kept. Returns false when the file won't go.
    bool close(const QVariantMap &kept);
    // Whether close ran: nothing is saved since.
    bool closed() const { return m_closed; }

protected:
    bool event(QEvent *event) override;

private:
    bool m_closed = false;
    QVariantMap m_kept;
    quint64 m_saves = 0;
};

} // namespace speecher
