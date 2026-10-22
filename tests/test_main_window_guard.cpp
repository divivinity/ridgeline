// MainWindow-level GUI tests.
//
// Separate from test_ui_interactions.cpp, which runs on the offscreen platform
// and deliberately links only the panels and dialogs. MainWindow owns a
// QOpenGLWidget viewport, which offscreen cannot give a GL context, so these
// need a real display — CI runs them under xvfb-run and they are opt-in via
// the RIDGELINE_GUI_TESTS CMake option.
//
// Private widgets are reached through Qt's object tree, so nothing here
// requires MainWindow to expose test hooks.
#include "ui/MainWindow.h"
#include <QWheelEvent>
#include <QMouseEvent>
#include "ui/WorkspaceTheme.h"
#include "ui/MapFurniture.h"
#include "ui/LocationDialog.h"
#include "ui/LayersPanel.h"
#include "map/LayerStack.h"

#include <QAbstractButton>
#include <QAction>
#include <QToolButton>
#include <QSurfaceFormat>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QProgressDialog>
#include <QEventLoop>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QTimer>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkAccessManager>
#include <QScopeGuard>
#include <QFileDialog>
#include <QFile>
#include <QLineEdit>
#include <QDir>
#include <QImage>
#include <QPixmap>
#include <QUndoStack>
#include <QBuffer>
#include <QElapsedTimer>
#include "data/TileBatchDownloader.h"
#include "data/RecoveryStore.h"
#include "data/WorkspaceSettings.h"
#include "data/CanopyLoader.h"
#include "CanopyTiffFixture.h"
#include "data/TerrainLoader.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    // cerr so progress survives even if a modal ever wedges the run.
    std::cerr << (condition ? "[PASS] " : "[FAIL] ") << what << "\n";
    if (!condition) ++failures;
}

// Let the window finish laying out and any deferred startup work settle.
void settle(int ms = 1200) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

bool sawPrompt = false;

void answerModal(QMessageBox::StandardButton which) {
    if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        sawPrompt = true;
        if (QAbstractButton* button = box->button(which)) button->click();
        else box->reject();
    }
}

// Runs body() with a timer standing by to answer any modal it raises.
template <typename Body>
void withModalAnswer(QMessageBox::StandardButton which, Body&& body) {
    sawPrompt = false;
    QTimer answering;
    QObject::connect(&answering, &QTimer::timeout, [which] { answerModal(which); });
    answering.start(150);
    body();
    answering.stop();
}

QComboBox* findModeCombo(const MainWindow& window) {
    for (QComboBox* combo : window.findChildren<QComboBox*>())
        for (int i = 0; i < combo->count(); ++i)
            if (combo->itemText(i).startsWith("High accuracy"))
                return combo;
    return nullptr;
}

QPushButton* findButton(const MainWindow& window, const QString& label) {
    for (QPushButton* button : window.findChildren<QPushButton*>())
        if (button->text().contains(label)) return button;
    return nullptr;
}

// A freshly launched app has no unsaved work, so it must close silently.
// This is the regression that would annoy most if the guard over-fired.
void testCleanScenarioClosesWithoutPrompting() {
    MainWindow window;
    window.show();
    settle();

    bool closed = false;
    withModalAnswer(QMessageBox::Discard, [&] { closed = window.close(); });
    check(closed, "freshly started app closes");
    check(!sawPrompt, "no prompt on a clean scenario");
    check(!window.isWindowModified() && window.windowTitle().contains("[*]"),
          "clean scenario has a supported but inactive title indicator");
}

// Source edits funnel through pushSourceChange(), so the undo stack's clean
// flag is what marks them.
void testSourceEditPromptsViaUndoStack() {
    MainWindow window;
    window.show();
    settle();

    auto* list = window.findChild<QListWidget*>();
    if (!list || list->count() == 0) { check(false, "found a source to remove"); return; }
    list->setCurrentRow(0);
    qApp->processEvents();

    QPushButton* remove = findButton(window, "Remove");
    if (!remove) { check(false, "found the Remove button"); return; }
    remove->click();
    qApp->processEvents();
    check(window.isWindowModified(), "source edit activates the unsaved title indicator");
    auto* undo = window.findChild<QUndoStack*>();
    check(undo != nullptr, "source edit exposes undo history");
    if (undo) {
        undo->undo();
        check(!window.isWindowModified(), "undo to saved state clears the title indicator");
        undo->redo();
        check(window.isWindowModified(), "redo reactivates the title indicator");
    }

    bool closedOnCancel = true;
    withModalAnswer(QMessageBox::Cancel, [&] { closedOnCancel = window.close(); });
    check(sawPrompt, "source edit prompts before closing");
    check(!closedOnCancel, "Cancel keeps the window open");

    bool closedOnDiscard = false;
    withModalAnswer(QMessageBox::Discard, [&] { closedOnDiscard = window.close(); });
    check(closedOnDiscard, "Discard closes the window");
}

void testSavingUpdatesModifiedIndicator() {
    QTemporaryDir folder;
    check(folder.isValid(), "save-indicator fixture directory exists");
    if (!folder.isValid()) return;
    const QString path = folder.filePath("saved.json");
    MainWindow window;
    window.show();
    settle(150);
    auto* list = window.findChild<QListWidget*>("SourceList");
    auto* remove = findButton(window, "Remove");
    auto* mode = findModeCombo(window);
    if (!list || !remove || !mode || list->count() == 0) {
        check(false, "save-indicator fixture controls exist"); return;
    }
    list->setCurrentRow(0);
    remove->click();
    mode->setCurrentIndex(mode->currentIndex() == 0 ? 1 : 0);
    check(window.isWindowModified(), "mixed edits mark scenario unsaved before save");
    auto save = [&](bool cancel, bool failWrite = false) {
        bool sawPicker = false, error = false, timedOut = false;
        QString errorText;
        QElapsedTimer elapsed;
        elapsed.start();
        QTimer driver;
        QObject::connect(&driver, &QTimer::timeout, [&] {
            if (auto* dialog = window.findChild<QFileDialog*>(); dialog && dialog->isVisible()) {
                sawPicker = true;
                if (cancel || elapsed.elapsed() > 10000) {
                    timedOut = !cancel; dialog->reject();
                } else if (auto* edit = dialog->findChild<QLineEdit*>("fileNameEdit")) {
                    edit->setText(path);
                    QMetaObject::invokeMethod(dialog, "accept");
                    // After picker validation, reserve its fresh target as a
                    // directory before the caller writes. This deterministically
                    // exercises save failure without changing system permissions.
                    if (failWrite && dialog->result() == QDialog::Accepted)
                        check(QDir().mkdir(path), "save failure fixture reserves the target");
                }
            } else if (auto* box = window.findChild<QMessageBox*>(); box && box->isVisible()) {
                error = true; errorText = box->text(); box->reject();
            }
        });
        driver.start(25);
        check(QMetaObject::invokeMethod(&window, "onSaveScenario"), "save action is reachable");
        driver.stop();
        check(sawPicker && error == failWrite && !timedOut,
              "save picker completes with the expected failure state");
        if (failWrite)
            check(errorText.contains("Cannot write scenario file"), "save failure explains the write error");
    };
    save(true);
    check(window.isWindowModified() && !QFile::exists(path),
          "cancelled save keeps the indicator active and writes no file");
    save(false, true);
    check(window.isWindowModified() && list->count() == 0,
          "failed save retains unsaved indicator and edited sources");
    withModalAnswer(QMessageBox::Cancel, [&] { window.close(); });
    check(sawPrompt && window.isVisible(), "failed save retains the close guard");
    check(QDir().rmdir(path), "remove empty test-only destination blocker for retry");
    save(false);
    check(!window.isWindowModified() && QFile::exists(path), "successful save clears the title indicator");
    try {
        const auto saved = ScenarioIO::load(path.toStdString());
        check(saved.antennas.empty(), "saved scenario parses and preserves the source removal");
    } catch (const std::exception& e) {
        check(false, (std::string("saved scenario round trip: ") + e.what()).c_str());
    }
    withModalAnswer(QMessageBox::Cancel, [&] { window.close(); });
    check(!sawPrompt && !window.isVisible(), "saved scenario closes without an unsaved prompt");
}

// The RF mode is persisted by ScenarioDoc but is not undoable, so it is
// tracked by the separate flag rather than the undo stack.
void testModeChangePromptsViaDirtyFlag() {
    MainWindow window;
    window.show();
    settle();

    QComboBox* mode = findModeCombo(window);
    if (!mode) { check(false, "found the RF mode combo"); return; }
    mode->setCurrentIndex(mode->currentIndex() == 0 ? 1 : 0);
    qApp->processEvents();
    check(window.isWindowModified(), "non-undoable setting edit activates the title indicator");
    auto* sourceList = window.findChild<QListWidget*>("SourceList");
    auto* removeSource = findButton(window, "Remove");
    auto* history = window.findChild<QUndoStack*>();
    check(sourceList && removeSource && history, "mixed dirty-state fixture is ready");
    if (sourceList && removeSource && history && sourceList->count() > 0) {
        sourceList->setCurrentRow(0);
        removeSource->click();
        history->undo();
        check(history->isClean() && window.isWindowModified(),
              "clean undo history does not hide unsaved non-undoable settings");
    }

    withModalAnswer(QMessageBox::Discard, [&] { window.close(); });
    check(sawPrompt, "mode change prompts before closing");
}

// A coverage run puts up its own modal progress dialog. Closing during one
// must be refused outright: raising the unsaved-changes prompt on top would
// stack a second modal while onSolve() is blocked in its nested event loop,
// which leaves the window looking wedged.
void testCloseIsRefusedDuringASolve() {
    MainWindow window;
    window.show();
    settle();

    QPushButton* run = findButton(window, "Run coverage");
    if (!run) { check(false, "found the Run coverage button"); return; }

    // Slowest mode, so the solve lasts long enough to be closed into.
    if (QComboBox* mode = findModeCombo(window)) {
        for (int i = 0; i < mode->count(); ++i)
            if (mode->itemText(i).startsWith("High accuracy")) mode->setCurrentIndex(i);
    }
    qApp->processEvents();

    bool sawSolve = false, sawStackedBox = false, closeRefused = false;

    // The close is posted rather than called inline: if the guard regresses,
    // close() blocks inside the stacked prompt, and a timer that called it
    // could not fire again to dismiss its own dialog — the run would wedge
    // until CI's job timeout instead of failing.
    QTimer probe;
    QObject::connect(&probe, &QTimer::timeout, [&] {
        if (sawSolve || window.findChild<QProgressDialog*>() == nullptr) return;
        sawSolve = true;
        QMetaObject::invokeMethod(qApp, [&] { closeRefused = !window.close(); },
                                  Qt::QueuedConnection);
    });
    probe.start(50);

    // Independent of the probe, so it still runs while a stacked prompt is up.
    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        for (QWidget* top : QApplication::topLevelWidgets()) {
            auto* box = qobject_cast<QMessageBox*>(top);
            if (!box || !box->isVisible()) continue;
            sawStackedBox = true;   // the regression
            if (QAbstractButton* b = box->button(QMessageBox::Cancel)) b->click();
            else if (QAbstractButton* d = box->button(QMessageBox::Discard)) d->click();
            else box->reject();
        }
    });
    watchdog.start(100);

    run->click();   // blocks in the solve's nested event loop
    probe.stop();
    watchdog.stop();

    // Guard against the test silently passing without exercising anything.
    check(sawSolve, "observed a solve in progress to close into");
    if (!sawSolve) return;
    check(closeRefused, "close is refused while a solve is running");
    check(!sawStackedBox, "no prompt is stacked over the progress dialog");

    // Reuse the completed solve to exercise the full analysis workspace.
    check(QMetaObject::invokeMethod(&window, "onTerrainInspected",
                                    Q_ARG(float, 500.0f), Q_ARG(float, 500.0f)),
          "result inspection slot is reachable");
    window.resize(1050, 700);
    settle(100);
    auto* inspector = window.findChild<QDockWidget*>("ResultInspectorDock");
    auto* sourcesDock = window.findChild<QDockWidget*>("SourcesDock");
    check(inspector && inspector->isVisible() && sourcesDock && sourcesDock->isVisible(),
          "both planning and result panels are open");
    check(window.width() <= 1050 && window.height() <= 700,
          "both docks fit a laptop-size window");
    auto* position = window.findChild<QLabel*>("PointPosition");
    check(position && position->text().contains("Synthetic heights") &&
              !position->text().contains("MSL"),
          "inspected demo receiver does not claim a sea-level datum");
    auto* scroll = window.findChild<QScrollArea*>("ResultInspectorScrollArea");
    auto* explanation = window.findChild<QLabel*>("PointExplanation");
    check(scroll && explanation, "result inspector provides scrollable explanations");
    if (scroll && explanation) {
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        settle(100);
        const QRect bounds(explanation->mapTo(scroll->viewport(), QPoint{}), explanation->size());
        check(scroll->viewport()->rect().contains(bounds),
              "full result explanation remains reachable in a compact window");
    }

    // With the solve finished the window closes normally again.
    withModalAnswer(QMessageBox::Discard, [&] { window.close(); });
}

