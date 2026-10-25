// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QMainWindow>

namespace proton::app {

// The app's main window. For now an empty bench with a menu: the instruments arrive in M2.2 to
// M2.5 as dock panels, each bound to an instrument of the bench (ARCHITECTURE.md section 11).
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void showAbout();
};

} // namespace proton::app
