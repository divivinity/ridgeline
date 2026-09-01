#include "data/AntennaLibrary.h"
#include "ui/LocationDialog.h"
#include "ui/PropertiesPanel.h"
#include "ui/SourcePositionDialog.h"
#include "sim/TerrainModel.h"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QHostAddress>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QToolButton>

#include <cmath>
#include <functional>
#include <iostream>

namespace {

int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "FAIL: " #condition " (line " << __LINE__ << ")\n"; \
        ++failures; \
    } \
} while (0)

bool waitUntil(const std::function<bool()>& condition, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (condition()) return true;
    }
    return condition();
}

void testAdvancedPanelRemainsEditableAndCollapsible() {
    PropertiesPanel panel;
    AntennaLibrary library;
    library.loadDefaults();
    panel.setAntennaLibrary(&library);

    Antenna antenna;
    antenna.antennaType = "omni";
    panel.load(&antenna);
    panel.resize(320, 300);
    panel.show();
    QApplication::processEvents();

    auto* toggle = panel.findChild<QToolButton*>("AdvancedSettingsToggle");
    auto* advanced = panel.findChild<QWidget*>("AdvancedSettingsPanel");
    auto* type = panel.findChild<QComboBox*>("AntennaTypeCombo");
    auto* polarization = panel.findChild<QComboBox*>("PolarizationCombo");
    auto* editPosition = panel.findChild<QPushButton*>("EditSourcePositionButton");
    CHECK(toggle && advanced && type && polarization && editPosition);
    if (!toggle || !advanced || !type || !polarization || !editPosition) return;
    panel.setPositionSummary("35.000000°, -80.000000°\nMGRS 17SNA0000000000");
    CHECK(editPosition->isVisible());

    toggle->click();
    QApplication::processEvents();
    CHECK(toggle->isChecked());
    CHECK(advanced->isVisible());
    CHECK(toggle->isVisible());

    type->setCurrentText("yagi");
    polarization->setCurrentText("horizontal");
    QApplication::processEvents();
    CHECK(type->currentText() == "yagi");
    CHECK(polarization->currentText() == "horizontal");
    CHECK(toggle->isVisible());

    toggle->click();
    QApplication::processEvents();
    CHECK(!toggle->isChecked());
    CHECK(!advanced->isVisible());
    CHECK(toggle->isVisible());
}

TerrainModel makeGeoTerrain() {
    TerrainModel terrain;
    terrain.width = 101;
    terrain.height = 101;
    const double sampleDegrees = 0.0002;
    const double centerLatitude = 35.01;
    terrain.setCellSpacing(
        static_cast<float>(TerrainModel::metersPerDegreeLon(centerLatitude) * sampleDegrees),
        static_cast<float>(TerrainModel::metersPerDegreeLat(centerLatitude) * sampleDegrees));
    terrain.setGeoReference(35.0, -80.0, sampleDegrees, sampleDegrees);
    terrain.heights.assign(terrain.width * terrain.height, 125.0f);
    return terrain;
}

void testPreciseSourcePositionDialog() {
    const TerrainModel terrain = makeGeoTerrain();
    const glm::vec2 start = terrain.latLonToWorld(35.005, -79.995);
    Antenna antenna;
    antenna.position = {start.x, 140.0f, start.y};

    SourcePositionDialog dialog(terrain, antenna);
    auto* format = dialog.findChild<QComboBox*>("SourcePositionFormatCombo");
    auto* latitude = dialog.findChild<QDoubleSpinBox*>("SourceLatitudeSpin");
    auto* longitude = dialog.findChild<QDoubleSpinBox*>("SourceLongitudeSpin");
    auto* mgrs = dialog.findChild<QLineEdit*>("SourceMgrsEdit");
    auto* move = dialog.findChild<QPushButton*>("MoveSourceButton");
    CHECK(format && latitude && longitude && mgrs && move);
    if (!format || !latitude || !longitude || !mgrs || !move) return;
    CHECK(format->count() == 3);
    CHECK(!mgrs->text().isEmpty());

    format->setCurrentIndex(1);
    latitude->setValue(35.012);
    longitude->setValue(-79.988);
    move->click();
    QApplication::processEvents();
    CHECK(dialog.result() == QDialog::Accepted);
    const glm::vec2 expected = terrain.latLonToWorld(35.012, -79.988);
    CHECK(std::abs(dialog.worldX() - expected.x) < 0.1f);
    CHECK(std::abs(dialog.worldZ() - expected.y) < 0.1f);
}

void testMgrsDialogInput() {
    LocationDialog dialog(0.0, 0.0, 10.0);
    auto* format = dialog.findChild<QComboBox*>("CoordinateFormatCombo");
    auto* mgrs = dialog.findChild<QLineEdit*>("MgrsEdit");
    auto* apply = dialog.findChild<QPushButton*>("ApplyMgrsButton");
    CHECK(format && mgrs && apply);
    if (!format || !mgrs || !apply) return;

    format->setCurrentIndex(1);
    mgrs->setText("18S UJ 23394 07395");
    apply->click();
    QApplication::processEvents();
    CHECK(std::abs(dialog.latitude() - 38.8977) < 0.0002);
    CHECK(std::abs(dialog.longitude() + 77.0365) < 0.0002);
}

void testDebouncedCustomGeocoderSuggestions() {
    QTcpServer server;
    CHECK(server.listen(QHostAddress::LocalHost, 0));
    if (!server.isListening()) return;

    QByteArray requestBytes;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
            requestBytes += socket->readAll();
            if (!requestBytes.contains("\r\n\r\n")) return;
            const QByteArray body =
                R"([{"display_name":"Raleigh, North Carolina, USA","lat":"35.7796","lon":"-78.6382"}])";
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    });

    const QByteArray endpoint = QByteArray("http://127.0.0.1:")
        + QByteArray::number(server.serverPort()) + "/search";
    qputenv("RIDGELINE_GEOCODER_URL", endpoint);
    LocationDialog dialog(0.0, 0.0, 10.0);
    auto* edit = dialog.findChild<QLineEdit*>("LocationSearchEdit");
    auto* results = dialog.findChild<QListWidget*>("SearchResults");
    CHECK(edit && results);
    if (edit && results) {
        edit->setText("Ral");
        QMetaObject::invokeMethod(&dialog, "onSearchTextEdited",
                                  Q_ARG(QString, QStringLiteral("Ral")));
        CHECK(waitUntil([&]() { return results->count() == 1; }, 2500));
        CHECK(requestBytes.contains("q=Ral"));
        CHECK(results->item(0)->text().contains("Raleigh"));

        // Repeating the exact typeahead query must be served from QSettings,
        // not sent to the configured endpoint a second time.
        QMetaObject::invokeMethod(&dialog, "onSearchTextEdited",
                                  Q_ARG(QString, QStringLiteral("Ral")));
        waitUntil([]() { return false; }, 700);
        CHECK(requestBytes.count("GET ") == 1);
    }
    qunsetenv("RIDGELINE_GEOCODER_URL");
}

} // namespace

int main(int argc, char** argv) {
#ifdef Q_OS_WIN
    qputenv("QT_QPA_PLATFORM", "windows");
#else
    qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("RidgelineTests");
    QCoreApplication::setApplicationName("RidgelineUiTests");

    testAdvancedPanelRemainsEditableAndCollapsible();
    testPreciseSourcePositionDialog();
    testMgrsDialogInput();
    testDebouncedCustomGeocoderSuggestions();

    if (failures == 0)
        std::cout << "UI interaction tests passed.\n";
    return failures == 0 ? 0 : 1;
}
