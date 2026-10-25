// SPDX-License-Identifier: AGPL-3.0-only
#include "main_window.h"

#include "scope_panel.h"
#include "scope_widget.h"

#include "proton/bench/engines.h"
#include "proton/core/version.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QString>
#include <QStringList>

#include <chrono>
#include <string>
#include <string_view>

namespace proton::app {

namespace {

constexpr std::chrono::milliseconds kPumpInterval{16}; // one UI tick (ADR 0005)

QString text(std::string_view s)
{
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), examples_(bench::examples()), control_(bench::ngspiceEngines())
{
    setWindowTitle(QStringLiteral("Proton"));
    resize(1280, 800);

    status_ = new QLabel(this);
    status_->setAlignment(Qt::AlignCenter);
    status_->setTextFormat(Qt::PlainText);
    setCentralWidget(status_);

    scope_ = new ScopePanel(this);
    auto* scopeDock = new QDockWidget(tr("Oscilloscope"), this);
    scopeDock->setObjectName(QStringLiteral("scope"));
    scopeDock->setWidget(scope_);
    addDockWidget(Qt::TopDockWidgetArea, scopeDock);
    resizeDocks({scopeDock}, {640}, Qt::Vertical);
    connect(scope_, &ScopePanel::changed, this, &MainWindow::applyScope);
    connect(scope_, &ScopePanel::armed, this, [this] { control_.bench().rearmScope(); });

    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* quit = file->addAction(tr("&Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    QMenu* examples = menuBar()->addMenu(tr("&Examples"));
    auto* group = new QActionGroup(this);
    for (std::size_t i = 0; i < examples_.size(); ++i) {
        QAction* action =
            examples->addAction(text(examples_[i].name), this, [this, i] { loadExample(i); });
        action->setCheckable(true);
        group->addAction(action);
    }

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About Proton"), this, &MainWindow::showAbout);
    // Qt's own dialog names the Qt version and its license, as the LGPL asks (LICENSING.md).
    help->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);

    if (!bench::haveNgspice()) {
        statusBar()->showMessage(tr("This build has no simulator: examples will not run."));
    } else {
        statusBar()->showMessage(tr("Ready"));
    }
    clock_.start();
    // Precise: on Windows a coarse 16 ms timer lands on the 15.6 ms system tick and fires every
    // other tick, halving the frame rate (ADR 0004).
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &MainWindow::pump);
    timer_.start(kPumpInterval);
    showStatus();
}

void MainWindow::loadExample(std::size_t index)
{
    if (index >= examples_.size()) {
        return;
    }
    example_ = index;
    reading_.reset();
    framesAtLoad_ = framesShown();
    scope_->setConfig(examples_[index].bench.scope);
    scope_->clear();
    control_.bench().configure(examples_[index].bench);
    control_.setCircuit(examples_[index].circuit);
    control_.start();
    pump();
}

void MainWindow::applyScope()
{
    bench::Bench::Config config = control_.bench().config();
    config.scope = scope_->config();
    control_.bench().configure(config);
}

long long MainWindow::framesShown() const
{
    return scope_->framesShown();
}

void MainWindow::pump()
{
    const double now = static_cast<double>(clock_.nsecsElapsed()) * 1e-9;
    ++pumps_;
    control_.pump(now);
    if (std::optional<instruments::ScopeFrame> frame = control_.bench().popFrame()) {
        scope_->showFrame(std::move(*frame), control_.bench().scopeStopped());
    }
    if (std::optional<instruments::DmmReading> reading = control_.bench().popReading(0)) {
        reading_ = std::move(reading);
    }
    if (now - rateStart_ >= 1.0) {
        frameRate_ = static_cast<double>(framesShown() - rateFrames_) / (now - rateStart_);
        pumpRate_ = static_cast<double>(pumps_ - ratePumps_) / (now - rateStart_);
        rateFrames_ = framesShown();
        ratePumps_ = pumps_;
        rateStart_ = now;
    }
    showStatus();
}

void MainWindow::showStatus()
{
    if (!example_) {
        status_->setText(tr("The bench is empty. Choose a circuit from the Examples menu."));
        return;
    }
    QStringList lines;
    lines << text(examples_[*example_].name);
    switch (control_.state()) {
    case bench::RunControl::State::Idle:
        lines << tr("Stopped");
        break;
    case bench::RunControl::State::Invalid:
        lines << tr("The circuit cannot be simulated: %1").arg(text(control_.error()));
        break;
    case bench::RunControl::State::Failed:
        lines << tr("The simulation stopped: %1").arg(text(control_.error()));
        break;
    case bench::RunControl::State::Running:
        lines << tr("Running: t = %1 s, %2 x real time")
                     .arg(control_.lastTime(), 0, 'f', 2)
                     .arg(control_.speed(), 0, 'f', 2);
        break;
    }
    for (const bench::ProbeError& error : control_.probeErrors()) {
        lines << text(error.message);
    }
    const bench::Bench::Config& config = control_.bench().config();
    if (!config.multimeters.empty()) {
        const char* mode =
            config.multimeters[0].config.mode == instruments::Multimeter::Mode::AcVoltage ? "AC"
                                                                                           : "DC";
        lines << tr("DMM1 on %1, %2 V: %3")
                     .arg(text(config.multimeters[0].net), QString::fromLatin1(mode),
                          reading_ ? text(reading_->display.text) + QLatin1Char(' ') +
                                         text(reading_->display.unit)
                                   : tr("no reading yet"));
    }
    lines << tr("Scope: CH1 on %1, CH2 on %2; %3 frames/s shown, %4 ms per paint")
                 .arg(text(config.scopeNets[0]), text(config.scopeNets[1]))
                 .arg(frameRate_, 0, 'f', 0)
                 .arg(scope_->screen().meanPaintMs(), 0, 'f', 2);
    lines << tr("Restarts: %1").arg(control_.restarts());
    status_->setText(lines.join(QLatin1Char('\n')));
}

void MainWindow::showAbout()
{
    const std::string_view version = core::versionString();
    QMessageBox::about(this, tr("About Proton"),
        tr("<p><b>Proton %1</b></p>"
           "<p>A virtual electronics bench.</p>"
           "<p>Licensed under the GNU Affero General Public License v3.0 only, "
           "or a commercial license.</p>")
            .arg(text(version)));
}

} // namespace proton::app
