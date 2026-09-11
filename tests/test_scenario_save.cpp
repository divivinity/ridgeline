#include "data/ScenarioIO.h"
#include "data/RecoveryStore.h"
#include "data/WorkspaceSettings.h"
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <cmath>
#include <iostream>
#include <stdexcept>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
QByteArray read(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "read saved fixture");
    return file.readAll();
}
int main() {
    try {
        QTemporaryDir dir;
        require(dir.isValid(), "temporary directory");
        const QString path = dir.filePath("scenario.json");
        ScenarioDoc doc;
        doc.calibrationSourceName = "original";
        ScenarioIO::save(doc, path.toStdString());
        const auto original = read(path);
        const auto entries = QDir(dir.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        doc.calibrationSourceName = std::string(1, char(0xff));
        bool failed = false;
        try { ScenarioIO::save(doc, path.toStdString()); } catch (const std::exception&) { failed = true; }
        require(failed && read(path) == original, "serialization failure preserves original bytes");
        doc.calibrationSourceName = "replacement";
#ifdef Q_OS_WIN
        const auto nativePath = path.toStdWString();
        HANDLE lock = CreateFileW(nativePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(lock != INVALID_HANDLE_VALUE, "lock fixture against replacement");
        failed = false;
        try { ScenarioIO::save(doc, path.toStdString()); } catch (const std::exception&) { failed = true; }
        CloseHandle(lock);
        require(failed && read(path) == original, "locked target failure preserves original bytes");
#endif
        require(QDir(dir.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot) == entries,
                "failed saves leave no temporary files");
        ScenarioIO::save(doc, path.toStdString());
        require(ScenarioIO::load(path.toStdString()).calibrationSourceName == "replacement", "replacement round trip");
        failed = false;
        try { ScenarioIO::save(doc, dir.filePath("missing/scenario.json").toStdString()); }
        catch (const std::exception&) { failed = true; }
        require(failed && ScenarioIO::load(path.toStdString()).calibrationSourceName == "replacement",
                "missing destination directory reports failure without affecting existing save");
        const QString unicodeDirectory = dir.filePath(QStringLiteral("\u5730\u5f62-\u00e9"));
        require(QDir().mkdir(unicodeDirectory), "create Unicode scenario directory");
        const QString unicodePath = QDir(unicodeDirectory).filePath(QStringLiteral("\u8a08\u753b.json"));
        ScenarioIO::save(doc, unicodePath.toStdString());
        require(QFile::exists(unicodePath), "save uses the exact Unicode path");
        require(ScenarioIO::load(unicodePath.toStdString()).calibrationSourceName == "replacement",
                "Unicode directory and filename round trip");
        {
            ScenarioDoc layered;
            layered.layers = {{"hillshade", true, 0.4f}, {"satellite", false, 1.0f}};
            const QString layerPath = dir.filePath("layers.json");
            ScenarioIO::save(layered, layerPath.toStdString());
            const ScenarioDoc back = ScenarioIO::load(layerPath.toStdString());
            require(back.layers.size() == 2, "layer state round-trips");
            require(back.layers[0].id == "hillshade" && back.layers[0].visible &&
                        std::abs(back.layers[0].opacity - 0.4f) < 1e-6f,
                    "layer fields round-trip in stack order");
            require(!back.layers[1].visible, "a hidden layer stays hidden");

            // Scenarios written before layers existed must still load.
            QFile legacy(layerPath);
            require(legacy.open(QIODevice::WriteOnly | QIODevice::Truncate),
                    "rewrite scenario without a layers key");
            legacy.write("{\"mode\":\"planner\"}");
            legacy.close();
            require(ScenarioIO::load(layerPath.toStdString()).layers.empty(),
                    "a scenario without a layers key loads with no layer state");
        }
        {
            // Recovery store: a crash-safe copy of unsaved work, kept outside
            // the process holding it.
            QTemporaryDir stateDir;
            require(stateDir.isValid(), "recovery state directory");
            qputenv("RIDGELINE_STATE_DIR", stateDir.path().toUtf8());

            require(!RecoveryStore::exists(), "a fresh profile has nothing to recover");

            ScenarioDoc unsaved;
            unsaved.calibrationSourceName = "unsaved work";
            unsaved.layers = {{"hillshade", true, 0.5f}};
            require(RecoveryStore::write(unsaved), "autosave writes a recovery copy");
            require(RecoveryStore::exists(), "the recovery copy is found afterwards");
            require(RecoveryStore::savedAt().isValid(), "the recovery copy is timestamped");

            const ScenarioDoc recovered = RecoveryStore::read();
            require(recovered.calibrationSourceName == "unsaved work",
                    "recovered work round-trips through the same format");
            require(recovered.layers.size() == 1 && recovered.layers[0].visible,
                    "recovered work keeps its layer state");

            // Writing again must replace, not accumulate.
            ScenarioDoc later;
            later.calibrationSourceName = "later work";
            require(RecoveryStore::write(later), "autosave overwrites the previous copy");
            require(RecoveryStore::read().calibrationSourceName == "later work",
                    "the newest autosave is the one recovered");

            // A stale copy would offer to restore work already dealt with.
            RecoveryStore::clear();
            require(!RecoveryStore::exists(), "clearing removes the recovery copy");
            RecoveryStore::clear();
            require(!RecoveryStore::exists(), "clearing twice is harmless");

            // Autosave runs on a timer behind the user's work, so an
            // unwritable location must fail quietly rather than throw.
            qputenv("RIDGELINE_STATE_DIR", (stateDir.path() + "/blocked").toUtf8());
            QFile blocker(stateDir.filePath("blocked"));
            require(blocker.open(QIODevice::WriteOnly), "reserve the recovery path as a file");
            blocker.write("not a directory");
            blocker.close();
            require(!RecoveryStore::write(unsaved),
                    "an unwritable recovery location reports failure instead of throwing");
            qunsetenv("RIDGELINE_STATE_DIR");
        }
        {
            // Workspace history and layout: per-installation convenience that
            // must never leak into a saved scenario.
            QTemporaryDir settingsDir;
            require(settingsDir.isValid(), "workspace settings directory");
            qputenv("RIDGELINE_STATE_DIR", settingsDir.path().toUtf8());
            WorkspaceSettings::clear();

            require(WorkspaceSettings::recentScenarios().isEmpty(),
                    "a fresh profile has no scenario history");

            WorkspaceSettings::addRecentScenario("/a/first.json");
            WorkspaceSettings::addRecentScenario("/a/second.json");
            require(WorkspaceSettings::recentScenarios().first() == "/a/second.json",
                    "the newest scenario is listed first");

            // Returning to a scenario promotes it rather than duplicating it.
            WorkspaceSettings::addRecentScenario("/a/first.json");
            const QStringList promoted = WorkspaceSettings::recentScenarios();
            require(promoted.size() == 2, "a repeat visit does not duplicate an entry");
            require(promoted.first() == "/a/first.json", "a repeat visit promotes the entry");

            for (int i = 0; i < 20; ++i)
                WorkspaceSettings::addRecentScenario(QString("/bulk/%1.json").arg(i));
            require(WorkspaceSettings::recentScenarios().size() == WorkspaceSettings::maxRecentEntries(),
                    "scenario history is capped");
            WorkspaceSettings::addRecentScenario("");
            require(WorkspaceSettings::recentScenarios().size() == WorkspaceSettings::maxRecentEntries(),
                    "an empty path is not recorded");

            WorkspaceSettings::addRecentLocation({"Asheville", 35.5951, -82.5515, 15.0});
            WorkspaceSettings::addRecentLocation({"Charlotte", 35.2271, -80.8431, 10.0});
            auto places = WorkspaceSettings::recentLocations();
            require(places.size() == 2 && places[0].name == "Charlotte",
                    "the newest location is listed first");
            require(std::abs(places[0].extentKm - 10.0) < 1e-9, "location extent round-trips");

            // Nudging the map and reloading must not fill the list with
            // near-duplicates of the same place.
            WorkspaceSettings::addRecentLocation({"Asheville again", 35.5955, -82.5512, 20.0});
            places = WorkspaceSettings::recentLocations();
            require(places.size() == 2, "a nearby reload replaces the same place");
            require(places[0].name == "Asheville again", "the newer visit wins");

            require(WorkspaceSettings::layoutGeometry().isEmpty(), "no layout is stored yet");
            WorkspaceSettings::saveLayout(QByteArray("geo"), QByteArray("state"));
            require(WorkspaceSettings::layoutGeometry() == QByteArray("geo"), "layout geometry persists");
            require(WorkspaceSettings::layoutState() == QByteArray("state"), "layout state persists");

            WorkspaceSettings::clear();
            require(WorkspaceSettings::recentScenarios().isEmpty() &&
                        WorkspaceSettings::recentLocations().empty() &&
                        WorkspaceSettings::layoutGeometry().isEmpty(),
                    "clearing forgets history and layout together");
            qunsetenv("RIDGELINE_STATE_DIR");
        }
        {
            // Vegetation areas persist as areas, not as the raster they make.
            ScenarioDoc vegetated;
            vegetated.vegetation = {
                {100.0f, 150.0f, 300.0f, 400.0f, 18.0f, "north wood"},
                {500.0f, 500.0f, 700.0f, 700.0f, 9.5f, "scrub"},
            };
            const QString vegPath = dir.filePath("vegetation.json");
            ScenarioIO::save(vegetated, vegPath.toStdString());
            const ScenarioDoc back = ScenarioIO::load(vegPath.toStdString());
            require(back.vegetation.size() == 2, "vegetation areas round-trip");
            require(back.vegetation[0].name == "north wood", "area names round-trip");
            require(std::abs(back.vegetation[0].maxZ - 400.0f) < 1e-4f,
                    "area bounds round-trip");
            require(std::abs(back.vegetation[1].canopyHeightM - 9.5f) < 1e-4f,
                    "canopy height round-trips");

            // A malformed area is dropped on read rather than reaching the UI.
            QFile malformed(vegPath);
            require(malformed.open(QIODevice::WriteOnly | QIODevice::Truncate),
                    "rewrite scenario with a malformed area");
            malformed.write("{\"vegetation\":[{\"minX\":200,\"maxX\":100,"
                            "\"minZ\":0,\"maxZ\":100,\"canopyHeightM\":10}]}");
            malformed.close();
            require(ScenarioIO::load(vegPath.toStdString()).vegetation.empty(),
                    "an inverted area is dropped on read");

            // Scenarios written before vegetation existed still load.
            QFile legacy(vegPath);
            require(legacy.open(QIODevice::WriteOnly | QIODevice::Truncate),
                    "rewrite scenario without vegetation");
            legacy.write("{\"mode\":\"planner\"}");
            legacy.close();
            require(ScenarioIO::load(vegPath.toStdString()).vegetation.empty(),
                    "a scenario without vegetation loads with none");
        }
        std::cout << "Scenario save checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
