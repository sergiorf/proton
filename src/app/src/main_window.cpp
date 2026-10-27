// SPDX-License-Identifier: AGPL-3.0-only
#include "main_window.h"

#include "generator_panel.h"
#include "icons.h"
#include "multimeter_panel.h"
#include "panel/knob_widget.h"
#include "scope_panel.h"
#include "schematic/parts_panel.h"
#include "schematic/schematic_view.h"
#include "scope_widget.h"
#include "start_page.h"

#include "proton/bench/engines.h"
#include "proton/bench/sheet_bench.h"
#include "proton/core/version.h"
#include "proton/schematic/scene_edit.h"
#include "proton/schematic/validate.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QToolBar>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

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

constexpr int kMaxRecentFiles = 8;
const QString kRecentKey = QStringLiteral("recentFiles");

// The example projects, installed beside the executable (the build copies them there).
std::filesystem::path examplesDir()
{
    return std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) /
           "examples";
}

// Proton's own settings, the recent files among them: an INI file in the user's configuration
// folder, which the smoke checks move to a test location (QStandardPaths::setTestModeEnabled).
QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
           QStringLiteral("/proton.ini");
}

std::filesystem::path fsPath(const QString& path)
{
    return std::filesystem::path(path.toStdWString());
}

// What a generator panel shows as its output's net: none unless both terminals are wired.
QString generatorNet(const bench::Bench::GeneratorSettings& generator)
{
    return generator.plus.empty() || generator.com.empty() ? QString() : text(generator.plus);
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), examples_(project::examples(examplesDir())),
      control_(bench::ngspiceEngines())
{
    resize(1440, 900);

    // The schematic in the middle with the parts to place on its left; the instruments' panels
    // float beside their symbols. The start page stands in for the sheet until something starts.
    sheet_ = new sheet::SchematicView(this);
    sheet_->setObjectName(QStringLiteral("sheet"));
    startPage_ = new StartPage(this);
    pages_ = new QStackedWidget(this);
    pages_->addWidget(startPage_);
    pages_->addWidget(sheet_);
    pages_->setCurrentWidget(sheet_);
    setCentralWidget(pages_);
    std::vector<StartPage::Card> cards;
    for (const project::Example& example : examples_) {
        cards.push_back({text(example.name), text(example.description)});
    }
    startPage_->setExamples(cards);
    startPage_->setRecentFiles(recentFiles());
    connect(startPage_, &StartPage::newSheet, this, &MainWindow::newSheet);
    connect(startPage_, &StartPage::openFile, this, &MainWindow::chooseAndOpen);
    connect(startPage_, &StartPage::recentChosen, this,
            [this](const QString& path) { openProject(path); });
    connect(startPage_, &StartPage::exampleChosen, this,
            [this](int index) { loadExample(static_cast<std::size_t>(index)); });
    connect(&sheet_->undoStack(), &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);
    connect(sheet_, &sheet::SchematicView::edited, this, &MainWindow::applyScene);
    connect(sheet_, &sheet::SchematicView::instrumentOpened, this,
            [this](const QString& name) { openPanel(name); });

    parts_ = new sheet::PartsPanel(this);
    partsDock_ = new QDockWidget(tr("Parts"), this);
    partsDock_->setObjectName(QStringLiteral("parts"));
    partsDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                            QDockWidget::DockWidgetClosable);
    partsDock_->setWidget(parts_);
    addDockWidget(Qt::LeftDockWidgetArea, partsDock_);
    resizeDocks({partsDock_}, {130}, Qt::Horizontal); // one icon a row
    connect(parts_, &sheet::PartsPanel::chosen, this,
            [this](const sheet::SchematicView::Placing& what) { sheet_->placeKind(what); });
    connect(sheet_, &sheet::SchematicView::instrumentRenamed, this,
            [this](const QString& from, const QString& to) {
                const schematic::Scene& scene = sheet_->scene();
                const auto symbol = std::ranges::find_if(scene.instruments, [&](const auto& i) {
                    return text(i.name) == from;
                });
                if (symbol != scene.instruments.end()) {
                    bench::carryOver(settings_, symbol->kind, from.toStdString(),
                                     to.toStdString());
                }
                // The old name's panel goes with the next edit; a shown one opens again.
                const Panel* panel = panelOf(from);
                if (panel != nullptr && panel->window->isVisible()) {
                    reopen_ = to;
                }
            });

    status_ = new QLabel(this);
    status_->setTextFormat(Qt::PlainText);
    statusBar()->addWidget(status_, 1);

    buildMenus();
    buildToolBars();
    connect(sheet_, &sheet::SchematicView::selectionChanged, this,
            &MainWindow::updateEditActions);
    connect(sheet_, &sheet::SchematicView::clipboardChanged, this,
            &MainWindow::updateEditActions);
    connect(sheet_, &sheet::SchematicView::toolChanged, this, [this](sheet::SchematicView::Tool which) {
        for (QAction* action : tools_->actions()) {
            action->setChecked(action->data().toInt() == static_cast<int>(which));
        }
        parts_->showTool(which, sheet_->placing());
        updateEditActions();
    });
    updateEditActions();
    setRunState(RunState::Stopped);
    updateTitle();

    clock_.start();
    // Precise: on Windows a coarse 16 ms timer lands on the 15.6 ms system tick and fires every
    // other tick, halving the frame rate (ADR 0004).
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &MainWindow::pump);
    timer_.start(kPumpInterval);
    showStatus();
}

