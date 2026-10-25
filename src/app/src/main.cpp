// SPDX-License-Identifier: AGPL-3.0-only
// proton: the Qt Widgets app.
//
//     proton [--smoke-test | --smoke-examples]
//
// --smoke-test opens the main window, closes it once the event loop runs and exits 0: the CI
// check that the app, its Qt DLLs and plugins start.
//
// --smoke-examples opens the main window and runs every example in menu order, switching to the
// next once the multimeter has read, the scope has shown a frame and kExampleDwell has passed, as
// a student choosing them from the menu would. It prints the frame rate shown and the scope's
// paint times (ADR 0004), and exits 0 after the last example, 1 if one fails or takes longer than
// kExampleTimeout: the CI check that the examples run in the app and that switching restarts
// cleanly.

#include "main_window.h"
#include "scope_panel.h"
#include "scope_widget.h"

#include "proton/core/version.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QIcon>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string_view>

namespace {

constexpr std::chrono::seconds kExampleTimeout{30}; // generous: CI runners are slow
constexpr std::chrono::milliseconds kExampleDwell{1500}; // over a second for the frame rate

// Runs the examples of `window` one after the other; see --smoke-examples.
void smokeExamples(proton::app::MainWindow& window)
{
    struct Progress
    {
        std::size_t example = 0;
        QElapsedTimer clock; // since the example was loaded
    };
    auto progress = std::make_shared<Progress>();
    auto* timer = new QTimer(&window);
    progress->clock.start();
    window.loadExample(0);
    QObject::connect(timer, &QTimer::timeout, &window, [&window, timer, progress] {
        using State = proton::bench::RunControl::State;
        const bool late =
            progress->clock.elapsed() > std::chrono::milliseconds(kExampleTimeout).count();
        if (window.control().state() != State::Running || late) {
            std::fprintf(stderr, "proton: example %zu did not run: %s\n", progress->example,
                         late ? "no reading in time" : window.control().error().c_str());
            timer->stop();
            QApplication::exit(1);
            return;
        }
        if (!window.hasReading() ||
            progress->clock.elapsed() < std::chrono::milliseconds(kExampleDwell).count()) {
            return;
        }
        const proton::app::ScopeWidget& screen = window.scope().screen();
        std::printf("proton: example %zu: %.0f pumps/s, %.0f frames/s shown, speed %.2f; paint mean %.2f ms, max %.2f ms "
                    "over %lld paints\n",
                    progress->example, window.pumpRate(), window.frameRate(),
                    window.control().speed(), screen.meanPaintMs(),
                    screen.maxPaintMs(), screen.paints());
        std::fflush(stdout);
        if (++progress->example == window.exampleCount()) {
            timer->stop();
            window.close();
            return;
        }
        progress->clock.restart();
        window.loadExample(progress->example);
    });
    timer->start(16);
}

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
    const QCommandLineOption smokeExamplesOption(QStringLiteral("smoke-examples"),
        QStringLiteral("Run every example until its multimeter reads, then exit."));
    parser.addOption(smokeTest);
    parser.addOption(smokeExamplesOption);
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
        smokeExamples(window);
    }
    return QApplication::exec();
}
