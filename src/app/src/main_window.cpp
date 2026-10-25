// SPDX-License-Identifier: AGPL-3.0-only
#include "main_window.h"

#include "generator_panel.h"
#include "multimeter_panel.h"
#include "scope_panel.h"
#include "schematic/schematic_view.h"
#include "scope_widget.h"

#include "proton/bench/engines.h"
#include "proton/core/version.h"
#include "proton/schematic/scene.h"
#include "proton/schematic/validate.h"

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
#include <QToolBar>

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
    resize(1440, 900);
    // Docks on the right run the full height, so the multimeter stands beside the scope.
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    // The schematic in the middle, the scope along the bottom, the meter and generator on the
    // right: the circuit in front of the student with the instruments around it.
    sheet_ = new sheet::SchematicView(this);
    setCentralWidget(sheet_);
    connect(sheet_, &sheet::SchematicView::edited, this, &MainWindow::applyScene);

    status_ = new QLabel(this);
    status_->setTextFormat(Qt::PlainText);
    statusBar()->addWidget(status_, 1);

    scope_ = new ScopePanel(this);
    auto* scopeDock = new QDockWidget(tr("Oscilloscope"), this);
    scopeDock->setObjectName(QStringLiteral("scope"));
    scopeDock->setWidget(scope_);
    addDockWidget(Qt::BottomDockWidgetArea, scopeDock);
    resizeDocks({scopeDock}, {400}, Qt::Vertical);
    connect(scope_, &ScopePanel::changed, this, &MainWindow::applyScope);
    connect(scope_, &ScopePanel::armed, this, [this] { control_.bench().rearmScope(0); });

    meter_ = new MultimeterPanel(this);
    auto* meterDock = new QDockWidget(tr("Multimeter"), this);
    meterDock->setObjectName(QStringLiteral("multimeter"));
    meterDock->setWidget(meter_);
    addDockWidget(Qt::RightDockWidgetArea, meterDock);
    resizeDocks({meterDock}, {380}, Qt::Horizontal);
    connect(meter_, &MultimeterPanel::changed, this, &MainWindow::applyMultimeter);

    generator_ = new GeneratorPanel(this);
    auto* generatorDock = new QDockWidget(tr("Function generator"), this);
    generatorDock->setObjectName(QStringLiteral("generator"));
    generatorDock->setWidget(generator_);
    splitDockWidget(meterDock, generatorDock, Qt::Vertical);
    connect(generator_, &GeneratorPanel::changed, this, &MainWindow::applyGenerator);

    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* newSheet = file->addAction(tr("&New sheet"), this, [this] {
        sheet_->clearSheet();
        sheet_->setTool(sheet::SchematicView::Tool::Select);
    });
    newSheet->setShortcut(QKeySequence::New);
    file->addSeparator();
    QAction* quit = file->addAction(tr("&Quit"), this, &QWidget::close);

    buildToolBar();
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
    // Each panel shows the first instrument of its kind; one panel per instance comes with the
    // editor (M4.4).
    const bench::Bench::Config& bench = examples_[index].bench;
    if (!bench.scopes.empty()) {
        scope_->setConfig(bench.scopes[0].config);
    }
    scope_->clear();
    if (!bench.multimeters.empty()) {
        const bench::Bench::MultimeterSettings& meter = bench.multimeters[0];
        meter_->setConfig(meter.config, text(meter.plus), text(meter.com));
    }
    if (!bench.generators.empty()) {
        const bench::Bench::GeneratorSettings& generator = bench.generators[0];
        generator_->setConfig(generator.config,
                              generator.com.empty() ? QString() : text(generator.plus));
    }
    control_.bench().configure(bench);
    sheet_->setScene(examples_[index].scene);
    applyScene();
    pump();
}

void MainWindow::applyScope()
{
    bench::Bench::Config config = control_.bench().config();
    if (config.scopes.empty()) {
        return;
    }
    config.scopes[0].config = scope_->config();
    control_.bench().configure(config);
}

void MainWindow::applyMultimeter()
{
    bench::Bench::Config config = control_.bench().config();
    if (config.multimeters.empty()) {
        return;
    }
    config.multimeters[0].config = meter_->config();
    control_.bench().configure(config);
    meter_->clear(); // a new gate: dashes until its reading
}

