#include "WorkspaceSettings.h"

#include <QDir>
#include <algorithm>
#include <cmath>
#include <QSettings>

namespace {
constexpr int kMaxRecent = 8;

// A file under RIDGELINE_STATE_DIR when set, so a test run is fully isolated;
// otherwise the platform's normal per-application settings.
QSettings store() {
    const QString override = qEnvironmentVariable("RIDGELINE_STATE_DIR");
    if (!override.isEmpty()) {
        QDir().mkpath(override);
        return QSettings(QDir(override).filePath("workspace.ini"), QSettings::IniFormat);
    }
    return QSettings();
}
}

int WorkspaceSettings::maxRecentEntries() { return kMaxRecent; }

QStringList WorkspaceSettings::recentScenarios() {
    return store().value("recent/scenarios").toStringList();
}

void WorkspaceSettings::addRecentScenario(const QString& path) {
    if (path.isEmpty()) return;
    QSettings settings = store();
    QStringList paths = settings.value("recent/scenarios").toStringList();
    // Coming back to a scenario promotes it rather than listing it twice.
    paths.removeAll(path);
    paths.prepend(path);
    while (paths.size() > kMaxRecent) paths.removeLast();
    settings.setValue("recent/scenarios", paths);
}

std::vector<WorkspaceSettings::RecentLocation> WorkspaceSettings::recentLocations() {
    QSettings settings = store();
    const int count = settings.beginReadArray("recent/locations");
    std::vector<RecentLocation> locations;
    locations.reserve(count);
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        RecentLocation location;
        location.name = settings.value("name").toString();
        location.latDeg = settings.value("lat").toDouble();
        location.lonDeg = settings.value("lon").toDouble();
        location.extentKm = settings.value("extentKm").toDouble();
        locations.push_back(location);
    }
    settings.endArray();
    return locations;
}

void WorkspaceSettings::addRecentLocation(const RecentLocation& location) {
    std::vector<RecentLocation> locations = recentLocations();
    // Same place to about a hundred metres is the same entry, so nudging the
    // map and reloading does not fill the list with near-duplicates.
    const auto same = [&location](const RecentLocation& other) {
        return std::abs(other.latDeg - location.latDeg) < 0.001 &&
               std::abs(other.lonDeg - location.lonDeg) < 0.001;
    };
    locations.erase(std::remove_if(locations.begin(), locations.end(), same),
                    locations.end());
    locations.insert(locations.begin(), location);
    if (static_cast<int>(locations.size()) > kMaxRecent)
        locations.resize(kMaxRecent);

    QSettings settings = store();
    settings.remove("recent/locations");
    settings.beginWriteArray("recent/locations", static_cast<int>(locations.size()));
    for (std::size_t i = 0; i < locations.size(); ++i) {
        settings.setArrayIndex(static_cast<int>(i));
        settings.setValue("name", locations[i].name);
        settings.setValue("lat", locations[i].latDeg);
        settings.setValue("lon", locations[i].lonDeg);
        settings.setValue("extentKm", locations[i].extentKm);
    }
    settings.endArray();
}

void WorkspaceSettings::saveLayout(const QByteArray& geometry, const QByteArray& state) {
    QSettings settings = store();
    settings.setValue("layout/geometry", geometry);
    settings.setValue("layout/state", state);
}

QByteArray WorkspaceSettings::layoutGeometry() {
    return store().value("layout/geometry").toByteArray();
}

QByteArray WorkspaceSettings::layoutState() {
    return store().value("layout/state").toByteArray();
}

void WorkspaceSettings::clear() {
    QSettings settings = store();
    settings.remove("recent");
    settings.remove("layout");
}