// Two independent places decide which overlays start visible: the checkbox
// arguments in OverlayToolbar, and ViewportWidget's constructor, which turns the
// analysis layers off over their headers' `visible = true` defaults. Nothing
// links them, so they can drift into a state where a layer draws while its box
// reads unchecked — the user would then have to check and uncheck it to hide it.
// They agree today; this pins that, since only a test can keep them in step.
void testCanopyOptionsCommitAndCancel() {
    MainWindow window;
    // Synthetic terrain, no loaded canopy: exercising options needs no network.
    auto visit = [&](int expectedPolicy, double expectedHeight, bool accept) {
        bool handled = false;
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "CanopyOptionsDialog") return;
            timer.stop();
            handled = true;
            auto* policy = dialog->findChild<QComboBox*>("DevelopedCanopyPolicy");
            auto* height = dialog->findChild<QDoubleSpinBox*>("AssumedCanopyHeight");
            check(policy && height, "canopy options controls are identifiable");
            if (policy && height) {
                check(policy->currentIndex() == expectedPolicy &&
                          std::abs(height->value() - expectedHeight) < 0.01,
                      "canopy options retain accepted values, not cancelled edits");
                check(!policy->accessibleName().isEmpty() &&
                          height->accessibleName().contains("metres"),
                      "canopy controls expose policy and height units");
                for (int index : {1, 2, 0}) {
                    policy->setCurrentIndex(index);
                    check(height->isEnabled() == (index == 0),
                          "assumed height is editable only for the assumption policy");
                }
                height->setValue(12.5);
                policy->setCurrentIndex(2);
            }
            if (accept) dialog->accept(); else dialog->reject();
        });
        timer.start(10);
        check(QMetaObject::invokeMethod(&window, "onCanopyOptions"),
              "canopy options action opens");
        check(handled, "canopy options dialog was exercised");
    };
    visit(0, 10.0, false);
    visit(0, 10.0, true);
    visit(2, 12.5, false);
}

void testCanopyRejectedAndCancelledLoadsKeepTerrain() {
    QNetworkAccessManager network;
    auto terrain = TerrainLoader::makeFlat(5, 5, 30.0f, 100.0f);
    terrain.canopyHeightDm.assign(25, 125);
    terrain.canopyCoverPct.assign(25, 60);
    const auto heights = terrain.heights;
    const auto canopy = terrain.canopyHeightDm;
    const auto cover = terrain.canopyCoverPct;
    const auto unchanged = [&] {
        check(terrain.heights == heights && terrain.canopyHeightDm == canopy &&
                  terrain.canopyCoverPct == cover,
              "unsuccessful canopy load preserves existing terrain and vegetation");
    };
    auto result = CanopyLoader::loadForTerrain(network, terrain, {});
    check(!result.ok() && result.error.contains("geographic reference"),
          "canopy loader rejects ungeoreferenced terrain before requesting data");
    unchanged();
    terrain.setGeoReference(48.8, 2.3, 0.001, 0.001);
    result = CanopyLoader::loadForTerrain(network, terrain, {});
    check(!result.ok() && result.error.contains("outside"),
          "canopy loader rejects terrain outside provider coverage");
    unchanged();
    terrain.setGeoReference(35.0, -78.0, 0.001, 0.001);
    result = CanopyLoader::loadForTerrain(network, terrain, {}, {}, [] { return true; });
    check(!result.ok() && result.error.contains("cancelled"),
          "pre-cancelled canopy request returns no installable result");
    unchanged();
}

void testCanopyNetworkCancellationAndMalformedResponse() {
    QTemporaryDir cache;
    check(cache.isValid(), "isolated canopy network cache exists");
    if (!cache.isValid()) return;
    const auto oldCache = qgetenv("RIDGELINE_CACHE_DIR");
    const auto oldUrl = qgetenv("RIDGELINE_CANOPY_URL");
    const auto restore = qScopeGuard([&] {
        if (oldCache.isNull()) qunsetenv("RIDGELINE_CACHE_DIR");
        else qputenv("RIDGELINE_CACHE_DIR", oldCache);
        if (oldUrl.isNull()) qunsetenv("RIDGELINE_CANOPY_URL");
        else qputenv("RIDGELINE_CANOPY_URL", oldUrl);
    });
    qputenv("RIDGELINE_CACHE_DIR", cache.path().toUtf8());
    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost, 0), "local canopy fixture listens");
    if (!server.isListening()) return;
    qputenv("RIDGELINE_CANOPY_URL",
            QString("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
    bool cancel = false;
    bool stalled = true;
    bool validTiffs = false;
    int requests = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto* socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                if (socket->property("answered").toBool()) return;
                socket->setProperty("answered", true);
                socket->readAll();
                ++requests;
                if (stalled) { cancel = true; return; }
                QByteArray body("nope");
                if (validTiffs) {
                    TiffBuilder tiff;
                    tiff.width = tiff.height = 5;
                    tiff.geo = true;
                    // Request bounds include the half-cell border around the
                    // terrain's sample centres.
                    tiff.westLon = -78.0005;
                    tiff.northLat = 35.0045;
                    tiff.step = 0.001;
                    tiff.samples.assign(25, requests == 1 ? 390 : requests == 2 ? 45 : 13);
                    const auto bytes = tiff.build();
                    body = QByteArray(reinterpret_cast<const char*>(bytes.data()),
                                      static_cast<qsizetype>(bytes.size()));
                }
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " +
                              QByteArray::number(body.size()) +
                              "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        }
    });
    QNetworkAccessManager network;
    auto terrain = TerrainLoader::makeFlat(5, 5, 30.0f, 100.0f);
    terrain.setGeoReference(35.0, -78.0, 0.001, 0.001);
    terrain.canopyHeightDm.assign(25, 125);
    terrain.canopyCoverPct.assign(25, 60);
    const auto original = terrain;
    QTimer deadline;
    deadline.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&deadline, &QTimer::timeout, [&] { timedOut = true; cancel = true; });
    deadline.start(5000);
    auto result = CanopyLoader::loadForTerrain(network, terrain, {}, {}, [&] { return cancel; });
    check(!timedOut && requests == 1 && !result.ok() && result.error.contains("cancelled"),
          "cancelling an active canopy request stops the remaining downloads");
    stalled = false;
    cancel = false;
    requests = 0;
    deadline.start(5000);
    result = CanopyLoader::loadForTerrain(network, terrain, {}, {}, [&] { return cancel; });
    deadline.stop();
    check(!timedOut && requests == 3 && !result.ok() && result.error.contains("could not be read"),
          "retry reaches all layers and rejects malformed TIFF responses");
    requests = 0;
    deadline.start(5000);
    result = CanopyLoader::loadForTerrain(network, terrain, {}, {}, [&] { return cancel; });
    deadline.stop();
    check(!timedOut && requests == 1 && !result.ok(),
          "malformed layer is fetched again instead of permanently poisoning the cache");
    check(terrain.heights == original.heights &&
              terrain.canopyHeightDm == original.canopyHeightDm &&
              terrain.canopyCoverPct == original.canopyCoverPct,
          "network cancellation and malformed data leave existing vegetation intact");
    validTiffs = true;
    requests = 0;
    qputenv("RIDGELINE_CANOPY_URL",
            QString("http://127.0.0.1:%1/valid").arg(server.serverPort()).toUtf8());
    deadline.start(5000);
    result = CanopyLoader::loadForTerrain(network, terrain, {}, {}, [&] { return cancel; });
    deadline.stop();
    check(!timedOut && requests == 3 && result.ok(),
          "three local TIFF layers produce an installable canopy");
    if (!result.ok()) std::cerr << result.error.toStdString() << '\n';
    if (result.ok()) {
        check(result.canopy.heightDm.size() == 25 && result.canopy.heightDm[0] == 100 &&
                  result.canopy.coverPct[0] == 45 && result.canopy.coverage.assignedHeight == 25,
              "successful load applies the declared height assumption and cover");
        const auto savedHeights = result.canopy.heightDm;
        requests = 0;
        result = CanopyLoader::loadForTerrain(network, terrain, {}, {}, [&] { return cancel; });
        check(result.ok() && requests == 0 && result.canopy.heightDm == savedHeights,
              "cached canopy reload reproduces heights without additional HTTP requests");
    }
}

