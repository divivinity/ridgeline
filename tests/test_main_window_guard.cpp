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

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QProgressDialog>
#include <QEventLoop>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

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

    bool closedOnCancel = true;
    withModalAnswer(QMessageBox::Cancel, [&] { closedOnCancel = window.close(); });
    check(sawPrompt, "source edit prompts before closing");
    check(!closedOnCancel, "Cancel keeps the window open");

    bool closedOnDiscard = false;
    withModalAnswer(QMessageBox::Discard, [&] { closedOnDiscard = window.close(); });
    check(closedOnDiscard, "Discard closes the window");
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

    // With the solve finished the window closes normally again.
    withModalAnswer(QMessageBox::Discard, [&] { window.close(); });
}

// Two independent places decide which overlays start visible: the checkbox
// arguments in OverlayToolbar, and ViewportWidget's constructor, which turns the
// analysis layers off over their headers' `visible = true` defaults. Nothing
// links them, so they can drift into a state where a layer draws while its box
// reads unchecked — the user would then have to check and uncheck it to hide it.
// They agree today; this pins that, since only a test can keep them in step.
void testOverlayCheckboxesMatchWhatTheViewportDraws() {
    MainWindow window;
    window.show();
    settle();

    auto* viewport = window.findChild<ViewportWidget*>();
    if (!viewport) { check(false, "found the viewport"); return; }

    const struct { const char* label; ViewportWidget::Overlay overlay; } layers[] = {
        {"Satellite", ViewportWidget::Basemap},
        {"Coverage",  ViewportWidget::Heatmap},
        {"LOS",       ViewportWidget::LOS},
        {"Pattern",   ViewportWidget::Lobe},
        {"Jamming",   ViewportWidget::Jammer},
        {"Fresnel",   ViewportWidget::Fresnel},
    };

    int matched = 0;
    for (const auto& layer : layers) {
        QCheckBox* box = nullptr;
        for (QCheckBox* candidate : window.findChildren<QCheckBox*>())
            if (candidate->text() == QLatin1String(layer.label)) box = candidate;
        if (!box) {
            check(false, (std::string("found the ") + layer.label + " checkbox").c_str());
            continue;
        }
        ++matched;
        const bool agrees = box->isChecked() == viewport->isOverlayVisible(layer.overlay);
        check(agrees, (std::string(layer.label) +
                       " overlay matches its checkbox at startup").c_str());
    }
    check(matched == 6, "found all six overlay checkboxes");

    // And the agreement must survive a toggle, not just hold at construction.
    for (QCheckBox* candidate : window.findChildren<QCheckBox*>()) {
        if (candidate->text() != QLatin1String("LOS")) continue;
        candidate->setChecked(!candidate->isChecked());
        qApp->processEvents();
        check(candidate->isChecked() == viewport->isOverlayVisible(ViewportWidget::LOS),
              "LOS overlay follows its checkbox when toggled");
    }

    withModalAnswer(QMessageBox::Discard, [&] { window.close(); });
}

}  // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("RidgelineTests");
    QCoreApplication::setApplicationName("RidgelineMainWindowTests");

    testCleanScenarioClosesWithoutPrompting();
    testSourceEditPromptsViaUndoStack();
    testModeChangePromptsViaDirtyFlag();
    testCloseIsRefusedDuringASolve();
    testOverlayCheckboxesMatchWhatTheViewportDraws();

    if (failures == 0)
        std::cout << "MainWindow guard tests passed.\n";
    return failures == 0 ? 0 : 1;
}
