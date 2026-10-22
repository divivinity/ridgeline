// End-to-end tests for drawing and editing vegetation in the real main window.
//
// Like test_main_window_guard.cpp these need a display, because the viewport is
// a QOpenGLWidget; they are opt-in through RIDGELINE_GUI_TESTS. They drive the
// map with synthesised mouse and key events and reach private widgets through
// the object tree, so MainWindow exposes no test hooks.
//
// The first test also pins what test_main_window_guard.cpp relies on without
// naming it - which widget an unnamed lookup finds, that there is one undo
// stack. A vegetation change that silently broke those assumptions fails here
// with a message saying why, instead of as a puzzling guard failure.
#include "ui/MainWindow.h"
#include "ui/MapFurniture.h"
#include "ui/VegetationAreaDialog.h"
#include "ui/WorkspaceTheme.h"
#include "data/RecoveryStore.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFocusEvent>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>

#include <cmath>
#include <functional>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    // cerr so progress survives even if a modal ever wedges the run.
    std::cerr << (condition ? "[PASS] " : "[FAIL] ") << what << "\n";
    if (!condition) ++failures;
}

void settle(int ms = 1200) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

QPushButton* findButton(const MainWindow& window, const QString& label) {
    for (QPushButton* button : window.findChildren<QPushButton*>())
        if (button->text().contains(label)) return button;
    return nullptr;
}

template <typename T>
bool hasAncestor(QObject* object) {
    for (QObject* candidate = object; candidate; candidate = candidate->parent())
        if (dynamic_cast<T*>(candidate)) return true;
    return false;
}

