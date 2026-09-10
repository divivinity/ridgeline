#include "ShortcutsDialog.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
struct Entry { const char* keys; const char* what; };

struct Group {
    const char* title;
    std::initializer_list<Entry> entries;
};
}

ShortcutsDialog::ShortcutsDialog(QWidget* parent) : QDialog(parent) {
    setObjectName("ShortcutsDialog");
    setWindowTitle("Keyboard shortcuts");
    setModal(true);

    const Group groups[] = {
        {"Project", {
            {"Ctrl+L", "Choose a location"},
            {"Ctrl+O", "Load terrain from a file"},
            {"Ctrl+Shift+O", "Open a scenario"},
            {"Ctrl+S", "Save the scenario"},
            {"Ctrl+Z / Ctrl+Y", "Undo / redo a source edit"},
        }},
        {"Analysis", {
            {"F5", "Run coverage"},
            {"M", "Measure distance between two points"},
            {"Esc", "Leave measuring or source placement"},
        }},
        {"Map", {
            {"T", "Look straight down"},
            {"R", "Reset the view"},
            {"Arrow keys", "Orbit the camera"},
            {"Mouse wheel", "Zoom toward the cursor"},
            {"Drag", "Orbit; drag a source to move it"},
        }},
    };

    auto* content = new QWidget();
    auto* grid = new QGridLayout(content);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(8);

    int row = 0;
    for (const Group& group : groups) {
        auto* heading = new QLabel(group.title);
        heading->setStyleSheet(
            "color: #5eead4; font-size: 13px; font-weight: 700; padding-top: 8px;");
        grid->addWidget(heading, row++, 0, 1, 2);
        for (const Entry& entry : group.entries) {
            auto* keys = new QLabel(entry.keys);
            keys->setObjectName("ShortcutKeys");
            keys->setStyleSheet(
                "color: #e2e8f0; font-family: 'Consolas'; font-size: 13px;"
                " background: #1b2a3e; border: 1px solid #3b4c63;"
                " border-radius: 5px; padding: 3px 8px;");
            keys->setTextInteractionFlags(Qt::TextSelectableByMouse);
            auto* what = new QLabel(entry.what);
            what->setStyleSheet("color: #cbd5e1; font-size: 13px;");
            what->setWordWrap(true);
            grid->addWidget(keys, row, 0, Qt::AlignLeft | Qt::AlignTop);
            grid->addWidget(what, row, 1);
            ++row;
        }
    }
    grid->setColumnStretch(1, 1);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("ShortcutsScrollArea");
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(12);
    auto* title = new QLabel("Keyboard shortcuts");
    title->setStyleSheet("color: #f8fafc; font-size: 18px; font-weight: 650;");
    layout->addWidget(title);
    layout->addWidget(scroll, 1);
    layout->addWidget(buttons);

    resize(470, 640);
}
