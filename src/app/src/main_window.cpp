// SPDX-License-Identifier: AGPL-3.0-only
#include "main_window.h"

#include "proton/core/version.h"

#include <QAction>
#include <QApplication>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QString>

#include <string_view>

namespace proton::app {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Proton"));
    resize(1280, 800);

    auto* placeholder = new QLabel(tr("The bench is empty."), this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);

    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* quit = file->addAction(tr("&Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About Proton"), this, &MainWindow::showAbout);
    // Qt's own dialog names the Qt version and its license, as the LGPL asks (LICENSING.md).
    help->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);

    statusBar()->showMessage(tr("Ready"));
}

void MainWindow::showAbout()
{
    const std::string_view version = core::versionString();
    QMessageBox::about(this, tr("About Proton"),
        tr("<p><b>Proton %1</b></p>"
           "<p>A virtual electronics bench.</p>"
           "<p>Licensed under the GNU Affero General Public License v3.0 only, "
           "or a commercial license.</p>")
            .arg(QString::fromUtf8(version.data(), static_cast<qsizetype>(version.size()))));
}

} // namespace proton::app