void testCanopyWindowBlocksCloseAndAutosave() {
    QTemporaryDir fixture;
    check(fixture.isValid(), "canopy window fixture directory exists");
    if (!fixture.isValid()) return;
    const auto oldUrl = qgetenv("RIDGELINE_CANOPY_URL");
    const auto oldCache = qgetenv("RIDGELINE_CACHE_DIR");
    const auto oldState = qgetenv("RIDGELINE_STATE_DIR");
    const auto oldOffline = qgetenv("RIDGELINE_OFFLINE");
    const auto restore = qScopeGuard([&] {
        qputenv("RIDGELINE_CANOPY_URL", oldUrl);
        qputenv("RIDGELINE_CACHE_DIR", oldCache);
        qputenv("RIDGELINE_STATE_DIR", oldState);
        qputenv("RIDGELINE_OFFLINE", oldOffline);
    });
    qputenv("RIDGELINE_CACHE_DIR", fixture.filePath("cache").toUtf8());
    qputenv("RIDGELINE_STATE_DIR", fixture.filePath("state").toUtf8());
    qputenv("RIDGELINE_OFFLINE", "1");
    QFile hgt(fixture.filePath("N35W078.hgt"));
    check(hgt.open(QIODevice::WriteOnly), "canopy terrain fixture opens");
    const QByteArray flat(1201 * 1201 * 2, '\0');
    check(hgt.write(flat) == flat.size(), "canopy terrain fixture is complete");
    hgt.close();
    MainWindow window;
    window.show();
    window.loadTerrain(hgt.fileName());
    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost, 0), "canopy window server listens");
    if (!server.isListening()) return;
    qputenv("RIDGELINE_CANOPY_URL",
            QString("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
    qputenv("RIDGELINE_OFFLINE", "0");
    bool exercised = false;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        auto* progress = window.findChild<QProgressDialog*>();
        check(progress != nullptr, "active canopy request has progress dialog");
        if (!progress) return;
        exercised = true;
        check(!window.close() && window.isVisible(), "close is refused during canopy download");
        auto* timer = window.findChild<QTimer*>("AutosaveTimer");
        check(timer != nullptr, "autosave timer exists during canopy download");
        if (timer) QMetaObject::invokeMethod(timer, "timeout");
        check(!RecoveryStore::exists(), "autosave skips an in-progress canopy load");
        progress->cancel();
    });
    QTimer deadline;
    QObject::connect(&deadline, &QTimer::timeout, [&] {
        if (auto* progress = window.findChild<QProgressDialog*>()) progress->cancel();
    });
    deadline.start(5000);
    QMetaObject::invokeMethod(&window, "onLoadCanopy");
    deadline.stop();
    check(exercised, "window guard checked during actual local network request");
    if (auto* timer = window.findChild<QTimer*>("AutosaveTimer"))
        QMetaObject::invokeMethod(timer, "timeout");
    check(RecoveryStore::exists(), "autosave resumes after canopy cancellation");
    QObject::disconnect(&server, &QTcpServer::newConnection, &server, nullptr);
    while (auto* socket = server.nextPendingConnection()) socket->deleteLater();
    int canopyRequests = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto* socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                if (socket->property("answered").toBool()) return;
                socket->setProperty("answered", true);
                socket->readAll();
                ++canopyRequests;
                TiffBuilder tiff;
                tiff.width = tiff.height = 5;
                tiff.samples.assign(25, canopyRequests == 1 ? 390 : canopyRequests == 2 ? 45 : 13);
                const auto bytes = tiff.build();
                const QByteArray body(reinterpret_cast<const char*>(bytes.data()),
                                      static_cast<qsizetype>(bytes.size()));
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                              "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        }
    });
    deadline.start(5000);
    withModalAnswer(QMessageBox::Ok, [&] { QMetaObject::invokeMethod(&window, "onLoadCanopy"); });
    deadline.stop();
    check(canopyRequests == 3, "window canopy install fetches three fixture layers");
    if (auto* timer = window.findChild<QTimer*>("AutosaveTimer"))
        QMetaObject::invokeMethod(timer, "timeout");
    const auto installed = RecoveryStore::read();
    check(installed.canopyImport.enabled && installed.canopyImport.developedPolicy == "assume" &&
              installed.canopyImport.assumedDevelopedCanopyM == 10.0f,
          "window installation records imported canopy and matching options in recovery");
    // Force a replacement failure without modifying the valid cached import.
    qputenv("RIDGELINE_CACHE_DIR", fixture.filePath("empty-replacement-cache").toUtf8());
    qputenv("RIDGELINE_OFFLINE", "1");
    bool optionsEdited = false;
    QTimer editOptions;
    QObject::connect(&editOptions, &QTimer::timeout, [&] {
        auto* dialog = window.findChild<QDialog*>("CanopyOptionsDialog");
        if (!dialog || !dialog->isVisible()) return;
        editOptions.stop();
        auto* policy = dialog->findChild<QComboBox*>("DevelopedCanopyPolicy");
        auto* height = dialog->findChild<QDoubleSpinBox*>("AssumedCanopyHeight");
        check(policy && height, "loaded-canopy options are editable");
        if (policy && height) {
            policy->setCurrentIndex(2);
            height->setValue(20.0);
            optionsEdited = true;
        }
        dialog->accept();
    });
    editOptions.start(10);
    withModalAnswer(QMessageBox::Ok, [&] {
        QMetaObject::invokeMethod(&window, "onCanopyOptions");
    });
    editOptions.stop();
    check(optionsEdited && sawPrompt, "replacement failure is reported after accepting changed options");
    if (auto* timer = window.findChild<QTimer*>("AutosaveTimer"))
        QMetaObject::invokeMethod(timer, "timeout");
    const auto retained = RecoveryStore::read();
    check(retained.canopyImport.enabled &&
              retained.canopyImport.developedPolicy == installed.canopyImport.developedPolicy &&
              retained.canopyImport.assumedDevelopedCanopyM == installed.canopyImport.assumedDevelopedCanopyM &&
              retained.canopyImport.retrievedUtc == installed.canopyImport.retrievedUtc,
          "failed replacement keeps the installed import and original options in recovery");
    qputenv("RIDGELINE_OFFLINE", "0");
    QObject::disconnect(&server, &QTcpServer::newConnection, &server, nullptr);
    bool replacementCancelled = false;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto* socket = server.nextPendingConnection()) socket->deleteLater();
        if (auto* progress = window.findChild<QProgressDialog*>()) {
            replacementCancelled = true;
            progress->cancel();
        }
    });
    optionsEdited = false;
    editOptions.start(10);
    deadline.start(5000);
    QMetaObject::invokeMethod(&window, "onCanopyOptions");
    deadline.stop();
    editOptions.stop();
    check(optionsEdited && replacementCancelled,
          "changed canopy options can be cancelled during replacement download");
    if (auto* timer = window.findChild<QTimer*>("AutosaveTimer"))
        QMetaObject::invokeMethod(timer, "timeout");
    const auto afterCancel = RecoveryStore::read();
    check(afterCancel.canopyImport.enabled &&
              afterCancel.canopyImport.developedPolicy == installed.canopyImport.developedPolicy &&
              afterCancel.canopyImport.assumedDevelopedCanopyM == installed.canopyImport.assumedDevelopedCanopyM &&
              afterCancel.canopyImport.retrievedUtc == installed.canopyImport.retrievedUtc,
          "cancelled replacement keeps original imported canopy settings in recovery");
    withModalAnswer(QMessageBox::Discard, [&] { window.close(); });
}

