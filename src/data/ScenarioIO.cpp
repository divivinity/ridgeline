#include "ScenarioIO.h"
#include <nlohmann/json.hpp>
#include <QFile>
#include <stdexcept>
#include <cmath>
#include <QSaveFile>

using json = nlohmann::json;

static const char* typeToStr(TerrainSource::Type t) {
    switch (t) {
        case TerrainSource::Type::Hills: return "hills";
        case TerrainSource::Type::Flat: return "flat";
        case TerrainSource::Type::Hgt: return "hgt";
        case TerrainSource::Type::Dted: return "dted";
        case TerrainSource::Type::GeoTiff: return "geotiff";
        case TerrainSource::Type::Global: return "global";
        default: throw std::logic_error("Unknown TerrainSource::Type");
    }
}

static TerrainSource::Type strToType(const std::string& s) {
    if (s == "flat") return TerrainSource::Type::Flat;
    if (s == "hgt") return TerrainSource::Type::Hgt;
    if (s == "dted") return TerrainSource::Type::Dted;
    if (s == "geotiff" || s == "tif" || s == "tiff") return TerrainSource::Type::GeoTiff;
    if (s == "global" || s == "streamed") return TerrainSource::Type::Global;
    return TerrainSource::Type::Hills;
}

static const char* modeToStr(SimEngine::Mode m) {
    switch (m) {
        case SimEngine::Mode::Preview: return "preview";
        case SimEngine::Mode::Planner: return "planner";
        case SimEngine::Mode::Engineering: return "engineering";
    }
    return "planner";
}

static SimEngine::Mode strToMode(const std::string& s) {
    if (s == "preview" || s == "fast") return SimEngine::Mode::Preview;
    if (s == "engineering" || s == "full") return SimEngine::Mode::Engineering;
    return SimEngine::Mode::Planner;
}

void ScenarioIO::save(const ScenarioDoc& doc, const std::string& path) {
    json j;
    j["version"] = 5;
    j["mode"] = modeToStr(doc.mode);

    auto& jr = j["receiver"];
    jr["height_agl_m"] = doc.receiver.receiverHeightAglM;
    jr["sensitivity_dbm"] = doc.receiver.sensitivityDbm;
    jr["bandwidth_hz"] = doc.receiver.bandwidthHz;
    jr["noise_figure_db"] = doc.receiver.noiseFigureDb;
    jr["required_snr_db"] = doc.receiver.requiredSnrDb;
    jr["fade_margin_target_db"] = doc.receiver.fadeMarginTargetDb;
    jr["polarization"] = doc.receiver.polarization;

    auto& jc = j["calibration"];
    jc["enabled"] = doc.calibration.enabled;
    jc["desired_power_offset_db"] = doc.calibration.desiredPowerOffsetDb;
    if (!doc.calibrationSourceName.empty())
        jc["measurement_source"] = doc.calibrationSourceName;
    if (doc.calibrationPairedSamples > 0)
        jc["paired_samples"] = doc.calibrationPairedSamples;
    if (std::isfinite(doc.calibrationRmseBeforeDb))
        jc["rmse_before_db"] = doc.calibrationRmseBeforeDb;
    if (std::isfinite(doc.calibrationRmseAfterDb))
        jc["rmse_after_db"] = doc.calibrationRmseAfterDb;

    auto& jt = j["terrain"];
    jt["type"] = typeToStr(doc.terrain.type);
    jt["width_cells"] = doc.terrain.widthCells;
    jt["height_cells"] = doc.terrain.heightCells;
    jt["cell_size"] = doc.terrain.cellSize;
    if (doc.terrain.type == TerrainSource::Type::Hills)
        jt["amplitude"] = doc.terrain.amplitude;
    if (doc.terrain.type == TerrainSource::Type::Hgt) {
        jt["hgt_path"] = doc.terrain.hgtPath;
        jt["hgt_lat"] = doc.terrain.hgtLat;
        jt["hgt_lon"] = doc.terrain.hgtLon;
        jt["hgt_size"] = doc.terrain.hgtSize;
    } else if (doc.terrain.type == TerrainSource::Type::Dted) {
        jt["dted_path"] = doc.terrain.hgtPath;
    } else if (doc.terrain.type == TerrainSource::Type::GeoTiff) {
        jt["geotiff_path"] = doc.terrain.hgtPath;
    } else if (doc.terrain.type == TerrainSource::Type::Global) {
        jt["center_lat"] = doc.terrain.centerLat;
        jt["center_lon"] = doc.terrain.centerLon;
        jt["extent_km"] = doc.terrain.extentKm;
    }

    json antennas = json::array();
    for (const auto& a : doc.antennas) {
        json ja;
        ja["x"] = a.position.x;
        ja["y"] = a.position.y;
        ja["z"] = a.position.z;
        ja["height_msl"] = a.position.y;
        ja["freq_mhz"] = a.frequencyMHz;
        ja["power_w"] = a.powerW;
        ja["gain_dbi"] = a.gainDbi;
        ja["is_jammer"] = a.isJammer;
        ja["antenna_type"] = a.antennaType;
        ja["azimuth_deg"] = a.azimuthDeg;
        ja["downtilt_deg"] = a.downtiltDeg;
        ja["horizontal_beamwidth_deg"] = a.horizontalBeamwidthDeg;
        ja["vertical_beamwidth_deg"] = a.verticalBeamwidthDeg;
        ja["front_to_back_db"] = a.frontToBackDb;
        ja["cable_loss_db"] = a.cableLossDb;
        ja["polarization"] = a.polarization;
        if (!a.azimuthPatternDb.empty()) ja["azimuth_pattern_db"] = a.azimuthPatternDb;
        if (!a.elevationPatternDb.empty()) ja["elevation_pattern_db"] = a.elevationPatternDb;
        if (!a.patternSource.empty()) ja["pattern_source"] = a.patternSource;
        if (a.hasGeoPosition) {
            ja["latitude"] = a.latitudeDeg;
            ja["longitude"] = a.longitudeDeg;
            ja["height_agl"] = a.heightAglM;
        }
        antennas.push_back(std::move(ja));
    }
    j["antennas"] = std::move(antennas);

    json layers = json::array();
    for (const auto& layer : doc.layers) {
        layers.push_back({{"id", layer.id},
                          {"visible", layer.visible},
                          {"opacity", layer.opacity}});
    }
    j["layers"] = std::move(layers);

    // Serialize before opening anything: invalid strings must not truncate an
    // existing scenario. Commit checks flushing and atomic replacement too.
    const std::string payload = j.dump(2);
    QSaveFile f(QString::fromStdString(path));
    f.setDirectWriteFallback(false);
    if (!f.open(QIODevice::WriteOnly))
        throw std::runtime_error("Cannot write scenario file: " + path + ": " + f.errorString().toStdString());
    if (f.write(payload.data(), static_cast<qint64>(payload.size())) != static_cast<qint64>(payload.size()))
        throw std::runtime_error("Scenario write failed: " + path + ": " + f.errorString().toStdString());
    if (!f.commit())
        throw std::runtime_error("Scenario commit failed: " + path + ": " + f.errorString().toStdString());
}

