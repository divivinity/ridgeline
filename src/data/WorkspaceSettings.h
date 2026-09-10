#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <vector>

// Things the workspace should remember between runs: where you have been, and
// how you had the panels arranged.
//
// Deliberately separate from the scenario format. A scenario describes the RF
// problem and travels between machines; this is per-installation convenience
// and must never leak into a saved document.
//
// Honours RIDGELINE_STATE_DIR so tests and QA runs never touch the real
// profile, and so one run cannot inherit another's history.
namespace WorkspaceSettings {

struct RecentLocation {
    QString name;
    double latDeg = 0.0;
    double lonDeg = 0.0;
    double extentKm = 0.0;
};

// Most recent first, capped. A path or place that comes back is promoted
// rather than duplicated.
QStringList recentScenarios();
void addRecentScenario(const QString& path);

std::vector<RecentLocation> recentLocations();
void addRecentLocation(const RecentLocation& location);

// Panel arrangement and window size, as QMainWindow hands them over.
void saveLayout(const QByteArray& geometry, const QByteArray& state);
QByteArray layoutGeometry();
QByteArray layoutState();

// Forgets everything. Used by tests, and by anything that needs a clean slate.
void clear();

// How many entries each history keeps.
int maxRecentEntries();

} // namespace WorkspaceSettings