MainWindow::~MainWindow()
{
    // The sheet and its undo stack go after this object's own members: their last signals must
    // not reach it (an undo stack emptied on the way out says it is clean again).
    disconnect(&sheet_->undoStack(), nullptr, this, nullptr);
}

void MainWindow::loadExample(std::size_t index)
{
    if (index >= examples_.size()) {
        return;
    }
    const project::Example& example = examples_[index];
    example_ = index;
    showProject({example.name, example.description, example.scene, example.bench}, {});
}

void MainWindow::showProject(const project::Project& project, const QString& path)
{
    showSheet();
    projectPath_ = path;
    description_ = project.description;
    reading_.reset();
    settings_ = project.settings; // without nets in a file: the drawing gives them (benchFor)
    sheet_->setScene(project.scene);
    applyScene();
    for (const Panel& panel : panels_) {
        if (auto* scope = qobject_cast<ScopePanel*>(panel.face)) {
            scope->clear();
        } else if (auto* meter = qobject_cast<MultimeterPanel*>(panel.face)) {
            meter->clear();
        }
    }
    for (const schematic::PlacedInstrument& instrument : project.scene.instruments) {
        openPanel(text(instrument.name));
    }
    // Side by side rather than each beside its symbol, where they would cover one another, and
    // the drawing in the part of the sheet they leave free.
    arrangePanels();
    QRect free = sheet_->viewport()->rect();
    for (const Panel& panel : panels_) {
        if (panel.window->isVisible()) {
            const QPoint corner =
                sheet_->viewport()->mapFromGlobal(panel.window->frameGeometry().topLeft());
            free.setRight(std::min(free.right(), corner.x() - 1));
        }
    }
    if (free.width() >= sheet_->viewport()->width() / 4) { // else the panels cover the sheet
        sheet_->fitInto(free);
    }
    framesAtLoad_ = framesShown();
    updateTitle();
    pump();
}

bool MainWindow::openProject(const QString& path)
{
    const project::Loaded loaded = project::load(fsPath(path));
    if (!loaded.project) {
        QMessageBox::warning(this, tr("Open project"),
                             tr("%1\n\n%2").arg(QDir::toNativeSeparators(path),
                                                 text(loaded.error)));
        return false;
    }
    example_.reset();
    showProject(*loaded.project, path);
    addRecent(path);
    return true;
}

bool MainWindow::saveProject(const QString& path)
{
    const project::Project saved{QFileInfo(path).completeBaseName().toStdString(), description_,
                                 sheet_->scene(),
                                 project::withoutNets(control_.bench().config())};
    std::string error;
    if (!project::save(fsPath(path), saved, error)) {
        QMessageBox::warning(this, tr("Save project"), text(error));
        return false;
    }
    projectPath_ = path;
    example_.reset();
    sheet_->undoStack().setClean();
    addRecent(path);
    updateTitle();
    showStatus();
    return true;
}

bool MainWindow::save()
{
    return projectPath_.isEmpty() ? chooseAndSave() : saveProject(projectPath_);
}

bool MainWindow::chooseAndSave()
{
    // Beside the open file, or in Documents under the example's name.
    const QString name = example_ ? text(examples_[*example_].name) : tr("circuit");
    const QString suggested =
        !projectPath_.isEmpty()
            ? projectPath_
            : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
                  QLatin1Char('/') + name + text(project::kExtension);
    QString path = QFileDialog::getSaveFileName(this, tr("Save project"), suggested,
                                                tr("Proton projects (*.proton)"));
    if (path.isEmpty()) {
        return false;
    }
    if (!path.endsWith(text(project::kExtension), Qt::CaseInsensitive)) {
        path += text(project::kExtension);
    }
    return saveProject(path);
}

