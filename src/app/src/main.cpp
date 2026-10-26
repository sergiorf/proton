// SPDX-License-Identifier: AGPL-3.0-only
// proton: the Qt Widgets app.
//
//     proton [--smoke-test | --smoke-examples | --smoke-panel | --smoke-latency |
//             --smoke-editor | --smoke-undo | --screenshot <file.png>]
//
// --smoke-test opens the main window, closes it once the event loop runs and exits 0: the CI
// check that the app, its Qt DLLs and plugins start.
//
// --smoke-examples, --smoke-panel, --smoke-latency, --smoke-editor and --smoke-undo are CI checks
// (smoke.h): every example runs in turn, every front-panel control answers wheel, keys, drag and
// clicks, a generator knob reaches the scope in time, a divider drawn on the sheet reads as the
// built-in one, and the sheet's edits undo to the empty sheet and redo. Each exits 0 when it passes
// and 1 otherwise.
//
// --screenshot loads the first example, lets it run for kScreenshotDelay, saves the window as a
// PNG and exits: for checking a panel's look and for documentation.

#include "main_window.h"
#include "smoke.h"

#include "proton/core/version.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QTimer>

#include <chrono>
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
        QStringLiteral("Draw the voltage divider on the sheet, check its reading, then exit."));
    parser.addOption(smokeEditorOption);
    const QCommandLineOption smokeUndoOption(QStringLiteral("smoke-undo"),
        QStringLiteral("Edit the sheet, undo every edit and redo them, check each, then exit."));
    parser.addOption(smokeUndoOption);
    parser.addOption(screenshot);
    parser.process(app);

    proton::app::MainWindow window;
    window.show();
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
    } else if (parser.isSet(screenshot)) {
        window.loadExample(0);
        const QString file = parser.value(screenshot);
        QTimer::singleShot(kScreenshotDelay, &window, [&window, file] {
            const bool saved = window.grab().save(file);
            QApplication::exit(saved ? 0 : 1);
        });
    }
    return QApplication::exec();
}
