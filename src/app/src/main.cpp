// SPDX-License-Identifier: AGPL-3.0-only
// proton: the Qt Widgets app.
//
//     proton [--smoke-test | --smoke-examples | --smoke-panel | --smoke-latency |
//             --smoke-editor | --smoke-undo | --smoke-menus | --smoke-session |
//             --smoke-diagnostics | --smoke-selection | --smoke-files | --screenshot <file.png>]
//
// Without an option the window opens maximised, on the start page.
//
// --smoke-test opens the main window, closes it once the event loop runs and exits 0: the CI
// check that the app, its Qt DLLs and plugins start.
//
// The other --smoke-* options are CI checks (smoke.h): every example runs in turn, every
// front-panel control answers wheel, keys, drag and clicks, a generator knob reaches the scope in
// time, circuits and instruments placed from the parts panel and wired by hand measure what they
// should, the sheet's edits undo to the empty sheet and redo, every menu and toolbar action does
// what it says, the RC low-pass's corner is found with the panels, each drawing mistake is marked
// where it is, box selection and the right-click menu work, and projects save, open and reopen.
// Each exits 0 when it passes and 1 otherwise. With any option, Proton's settings file is a test
// one (QStandardPaths::setTestModeEnabled), so the checks leave the student's recent files alone.
//
// --screenshot saves the start page (<file>-start.png), then loads the first example, presses Run,
// lets it run for kScreenshotDelay, saves the window as a PNG, each instrument's panel beside it
// (<file>-scope1.png, ...) and each menu opened (<file>-menu-edit.png, ...), and exits: for
// checking the look and for documentation.

#include "main_window.h"
#include "smoke.h"

#include "proton/core/version.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QStandardPaths>
#include <QString>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <string_view>

