#include <QApplication>
#include <QSurfaceFormat>
#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    // QSettings requires a stable organization/application namespace. Without
    // it, geocoder responses appear to be written but are not available to a
    // subsequent lookup in the running desktop app.
    app.setOrganizationName("Ridgeline");
    app.setOrganizationDomain("divivinity.github.io");
    app.setApplicationName("Ridgeline");

    MainWindow w;
    w.setMinimumSize(1050, 700);
    w.resize(1400, 900);
    w.showMaximized();

    // Optional: pass a DTED file as the first argument.
    if (argc > 1) w.loadTerrain(QString::fromLocal8Bit(argv[1]));

    return app.exec();
}