void MainWindow::chooseAndOpen()
{
    const QString dir = !projectPath_.isEmpty()
                            ? QFileInfo(projectPath_).path()
                            : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getOpenFileName(this, tr("Open project"), dir,
                                                      tr("Proton projects (*.proton)"));
    if (!path.isEmpty()) {
        openProject(path);
    }
}

void MainWindow::newSheet()
{
    showSheet();
    example_.reset();
    projectPath_.clear();
    description_.clear();
    sheet_->clearSheet();
    sheet_->setTool(sheet::SchematicView::Tool::Select);
    updateTitle();
}

QStringList MainWindow::recentFiles() const
{
    // Files moved or deleted since are left out.
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    QStringList recent = settings.value(kRecentKey).toStringList();
    recent.removeIf([](const QString& path) { return !QFileInfo::exists(path); });
    return recent;
}

void MainWindow::addRecent(const QString& path)
{
    const QString file = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QStringList recent = recentFiles();
    recent.removeIf([&](const QString& p) { return p.compare(file, Qt::CaseInsensitive) == 0; });
    recent.prepend(file);
    while (recent.size() > kMaxRecentFiles) {
        recent.removeLast();
    }
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(kRecentKey, recent);
    startPage_->setRecentFiles(recent);
}

void MainWindow::fillRecentMenu()
{
    recentMenu_->clear();
    const QStringList recent = recentFiles();
    for (qsizetype i = 0; i < recent.size(); ++i) {
        const QString path = recent[i];
        QAction* action = recentMenu_->addAction(
            QStringLiteral("&%1 %2").arg(i + 1).arg(QFileInfo(path).fileName()), this,
            [this, path] { openProject(path); });
        action->setObjectName(QStringLiteral("file-recent-%1").arg(i));
        action->setToolTip(QDir::toNativeSeparators(path));
    }
    if (recent.isEmpty()) {
        recentMenu_->addAction(tr("No recent projects"))->setEnabled(false);
    }
}

void MainWindow::showStartPage()
{
    startPage_->setRecentFiles(recentFiles());
    pages_->setCurrentWidget(startPage_);
}

bool MainWindow::startPageShown() const
{
    return pages_->currentWidget() == startPage_;
}

void MainWindow::showSheet()
{
    pages_->setCurrentWidget(sheet_);
}

QString MainWindow::documentName() const
{
    return !projectPath_.isEmpty() ? QFileInfo(projectPath_).completeBaseName()
           : example_              ? text(examples_[*example_].name)
                                   : tr("Untitled");
}