namespace {

constexpr std::chrono::milliseconds kScreenshotDelay{1500};

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Proton"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/proton.png")));
    const std::string_view version = proton::core::versionString();
    QApplication::setApplicationVersion(
        QString::fromUtf8(version.data(), static_cast<qsizetype>(version.size())));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption smokeTest(QStringLiteral("smoke-test"),
        QStringLiteral("Open the main window, close it and exit."));
    const QCommandLineOption screenshot(QStringLiteral("screenshot"),
        QStringLiteral("Run the first example, save the window as <file> and exit."),
        QStringLiteral("file"));
    const QCommandLineOption smokeExamplesOption(QStringLiteral("smoke-examples"),
        QStringLiteral("Run every example until its multimeter reads, then exit."));
    parser.addOption(smokeTest);
    parser.addOption(smokeExamplesOption);
    const QCommandLineOption smokePanelOption(QStringLiteral("smoke-panel"),
        QStringLiteral("Operate every control of the scope panel, check each, then exit."));
    parser.addOption(smokePanelOption);
    const QCommandLineOption smokeLatencyOption(QStringLiteral("smoke-latency"),
        QStringLiteral("Turn the generator's frequency and time the first new frame, then exit."));
    parser.addOption(smokeLatencyOption);
    const QCommandLineOption smokeEditorOption(QStringLiteral("smoke-editor"),
        QStringLiteral("Draw circuits and instruments by hand, check every reading, then exit."));
    parser.addOption(smokeEditorOption);
    const QCommandLineOption smokeUndoOption(QStringLiteral("smoke-undo"),
        QStringLiteral("Edit the sheet, undo every edit and redo them, check each, then exit."));
    parser.addOption(smokeUndoOption);
    const QCommandLineOption smokeMenusOption(QStringLiteral("smoke-menus"),
        QStringLiteral("Trigger every menu and toolbar action, check each, then exit."));
    parser.addOption(smokeMenusOption);
    const QCommandLineOption smokeSessionOption(QStringLiteral("smoke-session"),
        QStringLiteral("Find the RC low-pass's corner with the panels, check it, then exit."));
    parser.addOption(smokeSessionOption);
    const QCommandLineOption smokeDiagnosticsOption(QStringLiteral("smoke-diagnostics"),
        QStringLiteral("Make each drawing mistake, check where it is marked, then exit."));
    parser.addOption(smokeDiagnosticsOption);
    const QCommandLineOption smokeSelectionOption(QStringLiteral("smoke-selection"),
        QStringLiteral("Select with boxes and right-click menus, check each, then exit."));
    parser.addOption(smokeSelectionOption);
    const QCommandLineOption smokeFilesOption(QStringLiteral("smoke-files"),
        QStringLiteral("Save, open and reopen projects, check each, then exit."));
    parser.addOption(smokeFilesOption);
    parser.addOption(screenshot);
    parser.process(app);

    const bool interactive = std::ranges::none_of(
        std::initializer_list<const QCommandLineOption*>{
            &smokeTest, &smokeExamplesOption, &smokePanelOption, &smokeLatencyOption,
            &smokeEditorOption, &smokeUndoOption, &smokeMenusOption, &smokeSessionOption,
            &smokeDiagnosticsOption, &smokeSelectionOption, &smokeFilesOption, &screenshot},
        [&parser](const QCommandLineOption* option) { return parser.isSet(*option); });
    // The checks keep Proton's settings, the recent files among them, apart from the student's.
    QStandardPaths::setTestModeEnabled(!interactive);

    proton::app::MainWindow window;
    // A student gets the whole screen, room for the sheet and the panels side by side, and the
    // start page; the checks and screenshots keep the window's fixed size and the sheet, so they
    // do not depend on the screen.
    if (interactive) {
        window.showStartPage();
        window.showMaximized();
    } else {
        window.show();
    }
    if (parser.isSet(smokeTest)) {
        QTimer::singleShot(0, &window, [&window] {
            if (!window.isVisible()) {
                QApplication::exit(1);
                return;
            }
            window.close();
        });
    } else if (parser.isSet(smokeExamplesOption)) {
        proton::app::smokeExamples(window);
    } else if (parser.isSet(smokePanelOption)) {
        proton::app::smokePanel(window);
    } else if (parser.isSet(smokeLatencyOption)) {
        proton::app::smokeLatency(window);
    } else if (parser.isSet(smokeEditorOption)) {
        proton::app::smokeEditor(window);
    } else if (parser.isSet(smokeUndoOption)) {
        proton::app::smokeUndo(window);
    } else if (parser.isSet(smokeMenusOption)) {
        proton::app::smokeMenus(window);
    } else if (parser.isSet(smokeSessionOption)) {
        proton::app::smokeSession(window);
    } else if (parser.isSet(smokeDiagnosticsOption)) {
        proton::app::smokeDiagnostics(window);
    } else if (parser.isSet(smokeSelectionOption)) {
        proton::app::smokeSelection(window);
    } else if (parser.isSet(smokeFilesOption)) {
        proton::app::smokeFiles(window);
    } else if (parser.isSet(screenshot)) {
        const QFileInfo start(parser.value(screenshot));
        window.showStartPage();
        bool startSaved = window.grab().save(start.path() + QLatin1Char('/') +
                                             start.completeBaseName() + QStringLiteral("-start.") +
                                             start.suffix());
        window.loadExample(0);
        window.setRunning(true);
        const QString file = parser.value(screenshot);
        QTimer::singleShot(kScreenshotDelay, &window, [&window, file, startSaved] {
            bool saved = window.grab().save(file) && startSaved;
            const QFileInfo info(file);
            for (const QString name :
                 {QStringLiteral("gen1"), QStringLiteral("scope1"), QStringLiteral("dmm1")}) {
                if (QWidget* panel = window.openPanel(name)) {
                    const QString panelFile = info.path() + QLatin1Char('/') +
                                              info.completeBaseName() + QLatin1Char('-') + name +
                                              QLatin1Char('.') + info.suffix();
                    saved = panel->window()->grab().save(panelFile) && saved;
                }
            }
            // Each menu of the menu bar, opened: <file>-menu-file.png, ...
            for (QAction* entry : window.menuBar()->actions()) {
                QMenu* menu = entry->menu();
                if (menu == nullptr) {
                    continue;
                }
                Q_EMIT menu->aboutToShow(); // menus filled as they open
                menu->popup(window.menuBar()->mapToGlobal(
                    window.menuBar()->actionGeometry(entry).bottomLeft()));
                const QString name = entry->text().remove(QLatin1Char('&')).toLower();
                const QString menuFile = info.path() + QLatin1Char('/') + info.completeBaseName() +
                                         QStringLiteral("-menu-") + name + QLatin1Char('.') +
                                         info.suffix();
                saved = menu->grab().save(menuFile) && saved;
                menu->hide();
            }
            QApplication::exit(saved ? 0 : 1);
        });
    }
    return QApplication::exec();
}
