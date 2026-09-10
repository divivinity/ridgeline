#pragma once
#include <QDialog>

// A plain reference for every shortcut the workspace binds.
//
// The camera keys existed long before this dialog and were documented nowhere
// in the application, so the only way to discover them was to read the README.
// Anything bound anywhere belongs in this one list.
class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutsDialog(QWidget* parent = nullptr);
};
