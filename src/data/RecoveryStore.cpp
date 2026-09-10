#include "RecoveryStore.h"
#include "ScenarioIO.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
QString stateRoot() {
    // Tests and QA runs point this elsewhere so they never touch the real
    // profile, and so one run cannot inherit another's recovery file.
    const QString override = qEnvironmentVariable("RIDGELINE_STATE_DIR");
    if (!override.isEmpty()) return override;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}
}

QString RecoveryStore::path() {
    return QDir(stateRoot()).filePath("recovery/workspace.json");
}

bool RecoveryStore::exists() {
    const QFileInfo info(path());
    return info.exists() && info.size() > 0;
}

QDateTime RecoveryStore::savedAt() {
    return QFileInfo(path()).lastModified();
}

bool RecoveryStore::write(const ScenarioDoc& doc) {
    const QString target = path();
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) return false;
    try {
        // ScenarioIO writes atomically, so an interrupted autosave cannot leave
        // a truncated file that destroys the work it exists to protect.
        ScenarioIO::save(doc, target.toStdString());
        return true;
    } catch (const std::exception&) {
        // Autosave runs on a timer behind the user's work; a failure must not
        // interrupt them with a dialog. The next tick tries again.
        return false;
    }
}

ScenarioDoc RecoveryStore::read() {
    return ScenarioIO::load(path().toStdString());
}

void RecoveryStore::clear() {
    QFile::remove(path());
}
