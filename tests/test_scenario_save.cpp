#include "data/ScenarioIO.h"
#include "data/CanopyImportOptions.h"
#include "data/RecoveryStore.h"
#include "data/WorkspaceSettings.h"
#include "data/AntennaJson.h"
#include <nlohmann/json.hpp>
#include <string>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QSettings>
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
        {
            landfire::Options current;
            landfire::Options proposed;
            proposed.developed = landfire::DevelopedPolicy::TreatAsBare;
            proposed.assumedDevelopedCanopyM = 0.0f;
            const auto observesProposed = [&] {
                require(current.developed == proposed.developed &&
                            current.assumedDevelopedCanopyM == 0.0f,
                        "reload observes proposed canopy options");
            };
            require(!replaceCanopyOptions(current, proposed, [&] {
                        observesProposed(); return false;
                    }), "failed or cancelled replacement reports no commit");
            require(current.developed == landfire::DevelopedPolicy::AssumeHeight &&
                        current.assumedDevelopedCanopyM == 10.0f,
                    "failed replacement preserves options matching the old raster");
            bool threw = false;
            try {
                replaceCanopyOptions(current, proposed, [&]() -> bool {
                    observesProposed(); throw std::runtime_error("fixture failure");
                });
            } catch (const std::runtime_error&) { threw = true; }
            require(threw && current.assumedDevelopedCanopyM == 10.0f &&
                        current.developed == landfire::DevelopedPolicy::AssumeHeight,
                    "exception also rolls back both option fields");
            require(replaceCanopyOptions(current, proposed, [&] {
                        observesProposed(); return true;
                    }) && current.assumedDevelopedCanopyM == 0.0f &&
                        current.developed == landfire::DevelopedPolicy::TreatAsBare,
                    "successful replacement commits both option fields");
        }
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
            const QString canopyPath = dir.filePath("canopy.json");
            for (const char* policy : {"assume", "source", "bare"}) {
                for (float height : {0.0f, 12.5f, 60.0f}) {
                    ScenarioDoc canopy;
                    canopy.canopyImport.enabled = true;
                    canopy.canopyImport.sourceId = "landfire-fixture";
                    canopy.canopyImport.developedPolicy = policy;
                    canopy.canopyImport.assumedDevelopedCanopyM = height;
                    canopy.canopyImport.retrievedUtc = "2026-09-13T12:00:00Z";
                    ScenarioIO::save(canopy, canopyPath.toStdString());
                    const auto back = ScenarioIO::load(canopyPath.toStdString()).canopyImport;
                    const auto options = canopyOptionsForImport(back);
                    require(options.assumedDevelopedCanopyM == height,
                            "live import options restore boundary heights including zero");
                    const auto expectedPolicy = std::string(policy) == "source"
                        ? landfire::DevelopedPolicy::UseSourceValue
                        : std::string(policy) == "bare"
                            ? landfire::DevelopedPolicy::TreatAsBare
                            : landfire::DevelopedPolicy::AssumeHeight;
                    require(options.developed == expectedPolicy,
                            "live import options restore the recorded policy");
                    require(back.enabled && back.sourceId == "landfire-fixture" &&
                                back.developedPolicy == policy &&
                                std::abs(back.assumedDevelopedCanopyM - height) < 1e-6f &&
                                back.retrievedUtc == "2026-09-13T12:00:00Z",
                            "canopy policy, boundary height and provenance survive saving");
                }
            }
            ScenarioDoc disabled;
            for (float invalid : {-1.0f, 60.1f,
                                  std::numeric_limits<float>::infinity(),
                                  std::numeric_limits<float>::quiet_NaN()}) {
                CanopyImport record;
                record.developedPolicy = "unknown";
                record.assumedDevelopedCanopyM = invalid;
                const auto options = canopyOptionsForImport(record);
                require(options.assumedDevelopedCanopyM == 10.0f &&
                            options.developed == landfire::DevelopedPolicy::AssumeHeight,
                        "invalid import options use defaults, not previous scene state");
            }
            disabled.canopyImport.sourceId = "stale-provider";
            ScenarioIO::save(disabled, canopyPath.toStdString());
            require(!ScenarioIO::load(canopyPath.toStdString()).canopyImport.enabled,
                    "disabled canopy does not become an import on reopening");
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
            unsaved.canopyImport.enabled = true;
            unsaved.canopyImport.sourceId = "recovery-canopy-fixture";
            unsaved.canopyImport.developedPolicy = "bare";
            unsaved.canopyImport.assumedDevelopedCanopyM = 0.0f;
            require(RecoveryStore::write(unsaved), "autosave writes a recovery copy");
            require(RecoveryStore::exists(), "the recovery copy is found afterwards");
            require(RecoveryStore::savedAt().isValid(), "the recovery copy is timestamped");

            const ScenarioDoc recovered = RecoveryStore::read();
            require(recovered.calibrationSourceName == "unsaved work",
                    "recovered work round-trips through the same format");
            require(recovered.layers.size() == 1 && recovered.layers[0].visible,
                    "recovered work keeps its layer state");
            require(recovered.canopyImport.enabled &&
                        recovered.canopyImport.sourceId == "recovery-canopy-fixture" &&
                        recovered.canopyImport.developedPolicy == "bare" &&
                        recovered.canopyImport.assumedDevelopedCanopyM == 0.0f,
                    "recovery preserves canopy import choices including zero height");

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

            // Equipment presets keep the radio and antenna, listed by name.
            require(WorkspaceSettings::equipmentPresets().empty(), "a fresh profile has no presets");
            Antenna yagi;
            yagi.frequencyMHz = 433.5f;
            yagi.powerW = 5.0f;
            yagi.gainDbi = 11.0f;
            yagi.antennaType = "yagi";
            yagi.azimuthDeg = 270.0f;
            yagi.downtiltDeg = 2.0f;
            yagi.horizontalBeamwidthDeg = 40.0f;
            yagi.verticalBeamwidthDeg = 35.0f;
            yagi.frontToBackDb = 18.0f;
            yagi.cableLossDb = 1.5f;
            yagi.polarization = "horizontal";
            yagi.azimuthPatternDb = {0.0f, -3.0f, -20.0f, -3.0f};
            yagi.patternSource = "yagi.csv";
            WorkspaceSettings::saveEquipmentPreset("  Yagi 433  ", yagi);
            WorkspaceSettings::saveEquipmentPreset("base station", Antenna{});
            WorkspaceSettings::saveEquipmentPreset("", yagi);
            auto presets = WorkspaceSettings::equipmentPresets();
            require(presets.size() == 2, "a blank name is not saved");
            require(presets[0].name == "base station" && presets[1].name == "Yagi 433",
                    "presets are listed by name, case-insensitively, with names trimmed");
            const Antenna& kept = presets[1].equipment;
            require(kept.frequencyMHz == 433.5f && kept.powerW == 5.0f && kept.gainDbi == 11.0f &&
                        kept.antennaType == "yagi" && kept.azimuthDeg == 270.0f &&
                        kept.downtiltDeg == 2.0f && kept.horizontalBeamwidthDeg == 40.0f &&
                        kept.verticalBeamwidthDeg == 35.0f && kept.frontToBackDb == 18.0f &&
                        kept.cableLossDb == 1.5f && kept.polarization == "horizontal" &&
                        kept.azimuthPatternDb == yagi.azimuthPatternDb &&
                        kept.patternSource == "yagi.csv",
                    "every equipment field round-trips through a preset");

            yagi.powerW = 25.0f;
            WorkspaceSettings::saveEquipmentPreset("Yagi 433", yagi);
            presets = WorkspaceSettings::equipmentPresets();
            require(presets.size() == 2 && presets[1].equipment.powerW == 25.0f,
                    "saving under an existing name replaces that preset");
            WorkspaceSettings::removeEquipmentPreset("base station");
            require(WorkspaceSettings::equipmentPresets().size() == 1, "a preset can be deleted");

            // Applying equipment changes the radio, never the site or the role,
            // and a preset with no measured pattern removes the target's.
            Antenna site;
            site.position = {120.0f, 340.0f, 560.0f};
            site.isJammer = true;
            site.hasGeoPosition = true;
            site.latitudeDeg = 35.1;
            site.longitudeDeg = -82.4;
            site.heightAglM = 30.0f;
            site.elevationPatternDb = {0.0f, -6.0f};
            const Antenna applied = AntennaJson::withEquipment(site, Antenna{});
            require(applied.position == site.position && applied.isJammer &&
                        applied.hasGeoPosition && applied.latitudeDeg == 35.1 &&
                        applied.heightAglM == 30.0f,
                    "a preset keeps the target's position, height and role");
            require(applied.elevationPatternDb.empty() && applied.frequencyMHz == Antenna{}.frequencyMHz,
                    "a preset replaces the target's equipment wholesale");

            // A damaged entry is skipped without hiding the good ones, and
            // stored text that is not JSON at all reads as no presets.
            {
                QSettings raw(QDir(settingsDir.path()).filePath("workspace.ini"),
                              QSettings::IniFormat);
                raw.setValue("presets/equipment", QByteArray(
                    R"([{"name":"good","power_w":2},{"name":5},)"
                    R"({"name":"bad","power_w":"high"},"text"])"));
            }
            presets = WorkspaceSettings::equipmentPresets();
            require(presets.size() == 1 && presets[0].name == "good" &&
                        presets[0].equipment.powerW == 2.0f,
                    "damaged preset entries are skipped, not fatal");
            {
                QSettings raw(QDir(settingsDir.path()).filePath("workspace.ini"),
                              QSettings::IniFormat);
                raw.setValue("presets/equipment", QByteArray("not json"));
            }
            require(WorkspaceSettings::equipmentPresets().empty(),
                    "unreadable preset storage reads as no presets");

            WorkspaceSettings::clear();
            require(WorkspaceSettings::recentScenarios().isEmpty() &&
                        WorkspaceSettings::recentLocations().empty() &&
                        WorkspaceSettings::layoutGeometry().isEmpty() &&
                        WorkspaceSettings::equipmentPresets().empty(),
                    "clearing forgets history, layout and presets together");
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
        {
            // Format 6: clearings, and what older and newer readers do with them.
            ScenarioDoc mixed;
            const VegetationArea stand{100.0f, 100.0f, 300.0f, 300.0f, 18.0f, "north wood"};
            const VegetationArea clearing{150.0f, 150.0f, 250.0f, 250.0f, 12.0f,
                                          "landing zone", VegetationKind::Clearing};
            mixed.vegetation = {stand, clearing};
            const QString mixedPath = dir.filePath("clearings.json");
            ScenarioIO::save(mixed, mixedPath.toStdString());
            const ScenarioDoc back = ScenarioIO::load(mixedPath.toStdString());
            require(back.vegetation.size() == 2 &&
                    back.vegetation[0].kind == VegetationKind::Stand &&
                    back.vegetation[1].kind == VegetationKind::Clearing,
                    "stand and clearing kinds round-trip");
            require(back.vegetation[1].name == "landing zone" &&
                    std::abs(back.vegetation[1].minX - 150.0f) < 1e-4f &&
                    std::abs(back.vegetation[1].maxZ - 250.0f) < 1e-4f,
                    "a clearing's name and bounds round-trip");
            require(back.vegetation[1].canopyHeightM == 0.0f,
                    "a clearing comes back as bare ground");

            const nlohmann::json raw = nlohmann::json::parse(read(mixedPath).toStdString());
            require(raw.value("version", 0) == 6, "the file declares format 6");
            require(raw.value("minReaderVersion", 0) == 6 && raw.contains("requires"),
                    "a file with a clearing names the oldest reader that understands it");
            require(raw["vegetation"][0]["kind"] == "stand" &&
                    raw["vegetation"][0]["canopyHeightM"].is_number(),
                    "a stand is written with its height");
            require(raw["vegetation"][1]["kind"] == "clearing" &&
                    raw["vegetation"][1]["canopyHeightM"].is_null(),
                    "a clearing is written with a null height");

            // The fail-safe for builds before clearings: they read this key with
            // value<float>, which must throw so they refuse the file rather than
            // drop the clearing or plant a stand where the trees were felled.
            bool olderReaderRefuses = false;
            try {
                (void)raw["vegetation"][1].value("canopyHeightM", 0.0f);
            } catch (const nlohmann::json::type_error&) {
                olderReaderRefuses = true;
            }
            require(olderReaderRefuses, "an older reader's height lookup throws on a clearing");

            ScenarioDoc standsOnly;
            standsOnly.vegetation = {stand};
            ScenarioIO::save(standsOnly, mixedPath.toStdString());
            const nlohmann::json rawStands = nlohmann::json::parse(read(mixedPath).toStdString());
            bool standsReadable = true;
            try {
                for (const auto& entry : rawStands["vegetation"])
                    (void)entry.value("canopyHeightM", 0.0f);
            } catch (const nlohmann::json::exception&) {
                standsReadable = false;
            }
            require(!rawStands.contains("minReaderVersion") && !rawStands.contains("requires") &&
                        standsReadable,
                    "a file with only stands stays readable by builds before clearings");

            const auto writeRaw = [&mixedPath](const char* text) {
                QFile file(mixedPath);
                require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "rewrite fixture");
                file.write(text);
            };
            const auto loadError = [&mixedPath]() -> std::string {
                try {
                    ScenarioIO::load(mixedPath.toStdString());
                } catch (const std::exception& e) {
                    return e.what();
                }
                return {};
            };

            writeRaw("{\"vegetation\":[{\"minX\":0,\"minZ\":0,\"maxX\":10,\"maxZ\":10,"
                     "\"canopyHeightM\":7.5}]}");
            const ScenarioDoc legacy = ScenarioIO::load(mixedPath.toStdString());
            require(legacy.vegetation.size() == 1 &&
                    legacy.vegetation[0].kind == VegetationKind::Stand &&
                    std::abs(legacy.vegetation[0].canopyHeightM - 7.5f) < 1e-4f,
                    "an entry written before kinds existed loads as a stand");

            writeRaw("{\"vegetation\":[{\"kind\":\"polygon\",\"minX\":0,\"minZ\":0,"
                     "\"maxX\":10,\"maxZ\":10,\"canopyHeightM\":7.5}]}");
            const std::string unknownKind = loadError();
            require(unknownKind.find("polygon") != std::string::npos &&
                        unknownKind.find("parse error") == std::string::npos,
                    "an unknown vegetation kind refuses the file, naming the kind");

            writeRaw("{\"minReaderVersion\":7,\"vegetation\":[]}");
            require(loadError().find("newer") != std::string::npos,
                    "a file needing a newer reader is refused with a message saying so");

            writeRaw("{\"vegetation\":[{\"kind\":\"clearing\",\"minX\":10,\"minZ\":0,"
                     "\"maxX\":0,\"maxZ\":10,\"canopyHeightM\":null},"
                     "{\"minX\":0,\"minZ\":0,\"maxX\":1e39,\"maxZ\":10,\"canopyHeightM\":5}]}");
            require(ScenarioIO::load(mixedPath.toStdString()).vegetation.empty(),
                    "an inverted clearing and an infinite bound are both dropped");

            const QString stateDir = dir.filePath("clearing-recovery");
            require(QDir().mkpath(stateDir), "create a recovery state directory");
            qputenv("RIDGELINE_STATE_DIR", stateDir.toUtf8());
            require(RecoveryStore::write(mixed),
                    "a workspace containing a clearing is written for recovery");
            const ScenarioDoc recovered = RecoveryStore::read();
            require(recovered.vegetation.size() == 2 &&
                        recovered.vegetation[1].kind == VegetationKind::Clearing,
                    "a recovery copy preserves a clearing");
            RecoveryStore::clear();
            qunsetenv("RIDGELINE_STATE_DIR");
        }
        std::cout << "Scenario save checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
