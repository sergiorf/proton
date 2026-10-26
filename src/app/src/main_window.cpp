// SPDX-License-Identifier: AGPL-3.0-only
#include "main_window.h"

#include "generator_panel.h"
#include "multimeter_panel.h"
#include "scope_panel.h"
#include "schematic/parts_panel.h"
#include "schematic/schematic_view.h"
#include "scope_widget.h"

#include "proton/bench/engines.h"
#include "proton/bench/sheet_bench.h"
#include "proton/core/version.h"
#include "proton/schematic/scene_edit.h"
#include "proton/schematic/validate.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QGuiApplication>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScreen>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QToolBar>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>

namespace proton::app {

namespace {

using schematic::InstrumentKind;

constexpr std::chrono::milliseconds kPumpInterval{16}; // one UI tick (ADR 0005)
constexpr int kPanelGap = 24; // pixels between a symbol and its panel

QString text(std::string_view s)
{
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

// The index of instance `name` among `instances`, if it is there.
template <typename Settings>
std::optional<std::size_t> indexOf(const std::vector<Settings>& instances, const QString& name)
{
    for (std::size_t i = 0; i < instances.size(); ++i) {
        if (text(instances[i].name) == name) {
            return i;
        }
    }
    return std::nullopt;
}

QString kindTitle(InstrumentKind kind)
{
    switch (kind) {
    case InstrumentKind::Generator:
        return MainWindow::tr("Function generator");
    case InstrumentKind::Scope:
        return MainWindow::tr("Oscilloscope");
    case InstrumentKind::Multimeter:
        return MainWindow::tr("Multimeter");
    }
    return {};
}

// What a generator panel shows as its output's net: none unless both terminals are wired.
QString generatorNet(const bench::Bench::GeneratorSettings& generator)
{
    return generator.plus.empty() || generator.com.empty() ? QString() : text(generator.plus);
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), examples_(bench::examples()), control_(bench::ngspiceEngines())
{
    setWindowTitle(QStringLiteral("Proton"));
    resize(1440, 900);

    // The schematic in the middle with the parts to place on its left; the instruments' panels
    // float beside their symbols.
    sheet_ = new sheet::SchematicView(this);
    setCentralWidget(sheet_);
    connect(sheet_, &sheet::SchematicView::edited, this, &MainWindow::applyScene);
    connect(sheet_, &sheet::SchematicView::instrumentOpened, this,
            [this](const QString& name) { openPanel(name); });

    parts_ = new sheet::PartsPanel(this);
    auto* partsDock = new QDockWidget(tr("Parts"), this);
    partsDock->setObjectName(QStringLiteral("parts"));
    partsDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    partsDock->setWidget(parts_);
    addDockWidget(Qt::LeftDockWidgetArea, partsDock);
    resizeDocks({partsDock}, {130}, Qt::Horizontal); // one icon a row
    connect(parts_, &sheet::PartsPanel::chosen, this,
            [this](const sheet::SchematicView::Placing& what) { sheet_->placeKind(what); });

    status_ = new QLabel(this);
    status_->setTextFormat(Qt::PlainText);
    statusBar()->addWidget(status_, 1);

    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* newSheet = file->addAction(tr("&New sheet"), this, [this] {
        example_.reset();
        sheet_->clearSheet();
        sheet_->setTool(sheet::SchematicView::Tool::Select);
    });
    newSheet->setShortcut(QKeySequence::New);
    file->addSeparator();
    QAction* quit = file->addAction(tr("&Quit"), this, &QWidget::close);

    buildToolBar();
    quit->setShortcut(QKeySequence::Quit);

    // Undo and redo the sheet's edits (M4.3); the actions name the step they undo.
    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    QAction* undo = sheet_->undoStack().createUndoAction(this, tr("&Undo"));
    undo->setObjectName(QStringLiteral("undo"));
    undo->setShortcuts(QKeySequence::Undo);
    edit->addAction(undo);
    QAction* redo = sheet_->undoStack().createRedoAction(this, tr("&Redo"));
    redo->setObjectName(QStringLiteral("redo"));
    redo->setShortcuts(QKeySequence::Redo);
    edit->addAction(redo);

    // The bench's on switch (F5), in the toolbar and its own menu.
    QMenu* simulate = menuBar()->addMenu(tr("&Simulate"));
    simulate->addAction(run_);

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
    settings_ = examples_[index].bench;
    sheet_->setScene(examples_[index].scene);
    applyScene();
    for (const Panel& panel : panels_) {
        if (auto* scope = qobject_cast<ScopePanel*>(panel.face)) {
            scope->clear();
        } else if (auto* meter = qobject_cast<MultimeterPanel*>(panel.face)) {
            meter->clear();
        }
    }
    for (const schematic::PlacedInstrument& instrument : examples_[index].scene.instruments) {
        openPanel(text(instrument.name));
    }
    framesAtLoad_ = framesShown();
    pump();
}

const MainWindow::Panel* MainWindow::panelOf(const QString& name) const
{
    const auto found =
        std::ranges::find_if(panels_, [&](const Panel& panel) { return panel.name == name; });
    return found != panels_.end() ? &*found : nullptr;
}

ScopePanel* MainWindow::scopePanel(const QString& name) const
{
    const Panel* panel = panelOf(name);
    return panel != nullptr ? qobject_cast<ScopePanel*>(panel->face) : nullptr;
}

MultimeterPanel* MainWindow::multimeterPanel(const QString& name) const
{
    const Panel* panel = panelOf(name);
    return panel != nullptr ? qobject_cast<MultimeterPanel*>(panel->face) : nullptr;
}

GeneratorPanel* MainWindow::generatorPanel(const QString& name) const
{
    const Panel* panel = panelOf(name);
    return panel != nullptr ? qobject_cast<GeneratorPanel*>(panel->face) : nullptr;
}

ScopePanel& MainWindow::scope()
{
    return *qobject_cast<ScopePanel*>(openPanel(text(control_.bench().config().scopes.at(0).name)));
}

MultimeterPanel& MainWindow::multimeter()
{
    return *qobject_cast<MultimeterPanel*>(
        openPanel(text(control_.bench().config().multimeters.at(0).name)));
}

GeneratorPanel& MainWindow::generator()
{
    return *qobject_cast<GeneratorPanel*>(
        openPanel(text(control_.bench().config().generators.at(0).name)));
}

QWidget* MainWindow::openPanel(const QString& name)
{
    if (const Panel* panel = panelOf(name)) {
        panel->window->show();
        panel->window->raise();
        panel->window->activateWindow();
        return panel->face;
    }
    const bench::Bench::Config& config = control_.bench().config();
    const schematic::Scene& scene = sheet_->scene();
    const auto symbol = std::ranges::find_if(scene.instruments, [&](const auto& instrument) {
        return text(instrument.name) == name;
    });
    if (symbol == scene.instruments.end()) {
        return nullptr;
    }

    Panel panel{name, symbol->kind, nullptr, nullptr, {}};
    switch (symbol->kind) {
    case InstrumentKind::Scope: {
        const auto index = indexOf(config.scopes, name);
        if (!index) {
            return nullptr;
        }
        auto* face = new ScopePanel(this);
        face->setConfig(config.scopes[*index].config);
        connect(face, &ScopePanel::changed, this, [this, name] { applyScope(name); });
        connect(face, &ScopePanel::armed, this, [this, name] {
            if (const auto i = indexOf(control_.bench().config().scopes, name)) {
                control_.bench().rearmScope(*i);
            }
        });
        panel.face = face;
        break;
    }
    case InstrumentKind::Multimeter: {
        const auto index = indexOf(config.multimeters, name);
        if (!index) {
            return nullptr;
        }
        const bench::Bench::MultimeterSettings& meter = config.multimeters[*index];
        auto* face = new MultimeterPanel(this);
        face->setConfig(meter.config, text(meter.plus), text(meter.com));
        panel.nets = {text(meter.plus), text(meter.com)};
        connect(face, &MultimeterPanel::changed, this, [this, name] { applyMultimeter(name); });
        panel.face = face;
        break;
    }
    case InstrumentKind::Generator: {
        const auto index = indexOf(config.generators, name);
        if (!index) {
            return nullptr;
        }
        const bench::Bench::GeneratorSettings& generator = config.generators[*index];
        auto* face = new GeneratorPanel(this);
        face->setConfig(generator.config, generatorNet(generator));
        panel.nets = {generatorNet(generator)};
        connect(face, &GeneratorPanel::changed, this, [this, name] { applyGenerator(name); });
        panel.face = face;
        break;
    }
    }

    // A window of its own beside the symbol, kept on the screen: a tool window, above the main
    // window and minimised with it, that moves freely over the sheet without docking.
    panel.window = new QWidget(this, Qt::Tool);
    panel.window->setWindowTitle(tr("%1 %2").arg(kindTitle(symbol->kind), name));
    panel.window->setObjectName(QStringLiteral("panel-%1").arg(name));
    auto* layout = new QVBoxLayout(panel.window);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(panel.face);
    const schematic::Box box = schematic::bounds(*symbol);
    const QPoint anchor =
        sheet_->viewport()->mapToGlobal(sheet_->viewPoint({box.right, box.top})) +
        QPoint(kPanelGap, 0);
    const QScreen* screen = QGuiApplication::screenAt(anchor);
    const QRect area = (screen != nullptr ? screen : QGuiApplication::primaryScreen())
                           ->availableGeometry();
    const QSize hint = panel.face->sizeHint();
    // No bigger than a bench instrument beside the sheet: several fit at once.
    const QSize size(std::max(panel.face->minimumSizeHint().width(),
                              std::min(hint.width(), area.width() * 45 / 100)),
                     std::max(panel.face->minimumSizeHint().height(),
                              std::min(hint.height(), area.height() * 45 / 100)));
    panel.window->resize(size);
    panel.window->move(std::clamp(anchor.x(), area.left(), area.right() - size.width()),
                     std::clamp(anchor.y(), area.top(), area.bottom() - size.height()));
    panel.window->show();
    panels_.push_back(panel);
    return panel.face;
}

void MainWindow::applyScope(const QString& name)
{
    bench::Bench::Config config = control_.bench().config();
    const auto index = indexOf(config.scopes, name);
    const ScopePanel* face = scopePanel(name);
    if (!index || face == nullptr) {
        return;
    }
    config.scopes[*index].config = face->config();
    control_.bench().configure(config);
    bench::remember(settings_, config);
}

void MainWindow::applyMultimeter(const QString& name)
{
    bench::Bench::Config config = control_.bench().config();
    const auto index = indexOf(config.multimeters, name);
    MultimeterPanel* face = multimeterPanel(name);
    if (!index || face == nullptr) {
        return;
    }
    config.multimeters[*index].config = face->config();
    control_.bench().configure(config);
    bench::remember(settings_, config);
    face->clear(); // a new gate: dashes until its reading
}

// A generator knob: the next pump rebuilds the netlist and restarts the run (decision D3).
void MainWindow::applyGenerator(const QString& name)
{
    bench::Bench::Config config = control_.bench().config();
    const auto index = indexOf(config.generators, name);
    const GeneratorPanel* face = generatorPanel(name);
    if (!index || face == nullptr) {
        return;
    }
    config.generators[*index].config = face->config();
    control_.bench().configure(config);
    bench::remember(settings_, config);
}

void MainWindow::syncPanels()
{
    const bench::Bench::Config& config = control_.bench().config();
    std::vector<Panel> kept;
    for (Panel& panel : panels_) {
        bool present = false;
        switch (panel.kind) {
        case InstrumentKind::Scope:
            if (const auto i = indexOf(config.scopes, panel.name)) {
                present = true;
                auto* face = qobject_cast<ScopePanel*>(panel.face);
                if (!(face->config() == config.scopes[*i].config)) {
                    face->setConfig(config.scopes[*i].config);
                    face->clear();
                }
            }
            break;
        case InstrumentKind::Multimeter:
            if (const auto i = indexOf(config.multimeters, panel.name)) {
                present = true;
                const bench::Bench::MultimeterSettings& meter = config.multimeters[*i];
                auto* face = qobject_cast<MultimeterPanel*>(panel.face);
                const std::vector<QString> nets = {text(meter.plus), text(meter.com)};
                if (!(face->config() == meter.config) || panel.nets != nets) {
                    face->setConfig(meter.config, nets[0], nets[1]); // clears the display
                    panel.nets = nets;
                }
            }
            break;
        case InstrumentKind::Generator:
            if (const auto i = indexOf(config.generators, panel.name)) {
                present = true;
                const bench::Bench::GeneratorSettings& generator = config.generators[*i];
                auto* face = qobject_cast<GeneratorPanel*>(panel.face);
                const std::vector<QString> nets = {generatorNet(generator)};
                if (!(face->config() == generator.config) || panel.nets != nets) {
                    face->setConfig(generator.config, nets[0]);
                    panel.nets = nets;
                }
            }
            break;
        }
        if (present) {
            kept.push_back(panel);
        } else {
            panel.window->deleteLater(); // the symbol was deleted: its panel goes with it
        }
    }
    panels_ = std::move(kept);
}

void MainWindow::buildToolBar()
{
    using Tool = sheet::SchematicView::Tool;
    QToolBar* tools = addToolBar(tr("Schematic"));
    tools->setObjectName(QStringLiteral("schematic-tools"));
    auto* group = new QActionGroup(this);
    // None is checked while the Place tool is on: the parts panel shows what is being placed.
    group->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    const auto tool = [&](const QString& name, const QString& tip, const QKeySequence& key,
                          Tool which) {
        QAction* action = tools->addAction(name, this, [this, which] { sheet_->setTool(which); });
        action->setToolTip(tip);
        action->setShortcut(key);
        action->setCheckable(true);
        action->setData(static_cast<int>(which));
        group->addAction(action);
        return action;
    };
    tool(tr("Select"),
         tr("Select, move (drag), turn (R), delete (Del); double-click to edit a part or open an "
            "instrument"),
         QKeySequence(Qt::Key_S), Tool::Select)
        ->setChecked(true);
    tool(tr("Wire"), tr("Click where the wire starts, then where it bends or ends"),
         QKeySequence(Qt::Key_W), Tool::Wire);
    tool(tr("Label"), tr("Click a net to name it"), QKeySequence(Qt::Key_L), Tool::Label);
    tools->addSeparator();
    run_ = tools->addAction(tr("Run"));
    run_->setObjectName(QStringLiteral("run"));
    run_->setCheckable(true);
    run_->setShortcut(QKeySequence(Qt::Key_F5));
    run_->setToolTip(tr("Switch the bench on: simulate the circuit and run the instruments (F5)"));
    connect(run_, &QAction::toggled, this, &MainWindow::setRunning);
    connect(sheet_, &sheet::SchematicView::toolChanged, this, [this, group](Tool which) {
        for (QAction* action : group->actions()) {
            action->setChecked(action->data().toInt() == static_cast<int>(which));
        }
        parts_->showTool(which, sheet_->placing());
    });
}

// The drawing changed: the bench takes the instruments it draws, and the circuit runs. A drawing
// the conversion rejects (two labels on one net) does not run; the status says why.
void MainWindow::applyScene()
{
    const schematic::Conversion conversion = schematic::convert(sheet_->scene());
    const bench::Bench::Config config = bench::benchFor(conversion.instruments, settings_);
    if (!(config == control_.bench().config())) {
        control_.bench().configure(config);
    }
    bench::remember(settings_, config);
    syncPanels();

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
    if (running_) {
        control_.start();
    }
    showStatus();
}

void MainWindow::setRunning(bool running)
{
    running_ = running;
    {
        const QSignalBlocker blocker(run_);
        run_->setChecked(running);
    }
    run_->setText(running ? tr("Stop") : tr("Run"));
    run_->setToolTip(running ? tr("Switch the bench off: stop the simulation (F5)")
                             : tr("Switch the bench on: simulate the circuit and run the "
                                  "instruments (F5)"));
    if (running && sceneError_.empty()) {
        control_.start();
    } else if (!running) {
        control_.stop();
    }
    showStatus();
}

long long MainWindow::framesShown() const
{
    const bench::Bench::Config& config = control_.bench().config();
    const ScopePanel* panel =
        config.scopes.empty() ? nullptr : scopePanel(text(config.scopes[0].name));
    return panel != nullptr ? panel->framesShown() : 0;
}

void MainWindow::pump()
{
    const double now = static_cast<double>(clock_.nsecsElapsed()) * 1e-9;
    ++pumps_;
    control_.pump(now);
    // Every instance's frames and readings are popped; those with a panel show them.
    const bench::Bench::Config& config = control_.bench().config();
    for (std::size_t i = 0; i < config.scopes.size(); ++i) {
        if (std::optional<instruments::ScopeFrame> frame = control_.bench().popFrame(i)) {
            if (ScopePanel* panel = scopePanel(text(config.scopes[i].name))) {
                panel->showFrame(std::move(*frame), control_.bench().scopeStopped(i));
            }
        }
    }
    for (std::size_t i = 0; i < config.multimeters.size(); ++i) {
        if (std::optional<instruments::DmmReading> reading = control_.bench().popReading(i)) {
            if (MultimeterPanel* panel = multimeterPanel(text(config.multimeters[i].name))) {
                panel->showReading(*reading);
            }
            if (i == 0) {
                reading_ = std::move(reading);
            }
        }
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
    const schematic::Scene& scene = sheet_->scene();
    if (scene.parts.empty() && scene.instruments.empty()) {
        status_->setText(
            tr("Place parts and instruments from the panel on the left and wire them, or choose "
               "an example from the Examples menu. Double-click an instrument to open it."));
        return;
    }
    QStringList lines;
    lines << (example_ ? text(examples_[*example_].name) : tr("Sheet"));
    switch (control_.state()) {
    case bench::RunControl::State::Idle:
        lines << (!sceneError_.empty() ? text(sceneError_)
                  : running_           ? tr("Starting")
                                       : tr("Stopped: press Run (F5) to simulate"));
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
    if (!config.scopes.empty()) {
        const bench::Bench::ScopeSettings& scope = config.scopes[0];
        const ScopePanel* panel = scopePanel(text(scope.name));
        const auto net = [](const std::string& name) {
            return name.empty() ? tr("nothing") : text(name);
        };
        lines << tr("Scope %1: CH1 on %2, CH2 on %3; %4 frames/s shown, %5 ms per paint")
                     .arg(text(scope.name), net(scope.channels[0]), net(scope.channels[1]))
                     .arg(frameRate_, 0, 'f', 0)
                     .arg(panel != nullptr ? panel->screen().meanPaintMs() : 0.0, 0, 'f', 2);
    }
    lines << tr("Restarts: %1").arg(control_.restarts());
    status_->setText(lines.join(QStringLiteral("   ·   ")));
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