void testOverlayCheckboxesMatchWhatTheViewportDraws() {
    // Custom row widgets do not supply the list item's accessible text.
    // Check initial, external and control-originated changes without rebuilding.
    {
        LayerStack stack;
        LayersPanel panel;
        panel.setStack(&stack);
        auto* list = panel.findChild<QListWidget*>("LayerList");
        QListWidgetItem* coverage = nullptr;
        for (int i = 0; list && i < list->count(); ++i) {
            auto* item = list->item(i);
            check(!item->data(Qt::AccessibleTextRole).toString().isEmpty(),
                  "each layer row exposes an accessible name");
            if (item->data(Qt::UserRole).toString() == "coverage") coverage = item;
        }
        auto* slider = panel.findChild<QSlider*>("LayerOpacitycoverage");
        auto* box = panel.findChild<QCheckBox*>("LayerVisiblecoverage");
        check(coverage && slider && box, "coverage accessibility controls exist");
        if (coverage && slider && box) {
            stack.setOpacity("coverage", 0.37f);
            stack.setVisible("coverage", false);
            check(slider->value() == 37 && slider->toolTip() == "Coverage opacity: 37%",
                  "external layer updates keep opacity tooltip current");
            check(coverage->data(Qt::AccessibleDescriptionRole).toString() == "Hidden; opacity 37%",
                  "external layer updates keep accessible state current");
            box->setChecked(true);
            slider->setValue(62);
            check(coverage->data(Qt::AccessibleDescriptionRole).toString() == "Shown; opacity 62%",
                  "control changes keep accessible state current without replacing rows");
        }
    }
    MainWindow window;
    window.resize(1050, 700);
    window.show();
    settle();
    std::cerr << "Laptop layout: " << window.width() << " x " << window.height()
              << "; minimum hint " << window.minimumSizeHint().width() << " x "
              << window.minimumSizeHint().height() << "\n";
    check(window.width() <= 1050 && window.height() <= 700,
          "workspace fits the requested laptop-size window");
    auto* viewport = window.findChild<ViewportWidget*>();
    if (!viewport) { check(false, "found the viewport"); return; }
    auto* legend = window.findChild<HeatmapLegend*>();
    check(legend && legend->isHidden(), "unsolved scene has no result legend");

    // Two unlinked places decide what is on screen: the layer stack that the
    // panel writes to, and the overlay objects the renderer reads. They can
    // drift, so every row is checked against what the viewport will draw.
    auto* layerList = window.findChild<QListWidget*>("LayerList");
    check(layerList != nullptr, "the Layers dock lists its layers");
    check(layerList && layerList->count() == 10, "every layer has a row");
    for (const char* id : {"satellite", "usgs-topo", "hillshade", "vegetation",
                           "coverage", "comparison", "los", "pattern", "jamming",
                           "fresnel"}) {
        auto* box = window.findChild<QCheckBox*>(QString("LayerVisible%1").arg(id));
        check(box != nullptr, "found the layer visibility toggle");
        if (!box) continue;
        // Two layers are data-gated: with no comparison computed and no
        // vegetation loaded there is nothing to draw, so their rows can be
        // checked while the viewport correctly draws nothing. Every other layer
        // must match exactly.
        const bool dataGated = QLatin1String(id) == QLatin1String("comparison") ||
                               QLatin1String(id) == QLatin1String("vegetation");
        check(dataGated || box->isChecked() == viewport->isLayerVisible(id),
              "layer row matches what the viewport draws at startup");
        box->setChecked(!box->isChecked());
        settle(60);
        if (dataGated) {
            check(!viewport->isLayerVisible(id),
                  "a data-gated layer stays undrawn until it has data");
        } else {
            check(box->isChecked() == viewport->isLayerVisible(id),
                  "toggling a layer row changes what the viewport draws");
        }
        box->setChecked(!box->isChecked());
        settle(60);
    }

    // Opacity reaches the stack, and a hidden layer keeps its value.
    auto* coverageOpacity = window.findChild<QSlider*>("LayerOpacitycoverage");
    check(coverageOpacity != nullptr, "a layer row carries an opacity control");
    if (coverageOpacity) {
        coverageOpacity->setValue(40);
        settle(60);
        check(coverageOpacity->value() == 40, "opacity keeps the value it was given");
    }

    // Map furniture: a scale bar that reports a real ground distance, and a
    // north arrow that tracks the camera.
    auto* scaleBar = window.findChild<MapScaleBar*>("MapScaleBar");
    auto* north = window.findChild<NorthIndicator*>("NorthIndicator");
    check(scaleBar && north, "the viewport carries a scale bar and a north indicator");
    if (scaleBar) {
        check(scaleBar->isVisible(), "the scale bar is shown once a camera exists");
        check(scaleBar->metresPerPixel() > 0.0, "the scale bar has a real ground scale");
        check(scaleBar->barMetres() > 0.0 && !scaleBar->barLabel().isEmpty(),
              "the scale bar reports a labelled distance");
        // The demo scene is 2000 m across, so a bar spanning the whole view
        // would be wrong by orders of magnitude either way.
        check(scaleBar->barMetres() <= 2000.0,
              "the scale bar is not larger than the scene it measures");
        const QRect bounds(scaleBar->mapTo(&window, QPoint{}), scaleBar->size());
        check(window.rect().contains(bounds), "the scale bar is not clipped");
    }
    if (north && viewport) {
        const double before = north->cameraAzimuth();
        viewport->topView();
        settle(80);
        viewport->resetView();
        settle(80);
        check(std::abs(north->cameraAzimuth() - before) < 1e-6,
              "resetting the view restores the north indicator's bearing");
        check(!north->toolTip().isEmpty(), "the north indicator explains itself");
        // Camera parks the eye at +Z for azimuth zero and looks back at the
        // target, so it faces -Z. Terrain rows run south to north with rising
        // Z, so azimuth zero is looking SOUTH and north is at the bottom of
        // the screen. An arrow drawn at -azimuth points exactly backwards.
        north->setCameraAzimuth(0.0);
        check(std::abs(north->northScreenAngleDeg() - 180.0) < 1e-9,
              "at azimuth zero the camera looks south, so north points down");
        north->setCameraAzimuth(90.0);
        check(std::abs(north->northScreenAngleDeg() - 90.0) < 1e-9,
              "at azimuth ninety north is a quarter turn clockwise from up");
        north->setCameraAzimuth(180.0);
        check(std::abs(north->northScreenAngleDeg()) < 1e-9,
              "at azimuth one eighty the camera looks north, so north points up");
        north->setCameraAzimuth(-90.0);
        check(std::abs(north->northScreenAngleDeg() - 270.0) < 1e-9,
              "a negative azimuth wraps into range");
        north->setCameraAzimuth(before);
    }

    // The ruler: two terrain points give a ground distance, and it must not
    // fight the placement tool for the same click.
    auto* measure = window.findChild<QToolButton*>("MeasureButton");
    check(measure && measure->isCheckable(), "the toolbar offers a measure toggle");
    if (measure && viewport) {
        measure->setChecked(true);
        settle(60);
        check(viewport->measureMode(), "the toggle turns the ruler on");
        check(viewport->measurementSummary().isEmpty(),
              "no measurement exists before any point is taken");

        // Placement and measuring both consume a terrain click, so starting
        // one must cancel the other rather than leaving the click ambiguous.
        viewport->setPlacementMode(true);
        settle(60);
        viewport->setMeasureMode(true);
        settle(60);
        check(viewport->measureMode(), "measuring survives being re-enabled");

        viewport->setMeasureMode(false);
        settle(60);
        check(!viewport->measureMode() && !measure->isChecked(),
              "turning the ruler off unchecks the toolbar toggle");
        check(viewport->measurementSummary().isEmpty(),
              "leaving the ruler clears its measurement");
    }

    if (viewport) {
        auto* sources = window.findChild<QListWidget*>("SourceList");
        const int sourceCount = sources ? sources->count() : -1;
        int cancelled = 0, placements = 0, moves = 0;
        const auto cancelConnection = QObject::connect(viewport, &ViewportWidget::placementCancelled,
            [&] { ++cancelled; });
        const auto placementConnection = QObject::connect(viewport, &ViewportWidget::terrainClicked,
            [&](float, float) { ++placements; });
        const auto moveConnection = QObject::connect(viewport, &ViewportWidget::antennaMoveRequested,
            [&](int, float, float) { ++moves; });
        check(QMetaObject::invokeMethod(&window, "onAddAntenna"),
              "placement cancellation starts through the main-window action");
        const QPointF center(viewport->rect().center());
        QMouseEvent press(QEvent::MouseButtonPress, center, viewport->mapToGlobal(center.toPoint()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &press);
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(viewport, &escape);
        QMouseEvent release(QEvent::MouseButtonRelease, center, viewport->mapToGlobal(center.toPoint()),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &release);
        QApplication::sendEvent(viewport, &escape);
        settle(60);
        check(cancelled == 1, "Escape cancels active placement exactly once");
        check(placements == 0 && moves == 0 && sources && sources->count() == sourceCount,
              "release after cancelled placement cannot add or move a source");
        viewport->topView();
        settle(80);
        check(QMetaObject::invokeMethod(&window, "onAddAntenna"), "placement can restart after Escape");
        QApplication::sendEvent(viewport, &press);
        QApplication::sendEvent(viewport, &release);
        settle(60);
        check(placements == 1 && sources && sources->count() == sourceCount + 1,
              "restarted placement adds exactly one source on terrain");
        QApplication::sendEvent(viewport, &press);
        QApplication::sendEvent(viewport, &release);
        settle(60);
        check(placements == 1 && sources && sources->count() == sourceCount + 1,
              "completed placement returns to selection instead of adding repeatedly");
        if (auto* history = window.findChild<QUndoStack*>()) {
            history->undo();
            check(sources && sources->count() == sourceCount, "placement is undoable");
        } else check(false, "placement exposes its undo history");
        viewport->resetView();
        QObject::disconnect(cancelConnection);
        QObject::disconnect(placementConnection);
        QObject::disconnect(moveConnection);
    }

    // The cursor readout exists and starts hidden: it must never report a
    // position the cursor is not actually over.
    auto* readout = window.findChild<QLabel*>("CursorReadout");
    check(readout != nullptr, "the viewport carries a cursor readout");
    if (readout && viewport) {
        // Asserting the initial state here was flaky: if the developer's real
        // pointer happened to sit over the window, Qt delivered a move event
        // and the readout legitimately appeared. Drive the guarantee that
        // actually matters instead - once the cursor leaves, the readout must
        // not linger over a map it is no longer pointing at.
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(viewport, &leave);
        settle(60);
        check(readout->isHidden(),
              "the cursor readout hides once the cursor leaves the map");
    }

    // Every action a user is expected to find must be reachable from a menu
    // with a visible shortcut, not only from a toolbar button.
    const QList<QAction*> actions = window.findChildren<QAction*>();
    const auto actionWithText = [&actions](const QString& text) -> QAction* {
        for (QAction* action : actions)
            if (action->text().contains(text)) return action;
        return nullptr;
    };
    for (const auto& expected : QList<QPair<QString, QString>>{
             {"Run Coverage", "F5"}, {"Measure Distance", "M"},
             {"Top-down view", "T"}, {"Reset map view", "R"},
             {"Keyboard Shortcuts", "F1"}}) {
        QAction* action = actionWithText(expected.first);
        check(action != nullptr,
              qPrintable(QStringLiteral("%1 is reachable from a menu").arg(expected.first)));
        if (action)
            check(action->shortcut() == QKeySequence(expected.second),
                  qPrintable(QStringLiteral("%1 carries its documented shortcut")
                                 .arg(expected.first)));
    }

    // The menu entry and the toolbar toggle must agree, since both drive the
    // same mode and either can be used first.
    if (auto* measureAction = actionWithText("Measure Distance")) {
        auto* measureButton = window.findChild<QToolButton*>("MeasureButton");
        measureAction->setChecked(true);
        settle(60);
        check(viewport && viewport->measureMode(), "the menu entry turns the ruler on");
        check(measureButton && measureButton->isChecked(),
              "the toolbar toggle follows the menu entry");
        measureAction->setChecked(false);
        settle(60);
        check(viewport && !viewport->measureMode(), "the menu entry turns the ruler off");
    }

    // A workspace dragged into a mess must be recoverable without a restart.
    auto* resetLayout = window.findChild<QAction*>("ResetLayoutAction");
    check(resetLayout != nullptr, "the workspace layout can be reset from a menu");
    if (resetLayout) {
        auto* layersDock = window.findChild<QDockWidget*>("LayersDock");
        check(layersDock != nullptr, "the layers dock exists to be disarranged");
        if (layersDock) {
            const bool wasVisible = !layersDock->isHidden();
            layersDock->hide();
            settle(60);
            resetLayout->trigger();
            settle(120);
            check(!layersDock->isHidden() == wasVisible,
                  "resetting the layout restores a closed dock");
        }
    }

    auto* dock = window.findChild<QDockWidget*>("SourcesDock");
    auto* toggle = window.findChild<QToolButton*>("SourcesPanelButton");
    check(dock && toggle, "source dock and toolbar toggle exist");
    if (dock && toggle) {
        toggle->click();
        check(dock->isHidden() && !toggle->isChecked(), "source panel can be hidden");
        toggle->click();
        check(!dock->isHidden() && toggle->isChecked(), "source panel can be restored");
    }
    for (const char* name : {"RunButton", "CoverageQualityCombo", "CoverageMetricCombo",
                             "TopMapViewButton", "ResetMapViewButton"}) {
        auto* control = window.findChild<QWidget*>(name);
        check(control && control->isVisible(), (std::string(name) + " visible at laptop size").c_str());
        if (control) {
            const QRect bounds(control->mapTo(&window, QPoint{}), control->size());
            check(window.rect().contains(bounds), (std::string(name) + " not clipped").c_str());
        }
    }
    auto* summary = window.findChild<QLabel*>("LocationSummary");
    check(summary && summary->width() >= 140, "location summary remains readable");
    check(summary && summary->textFormat() == Qt::PlainText,
          "location names are displayed literally, never interpreted as markup");
    // The tooltip must separate the grid it draws from the data behind it. The
    // procedural demo scene has no source resolution, so it has to say so
    // rather than let the grid spacing be mistaken for the data's resolution.
    check(summary && summary->toolTip().contains("200 × 200 samples") &&
              summary->toolTip().contains("Source resolution is not known"),
          "terrain quality tooltip distinguishes sampling from source resolution");
    auto* credit = window.findChild<QLabel*>("ElevationAttribution");
    check(credit && credit->text().contains("Synthetic"), "demo elevation has correct credit");
    auto* sources = window.findChild<QListWidget*>("SourceList");
    if (sources) sources->setCurrentRow(0);
    settle(100);
    auto* position = window.findChild<QLabel*>("SourcePositionSummary");
    check(position && position->text().contains("Synthetic heights") &&
              !position->text().contains("MSL"),
          "demo source heights do not claim a sea-level datum");
    check(window.width() <= 1050 && window.height() <= 700,
          "selected source still fits laptop window");
    auto* advanced = window.findChild<QToolButton*>("AdvancedSettingsToggle");
    if (advanced) advanced->click();
    settle(100);
    check(advanced && advanced->isVisible(), "advanced settings remain reachable");
    check(window.width() <= 1050 && window.height() <= 700,
          "expanded source settings scroll instead of enlarging the window");
    withModalAnswer(QMessageBox::Discard, [&] { window.close(); });
}

void testTerrainFailureAndCancellationPreserveWorkspace() {
    QTemporaryDir cache;
    QTcpServer server;
    check(cache.isValid() && server.listen(QHostAddress::LocalHost, 0),
          "isolated terrain-load fixture is available");
    if (!cache.isValid() || !server.isListening()) return;
    const QByteArray oldCache = qgetenv("RIDGELINE_CACHE_DIR");
    const QByteArray oldOffline = qgetenv("RIDGELINE_OFFLINE");
    const QByteArray oldUrl = qgetenv("RIDGELINE_TERRAIN_URL");
    const auto envGuard = qScopeGuard([&] {
        qputenv("RIDGELINE_CACHE_DIR", oldCache);
        qputenv("RIDGELINE_OFFLINE", oldOffline);
        qputenv("RIDGELINE_TERRAIN_URL", oldUrl);
    });
    qputenv("RIDGELINE_CACHE_DIR", cache.path().toUtf8());
    qputenv("RIDGELINE_OFFLINE", "0");
    qputenv("RIDGELINE_TERRAIN_URL", QString("http://127.0.0.1:%1/{z}/{x}/{y}.png")
        .arg(server.serverPort()).toUtf8());
    int connections = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            QTcpSocket* socket = server.nextPendingConnection();
            ++connections; // Deliberately do not respond: cancellation must abort.
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    MainWindow window;
    window.show();
    settle(200);
    auto* run = findButton(window, "Run coverage");
    auto* sources = window.findChild<QListWidget*>("SourceList");
    auto* summary = window.findChild<QLabel*>("LocationSummary");
    auto* status = window.findChild<QLabel*>("WorkspaceStatus");
    auto* legend = window.findChild<HeatmapLegend*>();
    check(run && sources && summary && status && legend, "terrain fixture workspace is ready");
    if (!run || !sources || !summary || !status || !legend) return;
    run->click();
    const QString initialSummary = summary->text();
    const QString initialSource = sources->item(0)->text();
    check(legend->isVisible(), "fixture has computed coverage before loading");

    withModalAnswer(QMessageBox::Ok, [&] { window.loadTerrain(cache.filePath("missing.hgt")); });
    check(sawPrompt && status->text().contains("Previous terrain preserved"),
          "invalid terrain file reports failure and preservation");
    check(legend->isVisible() && summary->text() == initialSummary &&
              sources->item(0)->text() == initialSource,
          "invalid file leaves coverage and sources intact");

    // Real DTED records exercise decoding failures after opening the file.
    // Both profiles are complete; one case has a checksummed void and another
    // has a damaged checksum. Neither may replace the computed workspace.
    QByteArray dted(3428 + 2 * 16, ' ');
    auto field = [&](int at, const QByteArray& value) {
        dted.replace(at, value.size(), value);
    };
    field(0, "UHL1"); field(4, "0100000E"); field(12, "0450000N");
    field(20, "0010"); field(24, "0010");
    field(47, "0002"); field(51, "0002");
    field(80, "DSI"); field(224, "WGS84"); field(728, "ACC");
    for (int col = 0; col < 2; ++col) {
        const int at = 3428 + col * 16;
        field(at, QByteArray(16, '\0'));
        dted[at] = char(0xaa); dted[at + 3] = char(col); dted[at + 5] = char(col);
        dted[at + 9] = 10; dted[at + 11] = 20;
        if (col == 1) { dted[at + 10] = char(0xff); dted[at + 11] = char(0xff); }
        quint32 sum = 0;
        for (int i = 0; i < 12; ++i) sum += static_cast<unsigned char>(dted[at + i]);
        for (int i = 0; i < 4; ++i) dted[at + 12 + i] = char((sum >> (24 - i * 8)) & 255);
    }
    const QString initialTitle = window.windowTitle();
    for (bool damageChecksum : {false, true}) {
        QByteArray bytes = dted;
        if (damageChecksum) bytes[3428 + 15] = char(bytes[3428 + 15] ^ 1);
        const QString path = cache.filePath(damageChecksum ? "checksum.dt1" : "void.dt1");
        QFile file(path);
        check(file.open(QIODevice::WriteOnly), "DTED recovery fixture opens for writing");
        check(file.write(bytes) == bytes.size(), "DTED recovery fixture is complete");
        file.close();
        QString diagnostic;
        QTimer observer;
        QObject::connect(&observer, &QTimer::timeout, [&] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                diagnostic = box->text();
        });
        observer.start(10);
        withModalAnswer(QMessageBox::Ok, [&] { window.loadTerrain(path); });
        observer.stop();
        check(sawPrompt && diagnostic.contains(damageChecksum ? "checksum mismatch" : "missing elevation"),
              "DTED load exposes the specific decoder diagnostic");
        check(status->text().contains("Previous terrain preserved") && legend->isVisible() &&
                  summary->text() == initialSummary && sources->item(0)->text() == initialSource &&
                  window.windowTitle() == initialTitle && run->isEnabled(),
              "DTED decoding failure preserves coverage, source, title and usable controls");
    }

    auto chooseLocation = [&](bool cancelLoad) {
        bool chose = false, sawLoad = false, closeRefused = false;
        bool errorShown = false, stackedProgress = false;
        QTimer driver;
        QObject::connect(&driver, &QTimer::timeout, [&] {
            if (auto* dialog = qobject_cast<LocationDialog*>(QApplication::activeModalWidget())) {
                if (!chose) { chose = true; dialog->accept(); }
                return;
            }
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                errorShown = true;
                for (auto* progress : window.findChildren<QProgressDialog*>())
                    stackedProgress |= progress->isVisible();
                box->accept();
                return;
            }
            auto* progress = window.findChild<QProgressDialog*>();
            if (cancelLoad && progress && progress->isVisible() && connections > 0 && !sawLoad) {
                sawLoad = true;
                closeRefused = !window.close();
                progress->cancel();
            }
        });
        driver.start(30);
        QMetaObject::invokeMethod(&window, "onChooseLocation");
        driver.stop();
        check(chose, "location dialog was accepted");
        if (cancelLoad) {
            check(sawLoad && closeRefused && window.isVisible(),
                  "close is refused during active terrain downloads");
            check(!errorShown && status->text().contains("cancelled"),
                  "terrain cancellation exits without an error prompt");
        } else {
            check(errorShown && !stackedProgress && status->text().contains("Previous terrain preserved"),
                  "offline load failure dismisses progress before showing an error");
        }
        check(legend->isVisible() && summary->text() == initialSummary &&
                  sources->item(0)->text() == initialSource,
              "unsuccessful location load preserves the computed scenario");
    };
    chooseLocation(true);
    qputenv("RIDGELINE_OFFLINE", "1");
    chooseLocation(false);
    run->click();
    check(run->isEnabled() && status->text().contains("Coverage ready"),
          "coverage can run again after load cancellation and failure");
    withModalAnswer(QMessageBox::Discard, [&] { window.close(); });
    check(!sawPrompt, "failed or cancelled loads do not dirty the original scenario");

    // Repair the void and checksum, then retry in the same window. A successful
    // import must replace stale results and become an unsaved scenario.
    const int finalProfile = 3428 + 16;
    dted[finalProfile + 10] = 0;
    dted[finalProfile + 11] = 30;
    quint32 repairedSum = 0;
    for (int i = 0; i < 12; ++i)
        repairedSum += static_cast<unsigned char>(dted[finalProfile + i]);
    for (int i = 0; i < 4; ++i)
        dted[finalProfile + 12 + i] = char((repairedSum >> (24 - i * 8)) & 255);
    const QString repairedName = "repaired-long-location-name-for-compact-toolbar-layout-verification.dt1";
    const QString repairedPath = cache.filePath(repairedName);
    QFile repairedFile(repairedPath);
    check(repairedFile.open(QIODevice::WriteOnly), "repaired DTED fixture opens");
    check(repairedFile.write(dted) == dted.size(), "repaired DTED fixture is complete");
    repairedFile.close();
    window.show();
    auto pickTerrain = [&](bool cancel) {
        bool sawPicker = false, timedOut = false, error = false;
        QElapsedTimer elapsed;
        elapsed.start();
        QTimer driver;
        QObject::connect(&driver, &QTimer::timeout, [&] {
            if (auto* dialog = window.findChild<QFileDialog*>(); dialog && dialog->isVisible()) {
                sawPicker = true;
                if (cancel || elapsed.elapsed() > 10000) {
                    timedOut = !cancel;
                    dialog->reject();
                } else if (auto* edit = dialog->findChild<QLineEdit*>("fileNameEdit")) {
                    edit->setText(repairedPath);
                    QMetaObject::invokeMethod(dialog, "accept");
                }
            } else if (auto* box = window.findChild<QMessageBox*>(); box && box->isVisible()) {
                error = true;
                box->reject();
            }
        });
        driver.start(25);
        check(QMetaObject::invokeMethod(&window, "onLoadTerrain"), "Load Terrain action is reachable");
        driver.stop();
        check(sawPicker && !timedOut && !error, "terrain file picker completes without errors or timeout");
        sawPrompt = error;
    };
    auto* terrainViewport = window.findChild<ViewportWidget*>();
    check(terrainViewport != nullptr, "terrain picker fixture exposes its viewport");
    if (terrainViewport) {
        auto* north = terrainViewport->findChild<NorthIndicator*>();
        auto* scale = terrainViewport->findChild<MapScaleBar*>();
        check(north && scale, "camera cancellation fixture exposes map orientation and scale");
        const double originalAzimuth = north ? north->cameraAzimuth() : 0.0;
        const double originalScale = scale ? scale->metresPerPixel() : 0.0;
        QKeyEvent orbit(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        QApplication::sendEvent(terrainViewport, &orbit);
        const QPointF center(terrainViewport->rect().center());
        QWheelEvent zoom(center, terrainViewport->mapToGlobal(center.toPoint()),
                         QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                         Qt::NoScrollPhase, false);
        QApplication::sendEvent(terrainViewport, &zoom);
        settle(80);
        const double changedAzimuth = north ? north->cameraAzimuth() : 0.0;
        const double changedScale = scale ? scale->metresPerPixel() : 0.0;
        check(north && std::abs(changedAzimuth - originalAzimuth) > 1.0,
              "fixture orbits away from the initial view");
        check(scale && changedScale > 0.0 && changedScale < originalScale,
              "fixture zooms away from the initial scale");
        const QImage beforeTilt = terrainViewport->grabFramebuffer();
        QKeyEvent tilt(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
        QApplication::sendEvent(terrainViewport, &tilt);
        settle(80);
        const QImage afterTilt = terrainViewport->grabFramebuffer();
        check(!beforeTilt.isNull() && afterTilt != beforeTilt,
              "view elevation input changes the rendered scene");
        const auto mouse = [&](QEvent::Type type, QPointF point,
                               Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent event(type, point, terrainViewport->mapToGlobal(point.toPoint()),
                              button, buttons, Qt::NoModifier);
            QApplication::sendEvent(terrainViewport, &event);
        };
        mouse(QEvent::MouseButtonPress, center, Qt::MiddleButton, Qt::MiddleButton);
        mouse(QEvent::MouseMove, center + QPointF(45, 25), Qt::NoButton, Qt::MiddleButton);
        mouse(QEvent::MouseButtonRelease, center + QPointF(45, 25), Qt::MiddleButton, Qt::NoButton);
        settle(80);
        check(terrainViewport->grabFramebuffer() != afterTilt,
              "middle-button pan changes the rendered scene");
        // Exercise both settings for each layer, so a reset to defaults cannot
        // pass merely because the test started with those same defaults.
        for (bool visible : {false, true}) {
            for (const char* id : {"satellite", "usgs-topo", "hillshade", "vegetation",
                                   "coverage", "comparison", "los", "pattern",
                                   "jamming", "fresnel"}) {
                if (auto* box = window.findChild<QCheckBox*>(QString("LayerVisible%1").arg(id)))
                    box->setChecked(visible);
            }
            settle(80);
            const QImage beforePicker = terrainViewport->grabFramebuffer();
            check(!beforePicker.isNull(), "camera preservation has a rendered baseline");
            pickTerrain(true);
            settle(80);
            check(terrainViewport->grabFramebuffer() == beforePicker,
                  "terrain picker cancellation preserves the panned and tilted render");
            check(north && std::abs(north->cameraAzimuth() - changedAzimuth) < 1e-6,
                  "terrain picker cancellation preserves camera orientation");
            check(scale && std::abs(scale->metresPerPixel() - changedScale) < 1e-6,
                  "terrain picker cancellation preserves zoom scale");
            for (const char* id : {"satellite", "usgs-topo", "hillshade", "vegetation",
                                   "coverage", "comparison", "los", "pattern",
                                   "jamming", "fresnel"}) {
                auto* box = window.findChild<QCheckBox*>(QString("LayerVisible%1").arg(id));
                check(box && box->isChecked() == visible,
                      "terrain picker cancellation preserves every layer visibility setting");
            }
        }
    } else {
        pickTerrain(true);
    }
    check(legend->isVisible() && summary->text() == initialSummary && window.windowTitle() == initialTitle,
          "cancelling the terrain file picker preserves the solved workspace");
    pickTerrain(false);
    window.resize(1050, 700);
    settle(150);
    check(window.width() <= 1050 && window.height() <= 700,
          "long imported terrain name does not enlarge the laptop window");
    check(summary->height() >= summary->heightForWidth(summary->width()),
          "long imported terrain summary has enough height for wrapped text");
    for (const char* controlName : {"LocationButton", "CoverageQualityCombo", "RunButton"}) {
        auto* control = window.findChild<QWidget*>(controlName);
        check(control && control->isVisible() && window.rect().contains(
                  QRect(control->mapTo(&window, QPoint{}), control->size())),
              "toolbar controls remain visible beside a long location name");
    }
    const QString captureDir = qEnvironmentVariable("RIDGELINE_QA_CAPTURE_DIR");
    auto* mapSummary = window.findChild<QLabel*>("MapTerrainSummary");
    check(mapSummary && mapSummary->isVisible() &&
              mapSummary->text().contains(summary->text().section('\n', 1)) &&
              !mapSummary->text().contains("0.0 × 0.0 km"),
          "on-map and toolbar summaries agree on small-area dimensions");
    if (mapSummary) {
        check(mapSummary->parentWidget()->rect().contains(mapSummary->geometry()),
              "on-map terrain summary fits the compact viewport");
    }
    if (!captureDir.isEmpty()) {
        const QString capturePath = QDir(captureDir).filePath("long-location-toolbar.png");
        check(QDir().mkpath(captureDir) && !QFile::exists(capturePath),
              "long location capture uses a new output path");
        if (!QFile::exists(capturePath))
            check(summary->parentWidget()->grab().save(capturePath), "long location toolbar captured");
        const QString mapCapture = QDir(captureDir).filePath("small-area-map-summary.png");
        check(!QFile::exists(mapCapture), "map summary capture uses a new output path");
        if (mapSummary && !QFile::exists(mapCapture))
            check(mapSummary->grab().save(mapCapture), "on-map small area summary captured");
        const QString workspaceCapture = QDir(captureDir).filePath("compact-workspace.png");
        check(!QFile::exists(workspaceCapture), "workspace capture uses a new output path");
        if (!QFile::exists(workspaceCapture))
            check(window.grab().save(workspaceCapture), "compact workspace captured");
    }
    check(!sawPrompt && status->text().contains("Terrain loaded"),
          "valid DTED retry succeeds after prior failures");
    check(window.isWindowModified(), "imported terrain activates the unsaved title indicator");
    check(summary->text().count('\n') == 1 && summary->text().section('\n', 1).contains(QChar(0x00a0)),
          "geographic summary separates the name and keeps area units together");
    check(summary->fontMetrics().horizontalAdvance(summary->text().section('\n', 1)) <=
              summary->contentsRect().width(), "area line fits the available summary width");
    check(!legend->isVisible() && summary->text().contains("repaired") &&
                  window.windowTitle().contains(repairedName) && run->isEnabled(),
          "successful DTED import clears stale coverage and refreshes workspace identity");
    withModalAnswer(QMessageBox::Cancel, [&] { window.close(); });
    check(sawPrompt && window.isVisible(), "successful DTED import is protected by the dirty-close guard");
    withModalAnswer(QMessageBox::Discard, [&] { window.close(); });
}

void testScenarioOpeningProgressAndRecovery() {
    QTemporaryDir cache;
    QTcpServer server;
    if (!cache.isValid() || !server.listen(QHostAddress::LocalHost, 0)) {
        check(false, "scenario load fixtures available"); return;
    }
    const QByteArray oldCache = qgetenv("RIDGELINE_CACHE_DIR");
    const QByteArray oldOffline = qgetenv("RIDGELINE_OFFLINE");
    const QByteArray oldTerrain = qgetenv("RIDGELINE_TERRAIN_URL");
    const QByteArray oldImagery = qgetenv("RIDGELINE_TILE_URL");
    const auto envGuard = qScopeGuard([&] {
        qputenv("RIDGELINE_CACHE_DIR", oldCache);
        qputenv("RIDGELINE_OFFLINE", oldOffline);
        qputenv("RIDGELINE_TERRAIN_URL", oldTerrain);
        qputenv("RIDGELINE_TILE_URL", oldImagery);
    });
    const QString terrainUrl = QString("http://127.0.0.1:%1/terrain/{z}/{x}/{y}.png")
        .arg(server.serverPort());
    qputenv("RIDGELINE_CACHE_DIR", cache.path().toUtf8());
    qputenv("RIDGELINE_OFFLINE", "0");
    qputenv("RIDGELINE_TERRAIN_URL", terrainUrl.toUtf8());
    qputenv("RIDGELINE_TILE_URL", QString("http://127.0.0.1:%1/imagery/{z}/{x}/{y}.png")
        .arg(server.serverPort()).toUtf8());
    int connections = 0;
    bool respond = false;
    QImage imagery(256, 256, QImage::Format_RGB32);
    imagery.fill(Qt::darkGreen);
    QByteArray imageBytes;
    QBuffer imageBuffer(&imageBytes);
    imageBuffer.open(QIODevice::WriteOnly);
    check(imagery.save(&imageBuffer, "PNG"), "encode local imagery fixture");
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            QTcpSocket* socket = server.nextPendingConnection();
            ++connections;
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                socket->readAll();
                if (!respond || socket->property("replied").toBool()) return;
                socket->setProperty("replied", true);
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: "
                    + QByteArray::number(imageBytes.size()) + "\r\nConnection: close\r\n\r\n" + imageBytes);
                socket->disconnectFromHost();
            });
        }
    });
    ScenarioDoc doc;
    doc.terrain.type = TerrainSource::Type::Global;
    doc.terrain.centerLat = 0;
    doc.terrain.centerLon = 0;
    doc.terrain.extentKm = 1;
    Antenna source;
    source.frequencyMHz = 440;
    source.powerW = 12;
    source.position = {500, 110, 500};
    doc.antennas = {source};
    const QString path = cache.filePath("scenario.json");
    ScenarioIO::save(doc, path.toStdString());

    MainWindow window;
    window.show();
    settle(200);
    auto* summary = window.findChild<QLabel*>("LocationSummary");
    auto* status = window.findChild<QLabel*>("WorkspaceStatus");
    auto* sources = window.findChild<QListWidget*>("SourceList");
    auto* legend = window.findChild<HeatmapLegend*>();
    auto* viewport = window.findChild<ViewportWidget*>();
    auto* run = findButton(window, "Run coverage");
    if (!summary || !status || !sources || !legend || !viewport || !run) {
        check(false, "scenario fixture workspace ready"); return;
    }
    run->click();
    const QString oldSummary = summary->text();
    const QString oldSource = sources->item(0)->text();
    const QString oldTitle = window.windowTitle();
    auto openScenario = [&](bool cancel, const QString& phase, bool expectError) {
        bool chose = false, cancelled = false, errorShown = false, stacked = false;
        bool closeRefused = false, timedOut = false;
        const int initialConnections = connections;
        QElapsedTimer elapsed;
        elapsed.start();
        QTimer driver;
        QObject::connect(&driver, &QTimer::timeout, [&] {
            // The user may be working in another app: target our owned dialog,
            // not whichever modal currently has desktop focus. Set the file
            // edit directly so asynchronous directory loading cannot clear it.
            if (auto* dialog = window.findChild<QFileDialog*>(); dialog && dialog->isVisible()) {
                if (elapsed.elapsed() > 10000) { timedOut = true; dialog->reject(); }
                else if (auto* edit = dialog->findChild<QLineEdit*>("fileNameEdit")) {
                    chose = true;
                    edit->setText(path);
                    QMetaObject::invokeMethod(dialog, "accept");
                }
                return;
            }
            if (auto* box = window.findChild<QMessageBox*>(); box && box->isVisible()) {
                errorShown = true;
                for (auto* progress : window.findChildren<QProgressDialog*>())
                    stacked |= progress->isVisible();
                box->accept();
                return;
            }
            auto* progress = window.findChild<QProgressDialog*>();
            if (!progress || !progress->isVisible()) return;
            if (cancel && !cancelled && connections > initialConnections &&
                progress->labelText().contains(phase, Qt::CaseInsensitive)) {
                closeRefused = !window.close();
                progress->cancel();
                cancelled = true;
            } else if (elapsed.elapsed() > 10000) { timedOut = true; progress->cancel(); }
        });
        driver.start(25);
        QMetaObject::invokeMethod(&window, "onOpenScenario");
        driver.stop();
        check(chose && !timedOut, "scenario opening completes without timeout");
        check(errorShown == expectError && !stacked, "scenario errors do not stack with loading progress");
        if (cancel) check(cancelled && closeRefused && window.isVisible(),
                          "scenario phase cancellation works and blocks closing");
        check(window.findChildren<QProgressDialog*>().isEmpty(), "scenario progress is cleaned up");
    };
    openScenario(true, "elevation", false);
    check(status->text().contains("Previous scenario preserved") && legend->isVisible() &&
          summary->text() == oldSummary && sources->item(0)->text() == oldSource &&
          window.windowTitle() == oldTitle, "cancelled scenario elevation preserves sources, coverage and title");
    qputenv("RIDGELINE_OFFLINE", "1");
    openScenario(false, {}, true);
    check(status->text().contains("Previous scenario preserved") && legend->isVisible() &&
          summary->text() == oldSummary && sources->item(0)->text() == oldSource,
          "failed scenario elevation preserves the workspace");

    // Preload only elevation; imagery remains a stalled network request.
    const QString prefix = "terrain/" + TileBatchDownloader::customProviderCacheKey(terrainUrl);
    for (int x : {16383, 16384}) {
        const QString folder = cache.filePath(QString("%1/15/%2").arg(prefix).arg(x));
        check(QDir().mkpath(folder), "create scenario elevation fixture directory");
        for (int y : {16383, 16384}) {
            QImage tile(256, 256, QImage::Format_ARGB32);
            tile.fill(qRgba(128, 100, 0, 255));
            check(tile.save(folder + QString("/%1.png").arg(y), "PNG"), "save scenario elevation fixture");
        }
    }
    qputenv("RIDGELINE_OFFLINE", "0");
    openScenario(true, "imagery", false);
    check(status->text().contains("Scenario loaded · Satellite imagery cancelled") &&
          sources->count() == 1 && sources->item(0)->text().contains("440 MHz") &&
          !legend->isVisible() && summary->text() != oldSummary && !viewport->basemapReady(),
          "imagery cancellation retains loaded scenario and clears old results");
    qputenv("RIDGELINE_OFFLINE", "1");
    openScenario(false, {}, false);
    check(status->text().contains("Satellite imagery unavailable") && !viewport->basemapReady(),
          "offline imagery miss is reported without rejecting the scenario");
    qputenv("RIDGELINE_OFFLINE", "0");
    respond = true;
    openScenario(false, {}, false);
    check(status->text().trimmed() == "Scenario loaded" && viewport->basemapReady(),
          "retry after imagery cancellation installs imagery successfully");
    withModalAnswer(QMessageBox::Discard, [&] { window.close(); });
    check(!sawPrompt, "loaded scenario remains clean after imagery recovery");
}

}  // namespace