// A generator knob: the next pump rebuilds the netlist and restarts the run (decision D3).
void MainWindow::applyGenerator()
{
    bench::Bench::Config config = control_.bench().config();
    if (config.generators.empty()) {
        return;
    }
    config.generators[0].config = generator_->config();
    control_.bench().configure(config);
}

void MainWindow::buildToolBar()
{
    using Tool = sheet::SchematicView::Tool;
    QToolBar* tools = addToolBar(tr("Schematic"));
    tools->setObjectName(QStringLiteral("schematic-tools"));
    auto* group = new QActionGroup(this);
    const auto tool = [&](const QString& name, const QString& tip, const QKeySequence& key,
                          auto choose) {
        QAction* action = tools->addAction(name, this, choose);
        action->setToolTip(tip);
        action->setShortcut(key);
        action->setCheckable(true);
        group->addAction(action);
        return action;
    };
    tool(tr("Select"), tr("Select, move (drag), turn (R), delete (Del); double-click to edit"),
         QKeySequence(Qt::Key_S), [this] { sheet_->setTool(Tool::Select); })
        ->setChecked(true);
    tool(tr("Wire"), tr("Click where the wire starts, then where it bends or ends"),
         QKeySequence(Qt::Key_W), [this] { sheet_->setTool(Tool::Wire); });
    tool(tr("Label"), tr("Click a net to name it"), QKeySequence(Qt::Key_L),
         [this] { sheet_->setTool(Tool::Label); });
    tools->addSeparator();
    using schematic::ComponentKind;
    const auto part = [&](const QString& name, ComponentKind kind) {
        tool(name, tr("Place a %1: click to place, R to turn").arg(name.toLower()), QKeySequence(),
             [this, kind] { sheet_->placeKind(kind); });
    };
    part(tr("Resistor"), ComponentKind::Resistor);
    part(tr("Capacitor"), ComponentKind::Capacitor);
    part(tr("Inductor"), ComponentKind::Inductor);
    part(tr("Voltage source"), ComponentKind::VoltageSource);
    part(tr("Current source"), ComponentKind::CurrentSource);
    part(tr("Ground"), ComponentKind::Ground);
}

// The drawing changed: simulate the circuit it draws. A drawing the conversion rejects (two labels
// on one net) does not run; the status says why.
void MainWindow::applyScene()
{
    const schematic::Conversion conversion = schematic::convert(sheet_->scene());
    sceneError_.clear();
    for (const schematic::Diagnostic& diagnostic : conversion.diagnostics) {
        if (diagnostic.severity == schematic::Severity::Error) {
            sceneError_ = diagnostic.message;
            break;
        }
    }
    if (!sceneError_.empty()) {
        control_.stop();
        showStatus();
        return;
    }
    control_.setCircuit(conversion.circuit);
    control_.start();
    showStatus();
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
    if (std::optional<instruments::ScopeFrame> frame = control_.bench().popFrame(0)) {
        scope_->showFrame(std::move(*frame), control_.bench().scopeStopped(0));
    }
    if (std::optional<instruments::DmmReading> reading = control_.bench().popReading(0)) {
        meter_->showReading(*reading);
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
    if (!bench::haveNgspice()) {
        status_->setText(tr("This build has no simulator: circuits will not run."));
        return;
    }
    if (!example_) {
        status_->setText(
            tr("Draw a circuit, or choose one from the Examples menu. The instruments are wired "
               "to the nets labelled in and out."));
        return;
    }
    QStringList lines;
    lines << text(examples_[*example_].name);
    switch (control_.state()) {
    case bench::RunControl::State::Idle:
        lines << (sceneError_.empty() ? tr("Stopped") : text(sceneError_));
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
    const bench::Bench::ScopeSettings scope =
        config.scopes.empty() ? bench::Bench::ScopeSettings{} : config.scopes[0];
    lines << tr("Scope %1: CH1 on %2, CH2 on %3; %4 frames/s shown, %5 ms per paint")
                 .arg(text(scope.name), text(scope.channels[0]), text(scope.channels[1]))
                 .arg(frameRate_, 0, 'f', 0)
                 .arg(scope_->screen().meanPaintMs(), 0, 'f', 2);
    lines << tr("Restarts: %1").arg(control_.restarts());
    status_->setText(lines.join(QStringLiteral("   \u00b7   ")));
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
