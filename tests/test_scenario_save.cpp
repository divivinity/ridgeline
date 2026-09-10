#include "data/ScenarioIO.h"
#include "data/RecoveryStore.h"
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
        std::cout << "Scenario save checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