// The application's own Export dialogs, end to end. A solved georeferenced map
// is exported through File > Export, reopened from disk and checked for the
// selected metric's legend, the elevation attribution and coordinate content,
// alongside picker cancellation, a deterministic write failure and a retry.
void testExportsThroughApplicationDialogs() {
    QTemporaryDir out;
    QTemporaryDir cache;
    check(out.isValid() && cache.isValid(), "export fixture directory is available");
    if (!out.isValid() || !cache.isValid()) return;

    // Keep the run hermetic: an empty cache in offline mode means no basemap
    // request reaches the network, so the export under test cannot depend on
    // a live tile provider or stall on one.
    const QByteArray oldCache = qgetenv("RIDGELINE_CACHE_DIR");
    const QByteArray oldOffline = qgetenv("RIDGELINE_OFFLINE");
    const auto envGuard = qScopeGuard([&] {
        qputenv("RIDGELINE_CACHE_DIR", oldCache);
        qputenv("RIDGELINE_OFFLINE", oldOffline);
    });
    qputenv("RIDGELINE_CACHE_DIR", cache.path().toUtf8());
    qputenv("RIDGELINE_OFFLINE", "1");

    // Georeferenced terrain, so exported rows carry latitude and longitude.
    // Origin 10E/45N at one arc-second spacing; heights are distinct, positive
    // and finite so a wrong sample cannot pass a range check by accident.
    const int samples = 16;
    const int recordSize = 8 + samples * 2 + 4;
    QByteArray dted(3428 + samples * recordSize, ' ');
    auto field = [&](int at, const QByteArray& value) {
        dted.replace(at, value.size(), value);
    };
    field(0, "UHL1"); field(4, "0100000E"); field(12, "0450000N");
    field(20, "0010"); field(24, "0010");
    const QByteArray count = QByteArray::number(samples).rightJustified(4, '0');
    field(47, count); field(51, count);
    field(80, "DSI"); field(224, "WGS84"); field(728, "ACC");
    for (int col = 0; col < samples; ++col) {
        const int at = 3428 + col * recordSize;
        field(at, QByteArray(recordSize, '\0'));
        dted[at] = char(0xaa);
        dted[at + 3] = char(col);
        dted[at + 5] = char(col);
        for (int row = 0; row < samples; ++row) {
            const int metres = 120 + row * 4 + col;
            dted[at + 8 + row * 2] = char((metres >> 8) & 0xff);
            dted[at + 9 + row * 2] = char(metres & 0xff);
        }
        quint32 sum = 0;
        for (int i = 0; i < recordSize - 4; ++i)
            sum += static_cast<unsigned char>(dted[at + i]);
        for (int i = 0; i < 4; ++i)
            dted[at + recordSize - 4 + i] = char((sum >> (24 - i * 8)) & 255);
    }
    const QString terrainPath = out.filePath("export-fixture.dt1");
    QFile terrainFile(terrainPath);
    check(terrainFile.open(QIODevice::WriteOnly), "export terrain fixture opens");
    check(terrainFile.write(dted) == dted.size(), "export terrain fixture is complete");
    terrainFile.close();

    MainWindow window;
    window.show();
    settle(200);
    window.loadTerrain(terrainPath);
    settle(200);

    auto* run = findButton(window, "Run coverage");
    auto* status = window.findChild<QLabel*>("WorkspaceStatus");
    auto* legend = window.findChild<HeatmapLegend*>();
    auto* attribution = window.findChild<QLabel*>("ElevationAttribution");
    auto* viewport = window.findChild<ViewportWidget*>();
    auto* metric = window.findChild<QComboBox*>("CoverageMetricCombo");
    check(run && status && legend && attribution && viewport && metric,
          "export fixture workspace is ready");
    if (!run || !status || !legend || !attribution || !viewport || !metric) return;

    if (QComboBox* mode = findModeCombo(window)) mode->setCurrentIndex(0);
    qApp->processEvents();
    run->click();
    settle(200);
    check(legend->isVisible(), "solved coverage shows its result legend before export");

    // The legend was previously hardcoded to dBm for every metric. Compare its
    // rendered pixels across two selections: identical output would mean the
    // exported map credits the wrong scale to the displayed field.
    metric->setCurrentIndex(0);
    settle(150);
    const QImage desiredLegend = legend->grab().toImage();
    metric->setCurrentIndex(2);
    settle(150);
    const QImage snrLegend = legend->grab().toImage();
    check(!desiredLegend.isNull() && !snrLegend.isNull() && desiredLegend != snrLegend,
          "result legend redraws for the selected coverage metric");
    check(attribution->isVisible() && attribution->text().contains("imported DTED"),
          "map attribution credits the imported elevation source");

    // Drive the application's own save dialog, as a user would.
    auto exportThroughDialog = [&](const char* slot, const QString& path,
                                   bool cancel, bool expectError,
                                   bool reserveAfterAccept = false) {
        bool sawPicker = false, sawError = false, timedOut = false;
        QString errorText;
        QElapsedTimer elapsed;
        elapsed.start();
        QTimer driver;
        QObject::connect(&driver, &QTimer::timeout, [&] {
            if (auto* dialog = window.findChild<QFileDialog*>(); dialog && dialog->isVisible()) {
                sawPicker = true;
                if (cancel || elapsed.elapsed() > 10000) {
                    timedOut = !cancel;
                    dialog->reject();
                } else if (auto* edit = dialog->findChild<QLineEdit*>("fileNameEdit")) {
                    edit->setText(path);
                    QMetaObject::invokeMethod(dialog, "accept");
                    // Reserve the freshly validated target as a directory only
                    // after the picker returns it. Creating it beforehand would
                    // make QFileDialog navigate into it instead of accepting a
                    // filename, so the export would never be attempted.
                    if (reserveAfterAccept && dialog->result() == QDialog::Accepted)
                        check(QDir().mkdir(path), "export failure fixture reserves its target");
                }
            } else if (auto* box = window.findChild<QMessageBox*>(); box && box->isVisible()) {
                sawError = true;
                errorText = box->text();
                box->reject();
            }
        });
        driver.start(25);
        check(QMetaObject::invokeMethod(&window, slot), "export action is reachable");
        driver.stop();
        check(sawPicker && !timedOut && sawError == expectError,
              "export dialog completes with the expected outcome");
        return errorText;
    };

    // Unicode destinations: a narrow-path writer creates a differently named
    // file, or none, and the reopen below would fail.
    const QString csvPath = out.filePath(QString::fromUtf8("覆盖-é.csv"));
    exportThroughDialog("onExportCsv", csvPath, true, false);
    check(!QFile::exists(csvPath), "cancelling the CSV picker writes no file");

    exportThroughDialog("onExportCsv", csvPath, false, false);
    check(status->text().contains("CSV exported"), "successful CSV export is reported");
    QFile csv(csvPath);
    check(csv.open(QIODevice::ReadOnly), "exported CSV reopens from its Unicode path");
    const QString csvText = QString::fromUtf8(csv.readAll());
    csv.close();
    const qint64 csvSize = QFileInfo(csvPath).size();
    const QStringList rows = csvText.split('\n', Qt::SkipEmptyParts);
    check(rows.size() >= 2, "exported CSV has a header and at least one data row");

    const QStringList header = rows.value(0).split(',');
    check(header.size() == 12 && header.value(0) == "x_m" &&
              header.value(2) == "latitude" && header.value(3) == "longitude" &&
              header.value(4) == "terrain_elevation_m" &&
              header.value(11).trimmed() == "in_service",
          "georeferenced CSV header names its coordinate and metric columns");

    // Twelve fields per row also proves the classic numeric locale held: a
    // comma-decimal setting would split every number into extra columns.
    bool rowsWellFormed = rows.size() >= 2;
    for (int i = 1; i < rows.size() && rowsWellFormed; ++i) {
        const QStringList cells = rows.at(i).split(',');
        if (cells.size() != 12) { rowsWellFormed = false; break; }
        bool okLat = false, okLon = false, okElevation = false;
        const double lat = cells.value(2).toDouble(&okLat);
        const double lon = cells.value(3).toDouble(&okLon);
        const double elevation = cells.value(4).toDouble(&okElevation);
        rowsWellFormed = okLat && okLon && okElevation &&
            lat >= 45.0 && lat <= 45.01 && lon >= 10.0 && lon <= 10.01 &&
            elevation >= 119.0 && elevation <= 201.0;
    }
    check(rowsWellFormed,
          "every exported row carries fixture coordinates, elevation and 12 locale-safe columns");

    const QString pngPath = out.filePath(QString::fromUtf8("覆盖-é.png"));
    exportThroughDialog("onExportPng", pngPath, true, false);
    check(!QFile::exists(pngPath), "cancelling the PNG picker writes no file");

    exportThroughDialog("onExportPng", pngPath, false, false);
    check(status->text().contains("PNG exported"), "successful PNG export is reported");
    const QImage exported(pngPath);
    check(!exported.isNull(), "exported PNG reopens and decodes from its Unicode path");

    // The legend and attribution are child widgets of the viewport, so they
    // reach the file only if grab() composites them. Hiding both and grabbing
    // again gives a baseline the exported bytes must differ from.
    legend->hide();
    attribution->hide();
    settle(150);
    const QImage withoutOverlays = viewport->grab().toImage();
    legend->show();
    attribution->show();
    settle(150);
    check(!exported.isNull() && exported.size() == withoutOverlays.size(),
          "exported PNG covers the whole map viewport");
    check(!exported.isNull() && exported != withoutOverlays,
          "exported PNG contains the result legend and elevation attribution");

    // A destination reserved as a directory fails deterministically, without
    // changing permissions or touching an existing user file.
    const QString blocked = out.filePath("blocked.csv");
    const QString error = exportThroughDialog("onExportCsv", blocked, false, true, true);
    check(error.contains("Could not write CSV"), "failed export explains the write failure");
    check(QFileInfo(blocked).isDir(), "failed export leaves the reserved target untouched");
    check(QFileInfo(csvPath).size() == csvSize,
          "a failed export does not disturb an earlier exported file");
    check(QDir().rmdir(blocked), "remove the export blocker for retry");
    exportThroughDialog("onExportCsv", blocked, false, false);
    check(QFileInfo(blocked).isFile() && QFileInfo(blocked).size() > 0,
          "retry after a failed export writes the file");

    // Programmatic checks cannot judge whether a human can read the legend or
    // the attribution. Keep the exported pair for review when a caller asks,
    // without overwriting anything already in the chosen directory.
    const QString captureDir = qEnvironmentVariable("RIDGELINE_QA_CAPTURE_DIR");
    if (!captureDir.isEmpty()) {
        const QString pngCapture = QDir(captureDir).filePath("exported-coverage.png");
        const QString csvCapture = QDir(captureDir).filePath("exported-coverage.csv");
        check(QDir().mkpath(captureDir) && !QFile::exists(pngCapture) &&
                  !QFile::exists(csvCapture),
              "export capture uses new output paths");
        if (!QFile::exists(pngCapture))
            check(QFile::copy(pngPath, pngCapture), "exported map PNG captured for review");
        if (!QFile::exists(csvCapture))
            check(QFile::copy(csvPath, csvCapture), "exported metrics CSV captured for review");
    }

    withModalAnswer(QMessageBox::Discard, [&] { window.close(); });
}


