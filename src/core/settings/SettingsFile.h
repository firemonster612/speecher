#pragma once

#include <QSettings>

namespace speecher {

// The settings file one SettingsCodecs reads and writes, with the gate every
// write to disk passes: deleteFile deletes it and closes the gate. From then
// on changes stay in memory, so the running app reads what it set, and
// neither sync nor the save QSettings makes on its own after a change brings
// the file back. QSettings made after it keep their files in a throwaway
// folder, so a new reader can't save its unsaved changes either; ones made
// before on the same file (a shortcut binder's) still could, and none writes
// after a removal.
class SettingsFile final : public QSettings {
public:
    SettingsFile(const QString &organization, const QString &application);
    ~SettingsFile() override;

    // QSettings::sync, unless deleteFile ran. Hides it, so code holding a
    // SettingsFile can't save around the gate.
    void sync();
    // Clears every setting, deletes the file and closes the gate. Returns
    // false when the file won't go. The destructor deletes it again, for
    // whatever another QSettings wrote meanwhile.
    bool deleteFile();
    bool deleted() const { return m_deleted; }

protected:
    bool event(QEvent *event) override;

private:
    bool m_deleted = false;
};

} // namespace speecher
