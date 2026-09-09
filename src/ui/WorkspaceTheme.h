#pragma once
#include <QString>
#include <QPalette>
#include <QColor>

// Shared foundations for the map workspace: readable controls and focus states.
namespace WorkspaceTheme {
inline QPalette palette() {
    QPalette p;
    p.setColor(QPalette::Window, QColor("#101a29"));
    p.setColor(QPalette::WindowText, QColor("#e2e8f0"));
    p.setColor(QPalette::Base, QColor("#0d1726"));
    p.setColor(QPalette::AlternateBase, QColor("#152133"));
    p.setColor(QPalette::Text, QColor("#e2e8f0"));
    p.setColor(QPalette::Button, QColor("#1b2a3e"));
    p.setColor(QPalette::ButtonText, QColor("#e2e8f0"));
    p.setColor(QPalette::Highlight, QColor("#20514f"));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, QColor("#233248"));
    p.setColor(QPalette::ToolTipText, QColor("#f1f5f9"));
    p.setColor(QPalette::Link, QColor("#5eead4"));
    p.setColor(QPalette::PlaceholderText, QColor("#8998aa"));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor("#8998aa"));
    return p;
}
inline QString styleSheet() {
    return QStringLiteral(R"(
        QWidget { color: #e2e8f0; font-family: 'Segoe UI'; font-size: 13px; }
        QMainWindow { background: #0b1220; }
        QToolBar { background: #101a29; border: none; border-bottom: 1px solid #293649;
                   spacing: 10px; padding: 8px 12px; }
        QMenuBar { background: #0b1220; color: #a8b7ca; padding: 4px; }
        QMenuBar::item { padding: 5px 10px; }
        QMenuBar::item:selected { background: #243247; color: #f8fafc; border-radius: 4px; }
        QMenu { background: #152133; border: 1px solid #36465c; padding: 6px; }
        QMenu::item { padding: 8px 28px; border-radius: 4px; }
        QMenu::item:selected { background: #20514f; color: white; }
        QMenu::separator { height: 1px; background: #36465c; margin: 5px 10px; }
        QDockWidget { background: #101a29; color: #b8c8da; }
        QDockWidget::title { background: #152133; padding: 9px 12px;
                              border-bottom: 1px solid #293649; }
        QPushButton, QToolButton { background: #1b2a3e; border: 1px solid #3b4c63;
                                  border-radius: 6px; padding: 7px 10px; }
        QPushButton:hover, QToolButton:hover { background: #263950; border-color: #64809c; }
        QPushButton:pressed, QToolButton:checked { background: #184c49; border-color: #4fd1bd; }
        QPushButton:disabled, QToolButton:disabled { color: #8998aa; background: #172131; border-color: #293649; }
        QPushButton:focus, QToolButton:focus, QComboBox:focus, QDoubleSpinBox:focus,
        QLineEdit:focus, QListWidget:focus { border: 1px solid #5eead4; }
        QComboBox, QDoubleSpinBox, QSpinBox, QLineEdit {
            background: #0d1726; color: #e2e8f0; border: 1px solid #3b4c63;
            border-radius: 5px; padding: 6px 8px; selection-background-color: #20514f;
        }
        QComboBox QAbstractItemView { background: #152133; color: #e2e8f0;
                                     selection-background-color: #20514f; }
        QCheckBox { spacing: 7px; }
        QToolTip { background: #233248; color: #f1f5f9; border: 1px solid #52677f; padding: 6px; }
        QStatusBar { background: #0b1220; border-top: 1px solid #293649; }
        QStatusBar::item { border: none; }
        QLabel#LocationSummary { color: #a8b7ca; padding-right: 8px; }
        QPushButton#LocationButton { color: #b4f5e9; }
        QPushButton#RunButton { background: #0f766e; border-color: #4fd1bd;
                               color: white; padding: 8px 18px; font-weight: 600; }
        QPushButton#RunButton:hover { background: #138a80; }
        QPushButton#RunButton:disabled { background: #263247; color: #8998aa; border-color: #3b4c63; }
        QSplitter::handle { background: #293649; }
        QSplitter::handle:hover { background: #4fd1bd; }
        QScrollBar:vertical { background: #101a29; width: 9px; margin: 0; }
        QScrollBar::handle:vertical { background: #52677f; border-radius: 4px; min-height: 26px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    )");
}
}