// Unsaved work is only as durable as the process holding it. This drives the
// real cycle: edit, autosave on its own timer, then a fresh window offering the
// copy back.
void testAutosaveAndRecovery() {
    QTemporaryDir stateDir;
    check(stateDir.isValid(), "recovery fixture directory");
    if (!stateDir.isValid()) return;
    const QByteArray previousState = qgetenv("RIDGELINE_STATE_DIR");
    const auto stateGuard = qScopeGuard([&] { qputenv("RIDGELINE_STATE_DIR", previousState); });
    qputenv("RIDGELINE_STATE_DIR", stateDir.path().toUtf8());
    RecoveryStore::clear();

    {
        MainWindow window;
        window.show();
        settle(200);
        check(!RecoveryStore::exists(), "a clean workspace writes nothing to recover");

        // Autosave must not fire on a workspace with nothing to protect.
        auto* timer = window.findChild<QTimer*>("AutosaveTimer");
        check(timer != nullptr, "the workspace autosaves on a timer");
        if (!timer) return;
        timer->setInterval(60);
        settle(300);
        check(!RecoveryStore::exists(), "a clean workspace still writes nothing");

        // Make a real edit, then let the timer run on its own.
        auto* list = window.findChild<QListWidget*>("SourceList");
        auto* remove = findButton(window, "Remove");
        check(list && remove && list->count() > 0, "recovery fixture has a source to remove");
        if (!list || !remove || list->count() == 0) return;
        list->setCurrentRow(0);
        remove->click();
        check(window.isWindowModified(), "the edit marks the workspace unsaved");
        settle(400);
        check(RecoveryStore::exists(), "unsaved work is autosaved without being asked");
        check(RecoveryStore::savedAt().isValid(), "the recovery copy is timestamped");

        // Discarding on close is a decision, so the copy must not outlive it.
        withModalAnswer(QMessageBox::Discard, [&] { window.close(); });
        check(!RecoveryStore::exists(), "closing with Discard removes the recovery copy");
    }

    // Now the crash case: a copy is present and no one cleared it.
    {
        MainWindow seed;
        seed.show();
        settle(200);
        auto* list = seed.findChild<QListWidget*>("SourceList");
        auto* remove = findButton(seed, "Remove");
        if (!list || !remove || list->count() == 0) { check(false, "seed fixture"); return; }
        const int before = list->count();
        list->setCurrentRow(0);
        remove->click();
        auto* timer = seed.findChild<QTimer*>("AutosaveTimer");
        if (timer) timer->setInterval(60);
        settle(400);
        check(RecoveryStore::exists(), "the crash fixture leaves a recovery copy");
        check(before > 0, "the seed workspace had sources to lose");
        // Leave without closing cleanly, as a crash would.
        seed.hide();
    }

    {
        // A fresh window must offer the copy back and restore it on Restore.
        bool sawPrompt = false;
        QTimer answering;
        QObject::connect(&answering, &QTimer::timeout, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box || box->objectName() != "RecoveryPrompt") return;
            sawPrompt = true;
            for (QAbstractButton* button : box->buttons()) {
                if (button->text().contains("Restore")) { button->click(); return; }
            }
            box->reject();
        });
        answering.start(80);
        MainWindow recovered;
        recovered.show();
        settle(1500);
        answering.stop();

        check(sawPrompt, "a recovery copy is offered on the next start");
        check(recovered.isWindowModified(),
              "recovered work stays unsaved, since it has no file behind it");
        withModalAnswer(QMessageBox::Discard, [&] { recovered.close(); });
    }

    // Declining must not leave the copy to be offered again forever.
    {
        MainWindow seed;
        seed.show();
        settle(200);
        auto* remove = findButton(seed, "Remove");
        auto* list = seed.findChild<QListWidget*>("SourceList");
        if (list && remove && list->count() > 0) {
            list->setCurrentRow(0);
            remove->click();
            if (auto* timer = seed.findChild<QTimer*>("AutosaveTimer")) timer->setInterval(60);
            settle(400);
        }
        seed.hide();
    }
    check(RecoveryStore::exists(), "a second crash fixture is waiting");
    {
        QTimer answering;
        QObject::connect(&answering, &QTimer::timeout, [] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box || box->objectName() != "RecoveryPrompt") return;
            for (QAbstractButton* button : box->buttons()) {
                if (button->text().contains("Discard")) { button->click(); return; }
            }
            box->reject();
        });
        answering.start(80);
        MainWindow declined;
        declined.show();
        settle(1200);
        answering.stop();
        check(!RecoveryStore::exists(),
              "declining the offer removes the copy rather than asking every start");
        withModalAnswer(QMessageBox::Discard, [&] { declined.close(); });
    }
}