void MainWindow::updateTitle()
{
    // Marked while a file has edits it has not saved; an example or a new sheet has no file.
    setWindowTitle(documentName() + QStringLiteral("[*] - Proton"));
    setWindowModified(!projectPath_.isEmpty() && !sheet_->undoStack().isClean());
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
        const bench::Bench::ScopeSettings& scope = config.scopes[*index];
        auto* face = new ScopePanel(this);
        face->setConfig(scope.config);
        face->setInputs(text(scope.channels[0]), text(scope.channels[1]));
        panel.nets = {text(scope.channels[0]), text(scope.channels[1])};
        connect(face, &ScopePanel::changed, this, [this, name] { applyScope(name); });
        connect(face, &ScopePanel::armed, this, [this, name] {
            if (const auto i = indexOf(control_.bench().config().scopes, name)) {
                control_.bench().rearmScope(*i);
            }
        });
        connect(face, &ScopePanel::forced, this, [this, name] {
            if (const auto i = indexOf(control_.bench().config().scopes, name)) {
                control_.bench().forceScope(*i);
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
                const bench::Bench::ScopeSettings& scope = config.scopes[*i];
                auto* face = qobject_cast<ScopePanel*>(panel.face);
                if (!(face->config() == scope.config)) {
                    face->setConfig(scope.config);
                    face->clear();
                }
                const std::vector<QString> nets = {text(scope.channels[0]),
                                                   text(scope.channels[1])};
                if (panel.nets != nets) {
                    face->setInputs(nets[0], nets[1]);
                    panel.nets = nets;
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

template <typename Slot>
QAction* MainWindow::makeAction(const char* id, const QString& text, const QKeySequence& key,
                                Slot slot)
{
    auto* action = new QAction(text, this);
    action->setObjectName(QString::fromLatin1(id));
    action->setShortcut(key);
    connect(action, &QAction::triggered, this, slot);
    return action;
}

void MainWindow::buildMenus()
{
    using Tool = sheet::SchematicView::Tool;
    const auto withIcon = [](QAction* action, Icon which) {
        action->setIcon(icon(which));
        return action;
    };

    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(withIcon(makeAction("file-new", tr("&New"), QKeySequence::New,
                                        [this] { newSheet(); }),
                             Icon::New));
    file->addAction(withIcon(makeAction("file-open", tr("&Open..."), QKeySequence::Open,
                                        [this] { chooseAndOpen(); }),
                             Icon::Open));
    recentMenu_ = file->addMenu(tr("Open &recent"));
    recentMenu_->setObjectName(QStringLiteral("file-recent"));
    connect(recentMenu_, &QMenu::aboutToShow, this, &MainWindow::fillRecentMenu);
    QMenu* examples = file->addMenu(tr("&Examples"));
    examples->setObjectName(QStringLiteral("file-examples"));
    for (std::size_t i = 0; i < examples_.size(); ++i) {
        QAction* action = examples->addAction(text(examples_[i].name), this,
                                              [this, i] { loadExample(i); });
        action->setObjectName(QStringLiteral("file-example-%1").arg(i));
        action->setToolTip(text(examples_[i].description));
    }
    if (examples_.empty()) {
        examples->addAction(tr("No examples were found next to Proton"))->setEnabled(false);
    }
    file->addSeparator();
    file->addAction(withIcon(
        makeAction("file-save", tr("&Save"), QKeySequence::Save, [this] { save(); }),
        Icon::Save));
    file->addAction(makeAction("file-save-as", tr("Save &as..."),
                               QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S),
                               [this] { chooseAndSave(); }));
    file->addSeparator();
    file->addAction(withIcon(makeAction("file-export-image", tr("&Export image..."),
                                        QKeySequence(Qt::CTRL | Qt::Key_E),
                                        [this] {
                                            const QString path = QFileDialog::getSaveFileName(
                                                this, tr("Export image"),
                                                QStringLiteral("schematic.png"),
                                                tr("PNG image (*.png)"));
                                            if (!path.isEmpty() && !exportImage(path)) {
                                                QMessageBox::warning(
                                                    this, tr("Export image"),
                                                    tr("Could not save %1.").arg(path));
                                            }
                                        }),
                             Icon::ExportImage));
    file->addSeparator();
    // Ctrl+Q spelled out: Qt's standard Quit key is empty on Windows.
    file->addAction(makeAction("file-quit", tr("&Quit"), QKeySequence(Qt::CTRL | Qt::Key_Q),
                               [this] { close(); }));

    // Undo and redo the sheet's edits (M4.3); the actions name the step they undo.
    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    QAction* undo = sheet_->undoStack().createUndoAction(this, tr("&Undo"));
    undo->setObjectName(QStringLiteral("undo"));
    undo->setShortcuts(QKeySequence::Undo);
    undo->setIcon(icon(Icon::Undo));
    edit->addAction(undo);
    QAction* redo = sheet_->undoStack().createRedoAction(this, tr("&Redo"));
    redo->setObjectName(QStringLiteral("redo"));
    redo->setShortcuts(QKeySequence::Redo);
    redo->setIcon(icon(Icon::Redo));
    edit->addAction(redo);
    edit->addSeparator();
    cut_ = withIcon(makeAction("edit-cut", tr("Cu&t"), QKeySequence::Cut, [this] { sheet_->cut(); }),
                    Icon::Cut);
    copy_ = withIcon(
        makeAction("edit-copy", tr("&Copy"), QKeySequence::Copy, [this] { sheet_->copy(); }),
        Icon::Copy);
    paste_ = withIcon(
        makeAction("edit-paste", tr("&Paste"), QKeySequence::Paste, [this] { sheet_->paste(); }),
        Icon::Paste);
    delete_ = withIcon(makeAction("edit-delete", tr("&Delete"), QKeySequence::Delete,
                                  [this] { sheet_->deleteSelection(); }),
                       Icon::Delete);
    edit->addActions({cut_, copy_, paste_, delete_});
    QAction* selectAll = makeAction("edit-select-all", tr("Select &all"), QKeySequence::SelectAll,
                                    [this] { sheet_->selectAll(); });
    edit->addAction(selectAll);
    edit->addSeparator();
    rotate_ = withIcon(makeAction("edit-rotate", tr("&Rotate"), QKeySequence(Qt::Key_R),
                                  [this] { sheet_->rotate(); }),
                       Icon::Rotate);
    mirror_ = withIcon(makeAction("edit-mirror", tr("&Mirror"), QKeySequence(Qt::Key_X),
                                  [this] { sheet_->mirror(); }),
                       Icon::Mirror);
    edit->addActions({rotate_, mirror_});
    edit->addSeparator();
    properties_ = withIcon(makeAction("edit-properties", tr("Pr&operties..."),
                                      QKeySequence(Qt::Key_E),
                                      [this] { sheet_->editProperties(); }),
                           Icon::Properties);
    edit->addAction(properties_);
    // Delete also with Backspace, as the sheet always took it.
    delete_->setShortcuts({QKeySequence(QKeySequence::Delete), QKeySequence(Qt::Key_Backspace)});
    // The sheet's right-click menu (PLAN.md M4.10) offers the same actions.
    sheet_->setContextActions({delete_, rotate_, mirror_, properties_}, {paste_, selectAll});

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(withIcon(makeAction("view-zoom-in", tr("Zoom &in"), QKeySequence::ZoomIn,
                                        [this] { sheet_->zoomIn(); }),
                             Icon::ZoomIn));
    view->addAction(withIcon(makeAction("view-zoom-out", tr("Zoom &out"), QKeySequence::ZoomOut,
                                        [this] { sheet_->zoomOut(); }),
                             Icon::ZoomOut));
    view->addAction(withIcon(makeAction("view-zoom-fit", tr("Zoom to &fit"),
                                        QKeySequence(Qt::CTRL | Qt::Key_0),
                                        [this] { sheet_->zoomToFit(); }),
                             Icon::ZoomFit));
    view->addSeparator();
    QAction* grid = withIcon(makeAction("view-grid", tr("&Grid"), QKeySequence(Qt::Key_G),
                                        [this](bool on) { sheet_->setGridShown(on); }),
                             Icon::Grid);
    grid->setCheckable(true);
    grid->setChecked(sheet_->gridShown());
    view->addAction(grid);
    QAction* partsPanel = partsDock_->toggleViewAction();
    partsPanel->setObjectName(QStringLiteral("view-parts-panel"));
    partsPanel->setText(tr("&Parts panel"));
    partsPanel->setShortcut(QKeySequence(Qt::Key_F9));
    view->addAction(partsPanel);

    // Place: the parts panel for the keyboard, then the drawing tools.
    QMenu* place = menuBar()->addMenu(tr("&Place"));
    bool instruments = false;
    for (const sheet::SchematicView::Placing& what : parts_->entries()) {
        const bool instrument = std::holds_alternative<InstrumentKind>(what);
        if (instrument && !instruments) {
            place->addSeparator(); // components, then instruments
            instruments = true;
        }
        QAction* action = place->addAction(parts_->icon(what), parts_->name(what), this,
                                           [this, what] { sheet_->placeKind(what); });
        action->setObjectName((instrument ? QStringLiteral("place-instrument-")
                                          : QStringLiteral("place-part-")) +
                              parts_->name(what).toLower().replace(QLatin1Char(' '),
                                                                   QLatin1Char('-')));
    }
    place->addSeparator();
    tools_ = new QActionGroup(this);
    // None is checked while the Place tool is on: the parts panel shows what is being placed.
    tools_->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    const auto tool = [&](const char* id, const QString& name, const QString& tip,
                          const QKeySequence& key, Icon which, Tool kind) {
        QAction* action =
            withIcon(makeAction(id, name, key, [this, kind] { sheet_->setTool(kind); }), which);
        action->setStatusTip(tip);
        action->setCheckable(true);
        action->setData(static_cast<int>(kind));
        tools_->addAction(action);
        place->addAction(action);
        return action;
    };
    tool("tool-select", tr("&Select"),
         tr("Select, move (drag), turn (R), delete (Del); double-click to edit a part or open an "
            "instrument"),
         QKeySequence(Qt::Key_S), Icon::Select, Tool::Select)
        ->setChecked(true);
    tool("tool-wire", tr("&Wire"), tr("Click where the wire starts, then where it bends or ends"),
         QKeySequence(Qt::Key_W), Icon::Wire, Tool::Wire);
    tool("tool-label", tr("&Label"), tr("Click a net to name it"), QKeySequence(Qt::Key_L),
         Icon::Label, Tool::Label);
    tool("tool-junction", tr("&Junction"), tr("Click where crossing wires should connect"),
         QKeySequence(Qt::Key_J), Icon::Junction, Tool::Junction);

    // The bench's switch: Run, Pause, Stop.
    QMenu* simulate = menuBar()->addMenu(tr("&Simulate"));
    run_ = withIcon(makeAction("run", tr("&Run"), QKeySequence(Qt::Key_F5),
                               [this] { setRunState(RunState::Running); }),
                    Icon::Run);
    pause_ = withIcon(makeAction("simulate-pause", tr("&Pause"), QKeySequence(Qt::Key_F6),
                                 [this] { pauseRun(); }),
                      Icon::Pause);
    stop_ = withIcon(makeAction("simulate-stop", tr("&Stop"), QKeySequence(Qt::SHIFT | Qt::Key_F5),
                                [this] { setRunState(RunState::Stopped); }),
                     Icon::Stop);
    run_->setStatusTip(tr("Switch the bench on: simulate the circuit and run the instruments"));
    pause_->setStatusTip(tr("Freeze the run where it is; Run goes on from there"));
    stop_->setStatusTip(tr("Switch the bench off; Run starts again from t = 0"));
    for (QAction* action : {run_, pause_}) {
        action->setCheckable(true); // lit while the bench is in that state
    }
    simulate->addActions({run_, pause_, stop_});
    simulate->addSeparator();
    instrumentsMenu_ = simulate->addMenu(tr("&Instruments"));
    instrumentsMenu_->setObjectName(QStringLiteral("simulate-instruments"));
    connect(instrumentsMenu_, &QMenu::aboutToShow, this, &MainWindow::fillInstrumentsMenu);

    // How a drag turns the panels' knobs: round them as a hand turns a knob, or straight.
    QMenu* options = menuBar()->addMenu(tr("&Options"));
    QAction* circular = options->addAction(tr("Turn knobs by dragging &round them"));
    circular->setObjectName(QStringLiteral("circular-knobs"));
    circular->setCheckable(true);
    circular->setChecked(panel::KnobWidget::dragMode() == panel::KnobWidget::DragMode::Circular);
    circular->setToolTip(tr("Off: drag up or right to turn clockwise, down or left the other way"));
    connect(circular, &QAction::toggled, this, [](bool on) {
        panel::KnobWidget::setDragMode(on ? panel::KnobWidget::DragMode::Circular
                                          : panel::KnobWidget::DragMode::Straight);
    });

    windowMenu_ = menuBar()->addMenu(tr("&Window"));
    windowMenu_->setObjectName(QStringLiteral("window"));
    connect(windowMenu_, &QMenu::aboutToShow, this, &MainWindow::fillWindowMenu);
    // Made once, so that their shortcuts work before the menu is first opened.
    addAction(makeAction("window-arrange", tr("&Arrange panels"),
                         QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A),
                         [this] { arrangePanels(); }));
    addAction(makeAction("window-close-all", tr("&Close all panels"),
                         QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W),
                         [this] { closePanels(); }));
    fillWindowMenu();

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About Proton"), this, &MainWindow::showAbout)
        ->setObjectName(QStringLiteral("help-about"));
    // Qt's own dialog names the Qt version and its license, as the LGPL asks (LICENSING.md).
    help->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt)
        ->setObjectName(QStringLiteral("help-about-qt"));

    // Every tooltip names its shortcut, as the menus do.
    for (QAction* action : findChildren<QAction*>()) {
        if (!action->shortcut().isEmpty()) {
            action->setToolTip(QStringLiteral("%1 (%2)").arg(
                action->text().remove(QLatin1Char('&')).remove(QStringLiteral("...")),
                action->shortcut().toString(QKeySequence::NativeText)));
        }
    }
}

void MainWindow::buildToolBars()
{
    const auto bar = [this](const QString& title, const char* id,
                            std::initializer_list<const char*> actions) {
        QToolBar* toolBar = addToolBar(title);
        toolBar->setObjectName(QString::fromLatin1(id));
        toolBar->setIconSize(QSize(24, 24));
        for (const char* name : actions) {
            toolBar->addAction(findChild<QAction*>(QString::fromLatin1(name)));
        }
        return toolBar;
    };
    bar(tr("File"), "toolbar-file", {"file-new", "file-open", "file-save", "file-export-image"});
    bar(tr("Edit"), "toolbar-edit",
        {"undo", "redo", "edit-cut", "edit-copy", "edit-paste", "edit-delete", "edit-rotate",
         "edit-mirror"});
    bar(tr("View"), "toolbar-view",
        {"view-zoom-in", "view-zoom-out", "view-zoom-fit", "view-grid"});
    bar(tr("Tools"), "toolbar-tools", {"tool-select", "tool-wire", "tool-label", "tool-junction"});
    QToolBar* simulation = bar(tr("Simulation"), "toolbar-simulation",
                               {"run", "simulate-pause", "simulate-stop"});
    runTime_ = new QLabel(simulation);
    runTime_->setObjectName(QStringLiteral("run-time"));
    runTime_->setTextFormat(Qt::PlainText);
    runTime_->setContentsMargins(8, 0, 8, 0);
    QFont mono = runTime_->font();
    mono.setFamily(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    runTime_->setFont(mono);
    runTime_->setMinimumWidth(runTime_->fontMetrics().horizontalAdvance(
                                  QStringLiteral("Paused  t = 0000.000 s")) +
                              16);
    simulation->addWidget(runTime_);
    // Undo and Redo take the step's name as their text: their tooltips follow, with the shortcut.
    for (const char* id : {"undo", "redo"}) {
        auto* action = findChild<QAction*>(QString::fromLatin1(id));
        connect(action, &QAction::changed, this, [action] {
            const QString tip = action->text().remove(QLatin1Char('&')) + QStringLiteral(" (") +
                                action->shortcut().toString(QKeySequence::NativeText) +
                                QLatin1Char(')');
            if (action->toolTip() != tip) {
                action->setToolTip(tip);
            }
        });
    }
}

void MainWindow::updateEditActions()
{
    using Tool = sheet::SchematicView::Tool;
    const bool selecting = sheet_->tool() == Tool::Select;
    const bool selected = selecting && sheet_->hasSelection();
    const schematic::Selection selection = sheet_->selection();
    const bool turnable =
        selecting && !(selection.parts.empty() && selection.instruments.empty());
    cut_->setEnabled(selected);
    copy_->setEnabled(selected);
    delete_->setEnabled(selected);
    paste_->setEnabled(sheet_->canPaste());
    rotate_->setEnabled(turnable || sheet_->tool() == Tool::Place);
    mirror_->setEnabled(turnable);
    properties_->setEnabled(selecting && sheet_->canEditProperties());
}

void MainWindow::fillInstrumentsMenu()
{
    instrumentsMenu_->clear();
    const schematic::Scene& scene = sheet_->scene();
    for (const schematic::PlacedInstrument& instrument : scene.instruments) {
        const QString name = text(instrument.name);
        QAction* action =
            instrumentsMenu_->addAction(tr("%1 %2").arg(kindTitle(instrument.kind), name), this,
                                        [this, name] { openPanel(name); });
        action->setObjectName(QStringLiteral("simulate-instrument-%1").arg(name));
    }
    if (scene.instruments.empty()) {
        instrumentsMenu_->addAction(tr("No instruments on the sheet"))->setEnabled(false);
    }
}

void MainWindow::fillWindowMenu()
{
    windowMenu_->clear();
    for (const Panel& panel : panels_) {
        QWidget* window = panel.window;
        QAction* action = windowMenu_->addAction(window->windowTitle(), this, [this, window] {
            window->show();
            window->raise();
            window->activateWindow();
        });
        action->setObjectName(QStringLiteral("window-panel-%1").arg(panel.name));
        action->setCheckable(true);
        action->setChecked(window->isVisible());
    }
    if (!panels_.empty()) {
        windowMenu_->addSeparator();
    }
    windowMenu_->addAction(findChild<QAction*>(QStringLiteral("window-arrange")));
    windowMenu_->addAction(findChild<QAction*>(QStringLiteral("window-close-all")));
}

std::vector<QWidget*> MainWindow::panelWindows() const
{
    std::vector<QWidget*> windows;
    for (const Panel& panel : panels_) {
        windows.push_back(panel.window);
    }
    return windows;
}

void MainWindow::arrangePanels()
{
    const QRect area(sheet_->viewport()->mapToGlobal(QPoint(0, 0)), sheet_->viewport()->size());
    const QScreen* screen = QGuiApplication::screenAt(area.center());
    const QRect desk =
        (screen != nullptr ? screen : QGuiApplication::primaryScreen())->availableGeometry();
    constexpr int gap = kPanelGap / 2;
    int columnRight = area.right(); // the current column's right edge
    int columnLeft = columnRight;   // and its left edge, as wide as its widest row
    int rowTop = area.top();
    int rowLeft = columnRight; // where the current row's next panel ends
    int rowHeight = 0;
    for (const Panel& panel : panels_) {
        if (!panel.window->isVisible()) {
            continue;
        }
        const QSize size = panel.window->frameGeometry().size();
        if (columnLeft != columnRight && rowLeft - size.width() < columnLeft) {
            // Not beside the row's last panel: a row below, or else a column to the left.
            if (rowTop + rowHeight + gap + size.height() <= area.bottom()) {
                rowTop += rowHeight + gap;
            } else {
                columnRight = columnLeft - gap;
                columnLeft = columnRight;
                rowTop = area.top();
            }
            rowLeft = columnRight;
            rowHeight = 0;
        }
        // Kept on the screen when the sheet has no room left for it.
        panel.window->move(
            std::clamp(rowLeft - size.width(), desk.left(), desk.right() - size.width()),
            std::clamp(rowTop, desk.top(), desk.bottom() - size.height()));
        rowLeft -= size.width() + gap;
        rowHeight = std::max(rowHeight, size.height());
        columnLeft = std::min(columnLeft, rowLeft + gap);
    }
}

void MainWindow::closePanels()
{
    for (const Panel& panel : panels_) {
        panel.window->hide();
    }
}

bool MainWindow::exportImage(const QString& path)
{
    const QImage image = sheet_->image();
    return !image.isNull() && image.save(path, "PNG");
}

// The drawing changed: the bench takes the instruments it draws, the sheet marks its diagnostics
// (bench::diagnose), and the circuit runs. A drawing the conversion rejects (two labels on one
// net) does not run; the status says why.
void MainWindow::applyScene()
{
    const schematic::Conversion conversion = schematic::convert(sheet_->scene());
    const bench::Bench::Config config = bench::benchFor(conversion.instruments, settings_);
    if (!(config == control_.bench().config())) {
        control_.bench().configure(config);
    }
    bench::remember(settings_, config);
    sheet_->setDiagnostics(bench::diagnose(conversion, config), conversion);
    syncPanels();
    if (!reopen_.isEmpty()) {
        openPanel(std::exchange(reopen_, QString()));
    }

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
    if (runState_ == RunState::Running) {
        control_.start();
    }
    showStatus();
}

void MainWindow::setRunning(bool running)
{
    setRunState(running ? RunState::Running : RunState::Stopped);
}

void MainWindow::pauseRun()
{
    if (runState_ == RunState::Running) {
        setRunState(RunState::Paused);
    } else {
        setRunState(runState_); // the action's check mark back as it was
    }
}

void MainWindow::setRunState(RunState state)
{
    runState_ = state;
    switch (state) {
    case RunState::Running:
        if (sceneError_.empty()) {
            control_.start(); // goes on from a pause
        }
        break;
    case RunState::Paused:
        control_.pause();
        break;
    case RunState::Stopped:
        control_.stop();
        break;
    }
    run_->setChecked(state == RunState::Running);
    pause_->setChecked(state == RunState::Paused);
    run_->setEnabled(state != RunState::Running);
    pause_->setEnabled(state == RunState::Running);
    stop_->setEnabled(state != RunState::Stopped);
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
    if (runTime_ != nullptr) {
        const QString time = QStringLiteral("t = %1 s").arg(control_.lastTime(), 0, 'f', 3);
        runTime_->setText(runState_ == RunState::Stopped ? tr("Stopped")
                          : runState_ == RunState::Paused ? tr("Paused  %1").arg(time)
                                                          : time);
    }
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
    lines << documentName();
    switch (control_.state()) {
    case bench::RunControl::State::Idle:
        lines << (!sceneError_.empty()             ? text(sceneError_)
                  : runState_ == RunState::Stopped ? tr("Stopped: press Run (F5) to simulate")
                                                   : tr("Starting"));
        break;
    case bench::RunControl::State::Paused:
        lines << tr("Paused at t = %1 s: Run (F5) goes on, Stop (Shift+F5) ends the run")
                     .arg(control_.lastTime(), 0, 'f', 3);
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
