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
#include <QComboBox>
#include <QEventLoop>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

#include <iostream>

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

}  // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("RidgelineTests");
    QCoreApplication::setApplicationName("RidgelineMainWindowTests");

    testCleanScenarioClosesWithoutPrompting();
    testSourceEditPromptsViaUndoStack();
    testModeChangePromptsViaDirtyFlag();

    if (failures == 0)
        std::cout << "MainWindow guard tests passed.\n";
    return failures == 0 ? 0 : 1;
}