// Ridgeline never shows a coverage field that disagrees with the settings on
// screen, because every path that changes an input discards the field outright.
// That is a strong guarantee and an easy one to break by dropping a single
// setFieldGrid({}) call, so it is pinned here rather than assumed. MainWindow
// exposes resultsAreStale() purely so this check can be written.
void testDisplayedResultsAlwaysMatchTheirInputs() {
    MainWindow window;
    window.show();
    settle(200);

    auto* run = findButton(window, "Run coverage");
    auto* legend = window.findChild<HeatmapLegend*>();
    if (!run || !legend) { check(false, "stale-invariant fixture controls exist"); return; }
    if (auto* quality = window.findChild<QComboBox*>("CoverageQualityCombo"))
        quality->setCurrentIndex(0);
    qApp->processEvents();
    run->click();
    settle(200);
    check(legend->isVisible(), "a solve produces a displayed result");
    check(!window.findChild<ViewportWidget*>()->isLayerVisible("coverage") ||
              legend->isVisible(),
          "the coverage layer and its legend agree after a solve");

    // Editing a source must leave nothing stale behind. Either the field is
    // gone, or it still matches its inputs - never a mismatch on screen.
    auto* list = window.findChild<QListWidget*>("SourceList");
    check(list && list->count() > 0, "stale-invariant fixture has a source");
    if (list && list->count() > 0) {
        list->setCurrentRow(0);
        qApp->processEvents();
        if (auto* spin = window.findChild<QDoubleSpinBox*>()) {
            spin->setValue(spin->value() + 25.0);
            settle(150);
            check(!legend->isVisible(),
                  "editing a source discards the result rather than leaving it stale");
        }
    }

    // The receiver criteria path must behave the same way.
    run->click();
    settle(200);
    check(legend->isVisible(), "a second solve produces a result again");
    auto* mode = findModeCombo(window);
    if (mode) {
        mode->setCurrentIndex(mode->currentIndex() == 0 ? 1 : 0);
        settle(150);
        check(!legend->isVisible(),
              "changing propagation mode discards the result rather than leaving it stale");
    }

    withModalAnswer(QMessageBox::Discard, [&] { window.close(); });
}