void mouse(QWidget* widget, QEvent::Type type, const QPointF& at,
           Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, at, widget->mapToGlobal(at.toPoint()), button, buttons,
                      Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

void pressKey(QWidget* widget, int key) {
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

// Presses and moves in steps, leaving the left button held.
void pressAndMove(QWidget* widget, const QPointF& from, const QPointF& to, int steps = 6) {
    mouse(widget, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= steps; ++i) {
        const QPointF at = from + (to - from) * (static_cast<double>(i) / steps);
        mouse(widget, QEvent::MouseMove, at, Qt::NoButton, Qt::LeftButton);
    }
}

void release(QWidget* widget, const QPointF& at) {
    mouse(widget, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
}

void clickAt(QWidget* widget, const QPointF& at) {
    mouse(widget, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
    mouse(widget, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
}

// A freshly shown main window, looking straight down at the demo terrain so a
// drag near the middle of the viewport is always over ground.
struct Fixture {
    MainWindow window;
    ViewportWidget* viewport = nullptr;
    QUndoStack* history = nullptr;
    QLabel* status = nullptr;
    QListWidget* sources = nullptr;
    QAction* stands = nullptr;
    QAction* clearings = nullptr;
    QAction* properties = nullptr;
    QAction* remove = nullptr;
    NorthIndicator* north = nullptr;

    Fixture() {
        // A recovery copy left by an earlier test would raise a modal prompt.
        RecoveryStore::clear();
        window.resize(1280, 860);
        window.show();
        settle();
        viewport = window.findChild<ViewportWidget*>();
        history = window.findChild<QUndoStack*>();
        status = window.findChild<QLabel*>("WorkspaceStatus");
        sources = window.findChild<QListWidget*>("SourceList");
        stands = window.findChild<QAction*>("DrawVegetationStandsAction");
        clearings = window.findChild<QAction*>("DrawVegetationClearingsAction");
        properties = window.findChild<QAction*>("EditVegetationAreaAction");
        remove = window.findChild<QAction*>("DeleteVegetationAreaAction");
        north = viewport ? viewport->findChild<NorthIndicator*>() : nullptr;
        if (viewport) {
            viewport->topView();
            settle(150);
        }
    }
    ~Fixture() { RecoveryStore::clear(); }

    bool ready() const {
        return viewport && history && status && sources && stands && clearings &&
               properties && remove && north;
    }
    QPointF centre() const { return QPointF(viewport->rect().center()); }
    // Rendered synchronously first: the indicator only learns the camera's
    // azimuth when the map paints, and an unexposed window may not paint.
    double azimuth() {
        if (viewport) viewport->grabFramebuffer();
        return north ? north->cameraAzimuth() : 0.0;
    }

    // Draws with a plain drag; true when exactly one undo step was added.
    bool draw(const QPointF& from, const QPointF& to) {
        const int before = history->index();
        pressAndMove(viewport, from, to);
        release(viewport, to);
        return history->index() == before + 1;
    }
};

void testGuardDependenciesStillHold() {
    Fixture f;
    check(f.ready(), "the vegetation fixture finds its viewport, history, status and actions");
    if (!f.ready()) return;
    MainWindow& window = f.window;

    check(window.findChildren<QUndoStack*>().size() == 1,
          "there is exactly one undo stack, which the guard tests look up by type");
    auto* list = window.findChild<QListWidget*>();
    check(list && list->objectName() == "SourceList",
          "an unnamed list lookup still finds the source list");
    auto* spin = window.findChild<QDoubleSpinBox*>();
    check(spin && hasAncestor<PropertiesPanel>(spin),
          "an unnamed spin-box lookup still finds the source editor");
    QPushButton* removeSource = findButton(window, "Remove");
    check(removeSource && hasAncestor<ScenarioTree>(removeSource),
          "the first button labelled Remove is still the source list's");
    auto* layers = window.findChild<QListWidget*>("LayerList");
    check(layers && layers->count() == 10, "the layers panel still lists ten layers");

    const char* vegetationActionNames[] = {"DrawVegetationStandsAction",
                                           "DrawVegetationClearingsAction",
                                           "EditVegetationAreaAction",
                                           "DeleteVegetationAreaAction",
                                           "DeleteOffTerrainVegetationAction"};
    QList<QAction*> vegetationActions;
    for (const char* name : vegetationActionNames)
        if (QAction* action = window.findChild<QAction*>(name)) vegetationActions << action;
    check(vegetationActions.size() == 5, "every vegetation action exists under its object name");

    // The same lookup the guard uses: the first action, in object-tree order,
    // whose raw text contains the string.
    const QList<QAction*> actions = window.findChildren<QAction*>();
    for (const auto& expected : QList<QPair<QString, QString>>{
             {"Run Coverage", "F5"}, {"Measure Distance", "M"},
             {"Top-down view", "T"}, {"Reset map view", "R"},
             {"Keyboard Shortcuts", "F1"}}) {
        QAction* first = nullptr;
        int matches = 0;
        for (QAction* action : actions) {
            if (!action->text().contains(expected.first)) continue;
            if (!first) first = action;
            ++matches;
        }
        check(first && matches == 1 && !vegetationActions.contains(first) &&
              first->shortcut() == QKeySequence(expected.second),
              qPrintable(QStringLiteral("the guard's lookup for \"%1\" still finds one action, "
                                        "carrying %2").arg(expected.first, expected.second)));
    }

    // Use the tool, then make sure every lookup still lands where it did.
    f.stands->trigger();
    const QPointF c = f.centre();
    f.draw(c - QPointF(60, 45), c + QPointF(60, 45));
    pressKey(f.viewport, Qt::Key_Escape);
    pressKey(f.viewport, Qt::Key_Escape);
    settle(200);
    check(window.findChildren<QUndoStack*>().size() == 1 &&
          window.findChild<QDoubleSpinBox*>() == spin &&
          window.findChild<QListWidget*>() == list &&
          findButton(window, "Remove") == removeSource,
          "after drawing, every unnamed lookup still resolves to the same widget");
    check(window.findChild<VegetationAreaDialog*>() == nullptr,
          "no vegetation dialog lingers under the window");
}

void testDrawToolExclusivity() {
    Fixture f;
    if (!f.ready()) { check(false, "exclusivity fixture is ready"); return; }

    f.stands->trigger();
    check(f.viewport->vegetationDrawMode() &&
          f.viewport->vegetationDrawKind() == VegetationKind::Stand,
          "Draw Stands turns the drawing tool on for stands");
    f.clearings->trigger();
    check(f.viewport->vegetationDrawMode() &&
          f.viewport->vegetationDrawKind() == VegetationKind::Clearing &&
          f.clearings->isChecked() && !f.stands->isChecked(),
          "Draw Clearings switches the tool's kind and unchecks Draw Stands");

    auto* measure = f.window.findChild<QAbstractButton*>("MeasureButton");
    check(measure != nullptr, "found the measure button");
    if (measure) {
        measure->setChecked(true);
        settle(60);
        check(!f.viewport->vegetationDrawMode() && !f.stands->isChecked() &&
              !f.clearings->isChecked(),
              "starting to measure leaves the drawing tool and unchecks it");
        measure->setChecked(false);
        settle(60);
    }

    f.stands->trigger();
    check(QMetaObject::invokeMethod(&f.window, "onAddAntenna"),
          "source placement can be started");
    check(!f.viewport->vegetationDrawMode() && !f.stands->isChecked(),
          "starting source placement leaves the drawing tool");

    int cancellations = 0;
    const auto connection = QObject::connect(f.viewport, &ViewportWidget::placementCancelled,
                                             [&cancellations] { ++cancellations; });
    const int sourcesBefore = f.sources->count();
    f.stands->trigger();
    check(cancellations == 1 && f.viewport->vegetationDrawMode(),
          "drawing during source placement cancels the placement exactly once");
    clickAt(f.viewport, f.centre());
    settle(60);
    check(f.sources->count() == sourcesBefore,
          "a click in the drawing tool never places a source");
    QObject::disconnect(connection);
}

void testDragDrawsOneStandAndInvalidates() {
    Fixture f;
    if (!f.ready()) { check(false, "drawing fixture is ready"); return; }
    QPushButton* run = findButton(f.window, "Run coverage");
    auto* legend = f.window.findChild<HeatmapLegend*>();
    check(run && legend, "found the Run coverage button and the legend");
    if (!run || !legend) return;
    if (auto* quality = f.window.findChild<QComboBox*>("CoverageQualityCombo"))
        quality->setCurrentIndex(0);
    qApp->processEvents();
    run->click();   // blocks in the solve's nested event loop
    settle(300);
    check(legend->isVisible() && f.viewport->coverageFieldDrawn(),
          "a coverage result is on screen before anything is drawn");

    const int indexBefore = f.history->index();
    const int sourcesBefore = f.sources->count();
    const double azimuthBefore = f.azimuth();
    f.stands->trigger();
    const QPointF c = f.centre();
    pressAndMove(f.viewport, c - QPointF(60, 45), c + QPointF(60, 45));
    check(f.viewport->vegetationGestureActive() && f.viewport->vegetationOutlineVertexCount() > 0,
          "a drag previews the stand before anything is committed");
    release(f.viewport, c + QPointF(60, 45));

    // Before any event processing: the commit is synchronous, and the map
    // texture rebuild deliberately is not.
    check(!f.viewport->vegetationWashUpToDate(),
          "the map texture is rebuilt afterwards, never inside the undo command");
    check(f.history->index() == indexBefore + 1 && f.history->undoText() == "Draw stand",
          "one drag is one undoable 'Draw stand'");
    check(f.window.isWindowModified(), "drawing marks the scenario as unsaved");
    check(!legend->isVisible() && !f.viewport->coverageFieldDrawn(),
          "drawing vegetation removes the coverage result it made stale");
    check(f.viewport->vegetationOutlineVertexCount() > 0, "the committed stand is outlined");
    check(f.remove->isEnabled() && f.properties->isEnabled(),
          "the new stand is selected, so it can be edited or deleted");
    check(f.sources->count() == sourcesBefore, "drawing leaves the sources alone");
    check(f.status->text().contains("assumed"),
          "with no imported canopy the status says the stand's height is assumed");

    settle(400);
    check(f.viewport->vegetationWashUpToDate(), "the deferred map texture rebuild catches up");
    check(std::abs(f.azimuth() - azimuthBefore) < 1e-6, "a drawing drag does not orbit the camera");
}

void testClickSelectsAndKeysEditAndDelete() {
    Fixture f;
    if (!f.ready()) { check(false, "selection fixture is ready"); return; }
    f.stands->trigger();
    const QPointF c = f.centre();
    check(f.draw(c - QPointF(60, 45), c + QPointF(60, 45)), "a stand is drawn to work with");

    pressKey(f.viewport, Qt::Key_Escape);
    check(!f.remove->isEnabled() && f.viewport->vegetationDrawMode(),
          "the first Esc deselects and keeps the tool on");
    clickAt(f.viewport, c);
    check(f.remove->isEnabled(), "clicking inside a stand selects it");

    bool sawDialog = false;
    QTimer answer;
    QObject::connect(&answer, &QTimer::timeout, [&sawDialog] {
        if (auto* dialog = qobject_cast<VegetationAreaDialog*>(QApplication::activeModalWidget())) {
            sawDialog = true;
            dialog->reject();
        }
    });
    answer.start(100);
    const int indexBefore = f.history->index();
    pressKey(f.viewport, Qt::Key_Return);
    answer.stop();
    check(sawDialog, "Enter opens the selected area's properties");
    check(f.history->index() == indexBefore, "cancelling the properties dialog changes nothing");

    const int outlinesBefore = f.viewport->vegetationOutlineVertexCount();
    pressKey(f.viewport, Qt::Key_Delete);
    check(f.history->undoText() == "Delete vegetation area" &&
          f.viewport->vegetationOutlineVertexCount() == 0,
          "Delete removes the selected stand and its outline");
    f.history->undo();
    check(outlinesBefore > 0 && f.viewport->vegetationOutlineVertexCount() > 0,
          "undo brings the stand back");

    pressKey(f.viewport, Qt::Key_Escape);
    pressKey(f.viewport, Qt::Key_Escape);
    check(!f.viewport->vegetationDrawMode() && !f.stands->isChecked(),
          "with nothing selected, Esc leaves the tool");
}

void testClearingRemovesStand() {
    Fixture f;
    if (!f.ready()) { check(false, "clearing fixture is ready"); return; }
    f.stands->trigger();
    const QPointF c = f.centre();
    check(f.draw(c - QPointF(60, 45), c + QPointF(60, 45)), "a stand is drawn");
    settle(400);
    // isLayerVisible("vegetation") stands in for terrain().hasVegetation(): the
    // layer only reports itself drawn when there is vegetation to draw.
    check(f.viewport->isLayerVisible("vegetation"),
          "the stand is painted into the map's vegetation layer");

    // Deselected first, so the next drag draws rather than moving the stand.
    pressKey(f.viewport, Qt::Key_Escape);
    f.clearings->trigger();
    check(f.draw(c - QPointF(100, 80), c + QPointF(100, 80)) &&
          f.history->undoText() == "Draw clearing",
          "a larger clearing is drawn over the stand");
    settle(400);
    check(!f.viewport->isLayerVisible("vegetation"),
          "a clearing over the whole stand leaves no vegetation to paint");
    // Deselected, so the count is the committed outlines alone. Deleting the
    // covered stand - the smaller area, so a click inside it picks it - has to
    // take an outline of its own away, which shows it was still drawn.
    pressKey(f.viewport, Qt::Key_Escape);
    const int both = f.viewport->vegetationOutlineVertexCount();
    clickAt(f.viewport, c);
    check(f.remove->isEnabled(), "a click inside the covered stand selects it");
    pressKey(f.viewport, Qt::Key_Delete);
    const int clearingOnly = f.viewport->vegetationOutlineVertexCount();
    check(clearingOnly > 0 && both > clearingOnly,
          "a stand under a clearing keeps its own outline, the only sign it is there");
}

void testCancelledGestureCommitsNothing() {
    Fixture f;
    if (!f.ready()) { check(false, "cancellation fixture is ready"); return; }
    int inspected = 0;
    int placementCancels = 0;
    const auto inspectedConnection = QObject::connect(
        f.viewport, &ViewportWidget::terrainInspected, [&inspected](float, float) { ++inspected; });
    const auto placementConnection = QObject::connect(
        f.viewport, &ViewportWidget::placementCancelled, [&placementCancels] { ++placementCancels; });

    f.stands->trigger();
    const QPointF c = f.centre();
    check(f.draw(c + QPointF(-200, -150), c + QPointF(-120, -90)),
          "an area exists for undo to act on");
    pressKey(f.viewport, Qt::Key_Escape);

    enum class Cancel { Escape, Undo, FocusLoss };
    for (Cancel how : {Cancel::Escape, Cancel::Undo, Cancel::FocusLoss}) {
        const QPointF from = c + QPointF(40, 30);
        const QPointF to = c + QPointF(140, 110);
        const int indexBefore = f.history->index();
        const int countBefore = f.history->count();
        pressAndMove(f.viewport, from, to);
        check(f.viewport->vegetationGestureActive(), "a drag is in progress before it is cancelled");

        switch (how) {
            case Cancel::Escape:
                pressKey(f.viewport, Qt::Key_Escape);
                break;
            case Cancel::Undo:
                f.history->undo();
                break;
            case Cancel::FocusLoss: {
                QFocusEvent focusOut(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
                QApplication::sendEvent(f.viewport, &focusOut);
                break;
            }
        }
        check(!f.viewport->vegetationGestureActive(), "cancelling ends the gesture");

        const double azimuth = f.azimuth();
        for (int i = 1; i <= 4; ++i)
            mouse(f.viewport, QEvent::MouseMove, to + QPointF(20.0 * i, 0.0), Qt::NoButton,
                  Qt::LeftButton);
        release(f.viewport, to + QPointF(80, 0));
        settle(120);
        check(std::abs(f.azimuth() - azimuth) < 1e-6,
              "the rest of a cancelled drag does not orbit the camera");
        const int expectedIndex = how == Cancel::Undo ? indexBefore - 1 : indexBefore;
        check(f.history->index() == expectedIndex && f.history->count() == countBefore,
              "the release after a cancel commits nothing");
        check(f.viewport->vegetationDrawMode(), "cancelling a drag keeps the tool on");
        if (how == Cancel::Undo) f.history->redo();
    }
    // Leaving the tool mid-drag hands the release to the ordinary map, where a
    // release back at the press point is a terrain click. Only the swallow stops
    // this one being taken as one.
    const QPointF pressAt = c + QPointF(-160.0, 120.0);
    pressAndMove(f.viewport, pressAt, pressAt + QPointF(100.0, -80.0));
    check(f.viewport->vegetationGestureActive(), "a drag is in progress before the tool is left");
    f.stands->trigger();
    check(!f.viewport->vegetationDrawMode() && !f.viewport->vegetationGestureActive(),
          "leaving the tool mid-drag ends the drag");
    release(f.viewport, pressAt);
    settle(60);
    check(inspected == 0 && placementCancels == 0,
          "no release of a cancelled drag is taken as a terrain click or a placement cancel");
    f.stands->trigger();
    check(f.viewport->vegetationDrawMode(), "the tool is back on for the orbit check");

    const double before = f.azimuth();
    mouse(f.viewport, QEvent::MouseButtonPress, c, Qt::RightButton, Qt::RightButton);
    for (int i = 1; i <= 6; ++i)
        mouse(f.viewport, QEvent::MouseMove, c + QPointF(15.0 * i, 0.0), Qt::NoButton,
              Qt::RightButton);
    mouse(f.viewport, QEvent::MouseButtonRelease, c + QPointF(90, 0), Qt::RightButton,
          Qt::NoButton);
    settle(120);
    check(std::abs(f.azimuth() - before) > 1.0, "a right drag orbits while the drawing tool is on");

    QObject::disconnect(inspectedConnection);
    QObject::disconnect(placementConnection);
}

// Button chords and releases that go elsewhere must never leave the tool in a
// state where what is committed differs from what was shown, the camera moves
// on a drag that was cancelled, or hover feedback stops.
void testMouseChordsAndLostReleases() {
    Fixture f;
    if (!f.ready()) { check(false, "mouse-chord fixture is ready"); return; }
    f.stands->trigger();
    const QPointF c = f.centre();
    const int indexBefore = f.history->index();

    // Right held, then left pressed: part of a camera move, never a drawing.
    // Were it a drawing, releasing right would freeze its preview while the
    // left release still committed.
    mouse(f.viewport, QEvent::MouseButtonPress, c, Qt::RightButton, Qt::RightButton);
    mouse(f.viewport, QEvent::MouseButtonPress, c, Qt::LeftButton,
          Qt::LeftButton | Qt::RightButton);
    for (int i = 1; i <= 4; ++i)
        mouse(f.viewport, QEvent::MouseMove, c + QPointF(15.0 * i, 10.0 * i), Qt::NoButton,
              Qt::LeftButton | Qt::RightButton);
    check(!f.viewport->vegetationGestureActive(),
          "a left press while the right button is held starts no drawing");
    mouse(f.viewport, QEvent::MouseButtonRelease, c + QPointF(60.0, 40.0), Qt::RightButton,
          Qt::LeftButton);
    for (int i = 1; i <= 8; ++i)
        mouse(f.viewport, QEvent::MouseMove, c + QPointF(60.0 + 25.0 * i, 40.0), Qt::NoButton,
              Qt::LeftButton);
    release(f.viewport, c + QPointF(260.0, 40.0));
    settle(60);
    check(f.history->index() == indexBefore, "releasing that chord commits no area");

    // A cancelled drag, then a middle press with the left button still down: the
    // rest of that drag stays swallowed instead of turning into an orbit.
    const QPointF from = c + QPointF(-120.0, -90.0);
    const QPointF to = from + QPointF(80.0, 60.0);
    pressAndMove(f.viewport, from, to);
    pressKey(f.viewport, Qt::Key_Escape);
    check(!f.viewport->vegetationGestureActive(), "Esc cancels the drag");
    const double azimuthBefore = f.azimuth();
    mouse(f.viewport, QEvent::MouseButtonPress, to, Qt::MiddleButton,
          Qt::LeftButton | Qt::MiddleButton);
    for (int i = 1; i <= 6; ++i)
        mouse(f.viewport, QEvent::MouseMove, to + QPointF(20.0 * i, 0.0), Qt::NoButton,
              Qt::LeftButton | Qt::MiddleButton);
    check(std::abs(f.azimuth() - azimuthBefore) < 1e-6,
          "a middle press during a cancelled drag does not make the rest of it orbit");
    mouse(f.viewport, QEvent::MouseButtonRelease, to + QPointF(120.0, 0.0), Qt::MiddleButton,
          Qt::LeftButton);
    release(f.viewport, to + QPointF(120.0, 0.0));
    settle(60);
    check(f.history->index() == indexBefore, "and nothing is committed");

    // A drag whose release went elsewhere - to a modal dialog, say - must not
    // leave hover feedback dead once a move arrives with no button down.
    check(f.draw(c - QPointF(80.0, 60.0), c + QPointF(80.0, 60.0)), "an area is drawn and selected");
    pressAndMove(f.viewport, c, c + QPointF(30.0, 20.0));
    pressKey(f.viewport, Qt::Key_Escape);
    check(!f.viewport->vegetationGestureActive() && f.remove->isEnabled(),
          "Esc cancels the move and keeps the area selected");
    mouse(f.viewport, QEvent::MouseMove, c + QPointF(250.0, 200.0), Qt::NoButton, Qt::NoButton);
    const Qt::CursorShape outside = f.viewport->cursor().shape();
    mouse(f.viewport, QEvent::MouseMove, c, Qt::NoButton, Qt::NoButton);
    const Qt::CursorShape overBody = f.viewport->cursor().shape();
    check(overBody == Qt::SizeAllCursor && outside != overBody,
          "after a lost release, hovering still switches to the move cursor over the selected area");
}

void testUndoRedoReturnsToSavedState() {
    Fixture f;
    if (!f.ready()) { check(false, "history fixture is ready"); return; }
    check(!f.window.isWindowModified(), "a fresh window starts with nothing unsaved");

    const int sources = f.sources->count();
    f.sources->setCurrentRow(0);
    qApp->processEvents();
    if (QPushButton* removeSource = findButton(f.window, "Remove")) removeSource->click();
    check(f.sources->count() == sources - 1, "a source is removed first");

    f.stands->trigger();
    const QPointF c = f.centre();
    check(f.draw(c - QPointF(60, 45), c + QPointF(60, 45)), "then a stand is drawn");
    pressKey(f.viewport, Qt::Key_Escape);
    check(f.draw(c + QPointF(90, 70), c + QPointF(180, 140)), "then a second stand");

    while (f.history->canUndo()) f.history->undo();
    check(!f.window.isWindowModified() && f.sources->count() == sources &&
          f.viewport->vegetationOutlineVertexCount() == 0,
          "undoing a mixed history back to the start restores both and the saved state");
    while (f.history->canRedo()) f.history->redo();
    check(f.window.isWindowModified() && f.sources->count() == sources - 1 &&
          f.viewport->vegetationOutlineVertexCount() > 0,
          "redoing it all reproduces both and marks the scenario unsaved again");
}

void testRenameKeepsResultHeightDiscardsIt() {
    Fixture f;
    if (!f.ready()) { check(false, "rename fixture is ready"); return; }
    f.stands->trigger();
    const QPointF c = f.centre();
    check(f.draw(c - QPointF(60, 45), c + QPointF(60, 45)), "a stand is drawn and selected");
    QPushButton* run = findButton(f.window, "Run coverage");
    auto* legend = f.window.findChild<HeatmapLegend*>();
    if (!run || !legend) { check(false, "found the Run coverage button and the legend"); return; }
    if (auto* quality = f.window.findChild<QComboBox*>("CoverageQualityCombo"))
        quality->setCurrentIndex(0);
    qApp->processEvents();
    run->click();
    settle(300);
    check(legend->isVisible() && f.viewport->coverageFieldDrawn(),
          "coverage is solved with the stand in place");

    const auto editWith = [&f](const std::function<void(VegetationAreaDialog*)>& change) {
        bool saw = false;
        QTimer answer;
        QObject::connect(&answer, &QTimer::timeout, [&saw, &change] {
            auto* dialog = qobject_cast<VegetationAreaDialog*>(QApplication::activeModalWidget());
            if (!dialog || saw) return;
            saw = true;
            change(dialog);
            dialog->accept();
        });
        answer.start(100);
        pressKey(f.viewport, Qt::Key_Return);
        answer.stop();
        return saw;
    };

    check(editWith([](VegetationAreaDialog* dialog) {
              if (auto* name = dialog->findChild<QLineEdit*>("VegetationAreaName"))
                  name->setText("Renamed wood");
          }),
          "Enter opens the stand's properties for a rename");
    check(f.history->undoText() == "Edit vegetation area", "the rename is one undoable edit");
    check(legend->isVisible() && f.viewport->coverageFieldDrawn(),
          "a rename keeps the coverage result, since nothing the solver reads changed");

    check(editWith([](VegetationAreaDialog* dialog) {
              if (auto* height = dialog->findChild<QDoubleSpinBox*>("VegetationAreaHeight"))
                  height->setValue(height->value() + 3.0);
          }),
          "Enter opens the properties again for a height change");
    check(!legend->isVisible() && !f.viewport->coverageFieldDrawn(),
          "a height change discards the coverage result it made stale");
}

void testAutosaveWritesCommittedAreasOnly() {
    Fixture f;
    if (!f.ready()) { check(false, "autosave fixture is ready"); return; }
    auto* autosave = f.window.findChild<QTimer*>("AutosaveTimer");
    check(autosave != nullptr, "found the autosave timer");
    if (!autosave) return;

    f.stands->trigger();
    const QPointF c = f.centre();
    check(f.draw(c - QPointF(160, 110), c - QPointF(60, 40)), "a stand is drawn");
    pressKey(f.viewport, Qt::Key_Escape);
    f.clearings->trigger();
    check(f.draw(c + QPointF(40, 30), c + QPointF(140, 100)), "a clearing is drawn and selected");

    autosave->setInterval(20);
    autosave->start();
    settle(250);
    const auto readRecovery = []() -> ScenarioDoc {
        try {
            return RecoveryStore::read();
        } catch (const std::exception&) {
            return {};
        }
    };
    const ScenarioDoc committed = readRecovery();
    check(committed.vegetation.size() == 2 &&
          committed.vegetation[0].kind == VegetationKind::Stand &&
          committed.vegetation[0].canopyHeightM > 0.0f &&
          committed.vegetation[1].kind == VegetationKind::Clearing &&
          committed.vegetation[1].canopyHeightM == 0.0f,
          "autosave records the stand and the clearing");
    if (committed.vegetation.size() != 2) return;
    const VegetationArea clearingBefore = committed.vegetation[1];

    // Move the clearing and hold the button down across several autosaves.
    const QPointF grab = c + QPointF(90, 65);
    pressAndMove(f.viewport, grab, grab + QPointF(120, 60));
    settle(250);
    const ScenarioDoc midDrag = readRecovery();
    check(midDrag.vegetation.size() == 2 &&
          midDrag.vegetation[1].minX == clearingBefore.minX &&
          midDrag.vegetation[1].maxZ == clearingBefore.maxZ,
          "an autosave in the middle of a drag writes the committed clearing, not the preview");

    release(f.viewport, grab + QPointF(120, 60));
    settle(250);
    const ScenarioDoc afterRelease = readRecovery();
    check(afterRelease.vegetation.size() == 2 &&
          (afterRelease.vegetation[1].minX != clearingBefore.minX ||
           afterRelease.vegetation[1].minZ != clearingBefore.minZ),
          "once released, the next autosave has the moved clearing");
}

// Pixels of a committed outline's core, which is drawn opaque over everything,
// so its colour does not depend on what lies under it.
int outlinePixels(const QImage& frame) {
    const QImage rgb = frame.convertToFormat(QImage::Format_RGB32);
    int count = 0;
    for (int y = 0; y < rgb.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x) {
            if (std::abs(qRed(line[x]) - 204) <= 24 && std::abs(qGreen(line[x]) - 153) <= 24 &&
                qBlue(line[x]) >= 230)
                ++count;
        }
    }
    return count;
}

// Each layer row carries its own visibility checkbox; the list item itself is
// not checkable.
bool setLayerChecked(QWidget* layers, const QString& id, bool on) {
    auto* box = layers->findChild<QCheckBox*>("LayerVisible" + id);
    if (!box) return false;
    box->setChecked(on);
    return true;
}

// Seen from low over the ground, the imagery drape's polygon offset grows with
// the grazing angle; a fixed depth margin on the outlines loses to it and the
// far edges vanish under the drape. The same view is rendered with no drape and
// with the relief drape (which composites locally, so no network is involved),
// and the outlines must survive the drape. grabFramebuffer renders
// synchronously, so this does not depend on the window being exposed.
void testOutlinesSurviveTheDrapeAtGrazingAngles() {
    Fixture f;
    if (!f.ready()) { check(false, "grazing-angle fixture is ready"); return; }
    auto* layers = f.window.findChild<QListWidget*>("LayerList");
    if (!layers) { check(false, "found the layer list"); return; }

    for (const char* id : {"satellite", "usgs-topo", "hillshade", "vegetation"})
        setLayerChecked(layers, id, false);
    settle(200);
    check(!f.viewport->isLayerVisible("hillshade") && !f.viewport->isLayerVisible("satellite"),
          "every map drape is off for the reference view");

    f.stands->trigger();
    const QPointF c = f.centre();
    check(f.draw(c - QPointF(220, 160), c + QPointF(220, 160)), "a large stand is drawn");
    // Deselected with the tool left on, so only the committed outline is drawn,
    // whatever the vegetation layer says.
    pressKey(f.viewport, Qt::Key_Escape);
    settle(200);
    const QImage topFrame = f.viewport->grabFramebuffer();
    const int topDown = outlinePixels(topFrame);
    check(topDown > 200, "the outline's colour is found in a top-down frame");

    // Right-drag orbits inside the tool: straight up lowers the camera to its
    // 5-degree floor.
    mouse(f.viewport, QEvent::MouseButtonPress, c, Qt::RightButton, Qt::RightButton);
    for (int i = 1; i <= 12; ++i)
        mouse(f.viewport, QEvent::MouseMove, c - QPointF(0.0, 25.0 * i), Qt::NoButton,
              Qt::RightButton);
    mouse(f.viewport, QEvent::MouseButtonRelease, c - QPointF(0.0, 300.0), Qt::RightButton,
          Qt::NoButton);
    settle(150);
    const QImage bareFrame = f.viewport->grabFramebuffer();
    const int bare = outlinePixels(bareFrame);

    setLayerChecked(layers, "hillshade", true);
    settle(400);
    check(f.viewport->isLayerVisible("hillshade"), "the relief drape is drawn for the second view");
    const QImage drapedFrame = f.viewport->grabFramebuffer();
    const int draped = outlinePixels(drapedFrame);
    const QString dumpDir = qEnvironmentVariable("RIDGELINE_DUMP_FRAMES");
    if (!dumpDir.isEmpty()) {
        const QImage a = bareFrame.convertToFormat(QImage::Format_RGB32);
        const QImage b = drapedFrame.convertToFormat(QImage::Format_RGB32);
        const auto isOutline = [](QRgb p) {
            return std::abs(qRed(p) - 204) <= 24 && std::abs(qGreen(p) - 153) <= 24 && qBlue(p) >= 230;
        };
        QImage diff(a.size(), QImage::Format_RGB32);
        int lost = 0, gained = 0;
        for (int y = 0; y < a.height() && y < b.height(); ++y) {
            for (int x = 0; x < a.width() && x < b.width(); ++x) {
                const bool inA = isOutline(a.pixel(x, y));
                const bool inB = isOutline(b.pixel(x, y));
                const QRgb dim = qRgb(qRed(a.pixel(x, y)) / 3, qGreen(a.pixel(x, y)) / 3, qBlue(a.pixel(x, y)) / 3);
                if (inA && !inB) { diff.setPixel(x, y, qRgb(255, 0, 0)); ++lost; }
                else if (!inA && inB) { diff.setPixel(x, y, qRgb(0, 128, 255)); ++gained; }
                else if (inA && inB) diff.setPixel(x, y, qRgb(0, 255, 0));
                else diff.setPixel(x, y, dim);
            }
        }
        topFrame.save(dumpDir + "/top.png");
        bareFrame.save(dumpDir + "/bare.png");
        drapedFrame.save(dumpDir + "/draped.png");
        diff.save(dumpDir + "/diff.png");
        std::cerr << "  dumped frames to " << dumpDir.toStdString() << ": lost " << lost
                  << ", gained " << gained << ", size " << a.width() << "x" << a.height() << "\n";
    }
    std::cerr << "  outline pixels: top-down " << topDown << ", grazing bare " << bare
              << ", grazing under relief " << draped << "\n";
    check(bare > 100, "the outline is visible at a grazing angle with no drape");
    check(draped * 10 >= bare * 9,
          "at a grazing angle the relief drape hides no more than a tenth of the outline");
}

// Deleting areas that lie off the terrain changes nothing the solver reads, so
// the coverage result on screen has to survive it and its undo. The drawing
// tool cannot put an area off the terrain, so the scene arrives the way a
// recovered workspace does.
// The same comparison on large flat ground, where nothing stands between the
// camera and the stand: the only thing that can hide its outline is the drape
// it lies on. At a 5-degree camera the stand is several kilometres away, far
// enough for the drape's slope-scaled polygon offset to beat a fixed margin.
void testOutlinesSurviveTheDrapeOnOpenGround() {
    RecoveryStore::clear();
    ScenarioDoc doc;
    doc.terrain.type = TerrainSource::Type::Flat;
    doc.terrain.widthCells = 800;
    doc.terrain.heightCells = 800;
    doc.terrain.cellSize = 10.0f;
    Antenna source;
    source.position = {400.0f, 20.0f, 400.0f};
    doc.antennas = {source};
    VegetationArea stand;
    stand.minX = 3000.0f;
    stand.minZ = 3000.0f;
    stand.maxX = 5000.0f;
    stand.maxZ = 5000.0f;
    stand.canopyHeightM = 15.0f;
    stand.name = "Stand 1";
    doc.vegetation = {stand};
    if (!RecoveryStore::write(doc)) {
        check(false, "wrote a recovery copy of a large flat scene");
        return;
    }

    QTimer answering;
    QObject::connect(&answering, &QTimer::timeout, [] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box || box->objectName() != "RecoveryPrompt") return;
        for (QAbstractButton* button : box->buttons()) {
            if (button->text().contains("Restore")) {
                button->click();
                return;
            }
        }
    });
    answering.start(80);
    MainWindow window;
    window.resize(1280, 860);
    window.show();
    settle(2500);
    answering.stop();

    auto* viewport = window.findChild<ViewportWidget*>();
    auto* stands = window.findChild<QAction*>("DrawVegetationStandsAction");
    if (!viewport || !stands) {
        check(false, "the open-ground fixture finds its viewport and tool");
        RecoveryStore::clear();
        return;
    }
    for (const char* id : {"satellite", "usgs-topo", "hillshade", "vegetation"})
        setLayerChecked(&window, id, false);
    stands->trigger();   // outlines stay drawn with the vegetation layer off
    settle(300);
    check(!viewport->isLayerVisible("hillshade"), "no drape is drawn for the open-ground reference");

    const QPointF c(viewport->rect().center());
    mouse(viewport, QEvent::MouseButtonPress, c, Qt::RightButton, Qt::RightButton);
    for (int i = 1; i <= 12; ++i)
        mouse(viewport, QEvent::MouseMove, c - QPointF(0.0, 25.0 * i), Qt::NoButton,
              Qt::RightButton);
    mouse(viewport, QEvent::MouseButtonRelease, c - QPointF(0.0, 300.0), Qt::RightButton,
          Qt::NoButton);
    settle(150);
    const QImage bareFrame = viewport->grabFramebuffer();
    const int bare = outlinePixels(bareFrame);

    setLayerChecked(&window, "hillshade", true);
    settle(500);
    check(viewport->isLayerVisible("hillshade"), "the relief drape is drawn over the open ground");
    const QImage drapedFrame = viewport->grabFramebuffer();
    const int draped = outlinePixels(drapedFrame);
    std::cerr << "  open ground outline pixels: bare " << bare << ", under relief " << draped << "\n";
    const QString dumpDir = qEnvironmentVariable("RIDGELINE_DUMP_FRAMES");
    if (!dumpDir.isEmpty()) {
        bareFrame.save(dumpDir + "/open-bare.png");
        drapedFrame.save(dumpDir + "/open-draped.png");
    }
    check(bare > 100, "the outline is visible on open ground at a grazing angle with no drape");
    RecoveryStore::clear();
}

void testDeletingOffTerrainAreasKeepsTheResult() {
    RecoveryStore::clear();
    ScenarioDoc doc;   // the demo scene: 200 x 200 hills at 10 m
    Antenna source;
    source.position = {1000.0f, 60.0f, 1000.0f};
    doc.antennas = {source};
    VegetationArea onTerrain;
    onTerrain.minX = 400.0f;
    onTerrain.minZ = 400.0f;
    onTerrain.maxX = 800.0f;
    onTerrain.maxZ = 700.0f;
    onTerrain.canopyHeightM = 15.0f;
    onTerrain.name = "Stand 1";
    VegetationArea offTerrain = onTerrain;
    offTerrain.minX = 5000.0f;
    offTerrain.maxX = 5400.0f;
    offTerrain.name = "Stand 2";
    doc.vegetation = {onTerrain, offTerrain};
    if (!RecoveryStore::write(doc)) {
        check(false, "wrote a recovery copy holding an area off the terrain");
        return;
    }

    QTimer answering;
    QObject::connect(&answering, &QTimer::timeout, [] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box || box->objectName() != "RecoveryPrompt") return;
        for (QAbstractButton* button : box->buttons()) {
            if (button->text().contains("Restore")) {
                button->click();
                return;
            }
        }
    });
    answering.start(80);
    MainWindow window;
    window.resize(1280, 860);
    window.show();
    settle(1500);
    answering.stop();

    auto* history = window.findChild<QUndoStack*>();
    auto* legend = window.findChild<HeatmapLegend*>();
    auto* viewport = window.findChild<ViewportWidget*>();
    auto* status = window.findChild<QLabel*>("WorkspaceStatus");
    auto* deleteOutside = window.findChild<QAction*>("DeleteOffTerrainVegetationAction");
    QPushButton* run = findButton(window, "Run coverage");
    if (!history || !legend || !viewport || !status || !deleteOutside || !run) {
        check(false, "the off-terrain fixture finds its controls");
        RecoveryStore::clear();
        return;
    }
    check(deleteOutside->isEnabled(), "a restored area off the terrain can be deleted");

    if (auto* quality = window.findChild<QComboBox*>("CoverageQualityCombo"))
        quality->setCurrentIndex(0);
    qApp->processEvents();
    run->click();
    settle(300);
    check(legend->isVisible() && viewport->coverageFieldDrawn(),
          "coverage is solved with an area lying off the terrain");

    const int indexBefore = history->index();
    deleteOutside->trigger();
    check(history->index() == indexBefore + 1 &&
              history->undoText() == "Delete off-terrain vegetation areas",
          "deleting the off-terrain area is one undoable step");
    check(!deleteOutside->isEnabled() && status->text().contains("outside this terrain deleted"),
          "the off-terrain area is gone");
    check(legend->isVisible() && viewport->coverageFieldDrawn(),
          "deleting an area that paints nothing keeps the coverage result");
    history->undo();
    check(legend->isVisible() && viewport->coverageFieldDrawn(),
          "undoing that delete keeps the result too");
    RecoveryStore::clear();
}

void testSubCellAreaIsRefused() {
    Fixture f;
    if (!f.ready()) { check(false, "sub-cell fixture is ready"); return; }
    f.stands->trigger();
    const int indexBefore = f.history->index();
    check(QMetaObject::invokeMethod(&f.window, "onVegetationAreaDrawn",
                                    Q_ARG(float, 100.0f), Q_ARG(float, 100.0f),
                                    Q_ARG(float, 105.0f), Q_ARG(float, 105.0f)),
          "the draw handler is reachable");
    check(f.history->index() == indexBefore && f.status->text().contains("smaller than one"),
          "an area smaller than one terrain cell is refused, with the reason");
}

} // namespace

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
    QCoreApplication::setApplicationName("RidgelineVegetationGuiTests");

    // A throwaway state directory, so no real recovery copy or stored layout
    // can reach the suite.
    QTemporaryDir stateDir;
    if (stateDir.isValid()) qputenv("RIDGELINE_STATE_DIR", stateDir.path().toUtf8());

    testGuardDependenciesStillHold();
    testDrawToolExclusivity();
    testDragDrawsOneStandAndInvalidates();
    testClickSelectsAndKeysEditAndDelete();
    testClearingRemovesStand();
    testCancelledGestureCommitsNothing();
    testMouseChordsAndLostReleases();
    testUndoRedoReturnsToSavedState();
    testRenameKeepsResultHeightDiscardsIt();
    testAutosaveWritesCommittedAreasOnly();
    testOutlinesSurviveTheDrapeAtGrazingAngles();
    testDeletingOffTerrainAreasKeepsTheResult();
    testOutlinesSurviveTheDrapeOnOpenGround();
    testSubCellAreaIsRefused();

    if (failures == 0)
        std::cout << "Vegetation GUI tests passed.\n";
    return failures == 0 ? 0 : 1;
}