ScenarioDoc ScenarioIO::load(const std::string& path) {
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot open scenario file: " + path + ": " + f.errorString().toStdString());
    const QByteArray contents = f.readAll();
    if (f.error() != QFileDevice::NoError)
        throw std::runtime_error("Cannot read scenario file: " + path + ": " + f.errorString().toStdString());

    ScenarioDoc doc;
    try {
        json j = json::parse(contents.constBegin(), contents.constEnd());
        doc.mode = strToMode(j.value("mode", "planner"));

        // Absent in scenarios written before layers existed, so its absence is
        // not an error. Ids the running build does not know are kept out here
        // and ignored by the stack, leaving a newer file loadable.
        if (j.contains("layers") && j["layers"].is_array()) {
            for (const auto& entry : j["layers"]) {
                if (!entry.is_object()) continue;
                ScenarioLayerState layer;
                layer.id = entry.value("id", std::string{});
                layer.visible = entry.value("visible", false);
                layer.opacity = entry.value("opacity", 1.0f);
                if (!layer.id.empty()) doc.layers.push_back(layer);
            }
        }

        if (j.contains("receiver")) {
            const auto& jr = j["receiver"];
            ReceiverSettings defaults;
            doc.receiver.receiverHeightAglM = jr.value(
                "height_agl_m", defaults.receiverHeightAglM);
            doc.receiver.sensitivityDbm = jr.value("sensitivity_dbm", defaults.sensitivityDbm);
            doc.receiver.bandwidthHz = jr.value("bandwidth_hz", defaults.bandwidthHz);
            doc.receiver.noiseFigureDb = jr.value("noise_figure_db", defaults.noiseFigureDb);
            doc.receiver.requiredSnrDb = jr.value("required_snr_db", defaults.requiredSnrDb);
            doc.receiver.fadeMarginTargetDb = jr.value(
                "fade_margin_target_db", defaults.fadeMarginTargetDb);
            doc.receiver.polarization = jr.value(
                "polarization", defaults.polarization);
        }

        if (j.contains("calibration")) {
            const auto& jc = j["calibration"];
            const float offset = jc.value("desired_power_offset_db", 0.0f);
            doc.calibration.desiredPowerOffsetDb =
                std::isfinite(offset) ? offset : 0.0f;
            doc.calibration.enabled =
                jc.value("enabled", false) &&
                std::isfinite(doc.calibration.desiredPowerOffsetDb);
            doc.calibrationSourceName = jc.value(
                "measurement_source", std::string{});
            doc.calibrationPairedSamples = jc.value(
                "paired_samples", std::size_t{0});
            doc.calibrationRmseBeforeDb = jc.value(
                "rmse_before_db", std::numeric_limits<float>::quiet_NaN());
            doc.calibrationRmseAfterDb = jc.value(
                "rmse_after_db", std::numeric_limits<float>::quiet_NaN());
        }

        if (j.contains("terrain")) {
            const auto& jt = j["terrain"];
            doc.terrain.type = strToType(jt.value("type", std::string{"hills"}));
            const TerrainSource def;
            doc.terrain.widthCells = jt.value("width_cells", def.widthCells);
            doc.terrain.heightCells = jt.value("height_cells", def.heightCells);
            doc.terrain.cellSize = jt.value("cell_size", def.cellSize);
            doc.terrain.amplitude = jt.value("amplitude", def.amplitude);
            if (doc.terrain.type == TerrainSource::Type::Hgt) {
                doc.terrain.hgtPath = jt.value("hgt_path", std::string{});
                doc.terrain.hgtLat = jt.value("hgt_lat", def.hgtLat);
                doc.terrain.hgtLon = jt.value("hgt_lon", def.hgtLon);
                doc.terrain.hgtSize = jt.value("hgt_size", def.hgtSize);
            } else if (doc.terrain.type == TerrainSource::Type::Dted) {
                doc.terrain.hgtPath = jt.value("dted_path", std::string{});
            } else if (doc.terrain.type == TerrainSource::Type::GeoTiff) {
                doc.terrain.hgtPath = jt.value("geotiff_path", std::string{});
            } else if (doc.terrain.type == TerrainSource::Type::Global) {
                doc.terrain.centerLat = jt.value("center_lat", def.centerLat);
                doc.terrain.centerLon = jt.value("center_lon", def.centerLon);
                doc.terrain.extentKm = jt.value("extent_km", def.extentKm);
            }
        }

        if (j.contains("antennas")) {
            for (const auto& ja : j["antennas"]) {
                Antenna a;
                a.position.x = ja.value("x", 0.0f);
                a.position.y = ja.value("y", ja.value("height_msl", 0.0f));
                a.position.z = ja.value("z", 0.0f);
                a.frequencyMHz = ja.value("freq_mhz", 150.0f);
                a.powerW = ja.value("power_w", 50.0f);
                a.gainDbi = ja.value("gain_dbi", 3.0f);
                a.isJammer = ja.value("is_jammer", false);
                a.antennaType = ja.value("antenna_type", std::string{"omni"});
                a.azimuthDeg = ja.value("azimuth_deg", 0.0f);
                a.downtiltDeg = ja.value("downtilt_deg", 0.0f);
                a.horizontalBeamwidthDeg = ja.value("horizontal_beamwidth_deg", 360.0f);
                a.verticalBeamwidthDeg = ja.value("vertical_beamwidth_deg", 90.0f);
                a.frontToBackDb = ja.value("front_to_back_db", 30.0f);
                a.cableLossDb = ja.value("cable_loss_db", 0.0f);
                a.polarization = ja.value("polarization", std::string{"vertical"});
                if (ja.contains("azimuth_pattern_db"))
                    a.azimuthPatternDb = ja["azimuth_pattern_db"].get<std::vector<float>>();
                if (ja.contains("elevation_pattern_db"))
                    a.elevationPatternDb = ja["elevation_pattern_db"].get<std::vector<float>>();
                a.patternSource = ja.value("pattern_source", std::string{});
                if (ja.contains("latitude") && ja.contains("longitude")) {
                    a.hasGeoPosition = true;
                    a.latitudeDeg = ja.value("latitude", 0.0);
                    a.longitudeDeg = ja.value("longitude", 0.0);
                    a.heightAglM = ja.value("height_agl", 10.0f);
                }
                doc.antennas.push_back(std::move(a));
            }
        }
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("Scenario parse error: ") + e.what());
    }
    return doc;
}
