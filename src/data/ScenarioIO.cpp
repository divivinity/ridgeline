#include "ScenarioIO.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

static const char* typeToStr(TerrainSource::Type t) {
    switch (t) {
        case TerrainSource::Type::Hills: return "hills";
        case TerrainSource::Type::Flat:  return "flat";
        case TerrainSource::Type::Hgt:   return "hgt";
        case TerrainSource::Type::Dted:  return "dted";
        default: throw std::logic_error("Unknown TerrainSource::Type");
    }
}

static TerrainSource::Type strToType(const std::string& s) {
    if (s == "flat") return TerrainSource::Type::Flat;
    if (s == "hgt")  return TerrainSource::Type::Hgt;
    if (s == "dted") return TerrainSource::Type::Dted;
    return TerrainSource::Type::Hills;
}

void ScenarioIO::save(const ScenarioDoc& doc, const std::string& path) {
    json j;
    j["version"] = 1;
    j["mode"]    = (doc.mode == SimEngine::Mode::Fast) ? "fast" : "full";

    auto& jt = j["terrain"];
    jt["type"]         = typeToStr(doc.terrain.type);
    jt["width_cells"]  = doc.terrain.widthCells;
    jt["height_cells"] = doc.terrain.heightCells;
    jt["cell_size"]    = doc.terrain.cellSize;
    if (doc.terrain.type == TerrainSource::Type::Hills)
        jt["amplitude"] = doc.terrain.amplitude;
    if (doc.terrain.type == TerrainSource::Type::Hgt) {
        jt["hgt_path"] = doc.terrain.hgtPath;
        jt["hgt_lat"]  = doc.terrain.hgtLat;
        jt["hgt_lon"]  = doc.terrain.hgtLon;
        jt["hgt_size"] = doc.terrain.hgtSize;
    } else if (doc.terrain.type == TerrainSource::Type::Dted) {
        jt["dted_path"] = doc.terrain.hgtPath;
    }

    json antennas = json::array();
    for (const auto& a : doc.antennas) {
        json ja;
        ja["x"]            = a.position.x;
        ja["y"]            = a.position.y;
        ja["z"]            = a.position.z;
        ja["freq_mhz"]     = a.frequencyMHz;
        ja["power_w"]      = a.powerW;
        ja["gain_dbi"]     = a.gainDbi;
        ja["is_jammer"]    = a.isJammer;
        ja["antenna_type"] = a.antennaType;
        antennas.push_back(std::move(ja));
    }
    j["antennas"] = std::move(antennas);

    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot write scenario file: " + path);
    f << j.dump(2);
    if (!f) throw std::runtime_error("Write failed (disk full?): " + path);
}

ScenarioDoc ScenarioIO::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open scenario file: " + path);

    ScenarioDoc doc;
    try {
        json j = json::parse(f);

        const std::string modeStr = j.value("mode", "fast");
        doc.mode = (modeStr == "full") ? SimEngine::Mode::Full : SimEngine::Mode::Fast;

        if (j.contains("terrain")) {
            const auto& jt    = j["terrain"];
            doc.terrain.type  = strToType(jt.value("type", std::string{"hills"}));
            {
                const TerrainSource def;
                doc.terrain.widthCells  = jt.value("width_cells",  def.widthCells);
                doc.terrain.heightCells = jt.value("height_cells", def.heightCells);
                doc.terrain.cellSize    = jt.value("cell_size",    def.cellSize);
                doc.terrain.amplitude   = jt.value("amplitude",    def.amplitude);
                if (doc.terrain.type == TerrainSource::Type::Hgt) {
                    doc.terrain.hgtPath = jt.value("hgt_path", std::string{});
                    doc.terrain.hgtLat  = jt.value("hgt_lat",  def.hgtLat);
                    doc.terrain.hgtLon  = jt.value("hgt_lon",  def.hgtLon);
                    doc.terrain.hgtSize = jt.value("hgt_size",  def.hgtSize);
                } else if (doc.terrain.type == TerrainSource::Type::Dted) {
                    doc.terrain.hgtPath = jt.value("dted_path", std::string{});
                }
            }
        }

        if (j.contains("antennas")) {
            for (const auto& ja : j["antennas"]) {
                Antenna a;
                a.position.x   = ja.value("x",            0.0f);
                a.position.y   = ja.value("y",            0.0f);
                a.position.z   = ja.value("z",            0.0f);
                a.frequencyMHz = ja.value("freq_mhz",   150.0f);
                a.powerW       = ja.value("power_w",     50.0f);
                a.gainDbi      = ja.value("gain_dbi",     3.0f);
                a.isJammer     = ja.value("is_jammer",   false);
                a.antennaType  = ja.value("antenna_type", std::string{"omni"});
                doc.antennas.push_back(std::move(a));
            }
        }
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("Scenario parse error: ") + e.what());
    }
    return doc;
}
