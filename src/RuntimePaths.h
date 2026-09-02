#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

namespace RuntimePaths {

// Packaged assets live beside the executable. Resolve there first so launching
// Ridgeline from a shortcut, another application, or an unrelated working
// directory cannot make shaders and data files disappear.
inline QString resolveFromRoots(const QString& path,
                                const QString& applicationDir,
                                const QString& workingDir) {
    if (QFileInfo(path).isAbsolute()) return QDir::cleanPath(path);

    const QString packaged = QDir(applicationDir).filePath(path);
    if (QFileInfo::exists(packaged)) return QDir::cleanPath(packaged);

    const QString development = QDir(workingDir).filePath(path);
    if (QFileInfo::exists(development)) return QDir::cleanPath(development);

    // Prefer a useful packaged-path diagnostic when neither candidate exists.
    return QDir::cleanPath(packaged);
}

inline QString resolve(const QString& path) {
    return resolveFromRoots(path, QCoreApplication::applicationDirPath(),
                            QDir::currentPath());
}

} // namespace RuntimePaths
