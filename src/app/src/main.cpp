// SPDX-License-Identifier: AGPL-3.0-only
// proton: the Qt Widgets app.
//
//     proton [--smoke-test]
//
// --smoke-test opens the main window, closes it once the event loop runs and exits 0: the CI
// check that the app, its Qt DLLs and plugins start.

#include "main_window.h"

#include "proton/core/version.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QString>
#include <QTimer>

#include <string_view>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Proton"));
    const std::string_view version = proton::core::versionString();
    QApplication::setApplicationVersion(
        QString::fromUtf8(version.data(), static_cast<qsizetype>(version.size())));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption smokeTest(QStringLiteral("smoke-test"),
        QStringLiteral("Open the main window, close it and exit."));
    parser.addOption(smokeTest);
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
    }
    return QApplication::exec();
}
