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
    app.setApplicationName("Atomic Biscuit");

    MainWindow w;
    w.resize(1400, 900);
    w.show();
    return app.exec();
}