// Where you have been and how you had the panels arranged, remembered between
// runs. Uses its own state directory so it cannot inherit or leak history.
void testRecentHistoryAndLayoutPersist() {
    QTemporaryDir stateDir;
    check(stateDir.isValid(), "history fixture directory");
    if (!stateDir.isValid()) return;
    const QByteArray previousState = qgetenv("RIDGELINE_STATE_DIR");
    const auto stateGuard = qScopeGuard([&] { qputenv("RIDGELINE_STATE_DIR", previousState); });
    qputenv("RIDGELINE_STATE_DIR", stateDir.path().toUtf8());
    WorkspaceSettings::clear();
    RecoveryStore::clear();

    QTemporaryDir docs;
    check(docs.isValid(), "history scenario directory");
    if (!docs.isValid()) return;
    const QString saved = docs.filePath("history.json");

    {
        MainWindow window;
        window.show();
        settle(200);

        auto* scenarios = window.findChild<QMenu*>("RecentScenariosMenu");
        auto* locations = window.findChild<QMenu*>("RecentLocationsMenu");
        check(scenarios && locations, "the File menu offers both histories");
        if (!scenarios || !locations) return;
        // An empty history must not present an empty submenu to click into.
        check(!scenarios->isEnabled(), "an empty scenario history is disabled");
        check(!locations->isEnabled(), "an empty location history is disabled");

        // Saving is somewhere you have been, so it enters the history.
        bool sawPicker = false;
        QTimer driver;
        QObject::connect(&driver, &QTimer::timeout, [&] {
            if (auto* dialog = window.findChild<QFileDialog*>(); dialog && dialog->isVisible()) {
                sawPicker = true;
                if (auto* edit = dialog->findChild<QLineEdit*>("fileNameEdit")) {
                    edit->setText(saved);
                    QMetaObject::invokeMethod(dialog, "accept");
                }
            }
        });
        driver.start(25);
        check(QMetaObject::invokeMethod(&window, "onSaveScenario"), "save is reachable");
        driver.stop();
        check(sawPicker, "the save picker was driven");
        settle(120);

        check(scenarios->isEnabled(), "saving enables the scenario history");
        check(scenarios->actions().size() == 1, "the saved scenario is listed once");
        if (!scenarios->actions().isEmpty()) {
            check(scenarios->actions().first()->text() == "history.json",
                  "the history lists the file by name");
            check(scenarios->actions().first()->toolTip() == saved,
                  "the full path is available, since names collide");
        }

        // Arrange the panels differently, then close so it is remembered.
        if (auto* layersDock = window.findChild<QDockWidget*>("LayersDock"))
            layersDock->hide();
        settle(120);
        withModalAnswer(QMessageBox::Discard, [&] { window.close(); });
    }

    check(!WorkspaceSettings::layoutState().isEmpty(), "closing remembers the arrangement");
    check(WorkspaceSettings::recentScenarios().size() == 1, "history survives the window");

    {
        // A new window restores both.
        MainWindow reopened;
        reopened.show();
        settle(400);
        auto* scenarios = reopened.findChild<QMenu*>("RecentScenariosMenu");
        check(scenarios && scenarios->isEnabled() && scenarios->actions().size() == 1,
              "a new window shows the remembered scenario history");
        auto* layersDock = reopened.findChild<QDockWidget*>("LayersDock");
        check(layersDock && layersDock->isHidden(),
              "a new window restores the arrangement it was left in");

        // Reset must still return to the shipped arrangement, not the stored one.
        if (auto* reset = reopened.findChild<QAction*>("ResetLayoutAction")) {
            reset->trigger();
            settle(150);
            check(layersDock && !layersDock->isHidden(),
                  "resetting the layout overrides what was restored");
        }
        withModalAnswer(QMessageBox::Discard, [&] { reopened.close(); });
    }
    WorkspaceSettings::clear();
}

int main(int argc, char** argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    application.setStyle("Fusion");
    application.setPalette(WorkspaceTheme::palette());
    QCoreApplication::setOrganizationName("RidgelineTests");
    QCoreApplication::setApplicationName("RidgelineMainWindowTests");

    // MainWindow now offers to restore a recovery copy on startup. Point the
    // state directory at a throwaway location so the suite can never find a
    // real one, hang on its modal, or inherit another run's leftovers.
    QTemporaryDir stateDir;
    if (stateDir.isValid()) qputenv("RIDGELINE_STATE_DIR", stateDir.path().toUtf8());

    testCleanScenarioClosesWithoutPrompting();
    testSourceEditPromptsViaUndoStack();
    testSavingUpdatesModifiedIndicator();
    testModeChangePromptsViaDirtyFlag();
    testCloseIsRefusedDuringASolve();
    testCanopyOptionsCommitAndCancel();
    testCanopyRejectedAndCancelledLoadsKeepTerrain();
    testCanopyNetworkCancellationAndMalformedResponse();
    testCanopyWindowBlocksCloseAndAutosave();
    testOverlayCheckboxesMatchWhatTheViewportDraws();
    testTerrainFailureAndCancellationPreserveWorkspace();
    testScenarioOpeningProgressAndRecovery();
    testExportsThroughApplicationDialogs();
    testAutosaveAndRecovery();
    testDisplayedResultsAlwaysMatchTheirInputs();
    testRecentHistoryAndLayoutPersist();

    if (failures == 0)
        std::cout << "MainWindow guard tests passed.\n";
    return failures == 0 ? 0 : 1;
}
