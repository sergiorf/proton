// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/bench/run_control.h"
#include "proton/project/project.h"
#include "proton/schematic/scene.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

class QAction;
class QActionGroup;
class QDockWidget;
class QKeySequence;
class QLabel;
class QMenu;
class QStackedWidget;

namespace proton::app {
namespace sheet {
class PartsPanel;
class SchematicView;
} // namespace sheet
class GeneratorPanel;
class MultimeterPanel;
class ScopePanel;
class StartPage;
class SupplyPanel;
} // namespace proton::app

namespace proton::app {

// The app's main window: the schematic sheet in the middle with the parts panel on its left, and
// the bench, run by a bench::RunControl that a 16 ms UI timer pumps (ARCHITECTURE.md section 11).
//
// The instruments are the ones drawn on the sheet (ADR 0009): every edit gives the bench one
// instance per instrument symbol, on the nets its terminals are wired to (bench::benchFor), with
// the settings that instance had. Double-clicking a symbol opens that instance's front panel in a
// window of its own beside it, a tool window that stays above the main one and never docks into
// it; closing it only hides it, and deleting the symbol removes the panel. Settings stay with an
// instance's name, so an instrument deleted and brought back by undo has them again.
//
// Nothing runs until the student switches the bench on with Run (F5), as on a real bench or in a
// circuit simulator: then the drawing runs, and every edit or knob restarts it on the next pump.
// Pause (F6) freezes the run, simulation time and instruments included, and Run goes on from
// there; Stop (Shift+F5) ends it, the panels keeping their last frame and reading, and Run starts
// again from t = 0 (bench::RunControl::pause). File > Examples loads a built-in circuit with its
// instruments, opening their panels side by side (arrangePanels) with the drawing in the sheet
// they leave free, without switching on. The run's state shows in the status
// bar and its simulation time beside the switch.
//
// The menus and toolbars (PLAN.md M4.7) are a desktop circuit simulator's: File, Edit, View,
// Place, Simulate, Window, with Proton's own icons (icons.h) and every shortcut shown. Each action
// has an object name ("edit-copy", "simulate-pause") for the smoke checks.
//
// Project files (PLAN.md M4.11, project/project.h): File > Open, Save, Save As and Open Recent
// read and write the drawing with every instance's settings. The title shows the file, marked
// while there are edits since it was opened or saved; an example or a new sheet has no file yet,
// so Save asks where. The examples are the project files in examples/ next to the executable.
// The recent files, newest first, are kept in Proton's settings file (QStandardPaths). The start
// page (showStartPage) stands in for the sheet until the student starts something.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    enum class RunState {
        Stopped,
        Running,
        Paused,
    };

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Loads example `index` of the examples, as its menu entry does; it runs if Run is on.
    void loadExample(std::size_t index);
    // File > Open and Save As without their dialogs, and File > Save. Open shows what went wrong
    // in a message box and returns false; Save does the same when the file cannot be written.
    bool openProject(const QString& path);
    bool saveProject(const QString& path);
    bool save();
    // The project's file, empty for an example or a new sheet; its recent files, newest first.
    const QString& projectPath() const { return projectPath_; }
    QStringList recentFiles() const;
    // The start page in place of the sheet, as the app opens; anything started from it, a menu
    // or an example shows the sheet again.
    void showStartPage();
    bool startPageShown() const;
    // Run (F5) or Stop, as the toolbar's switch does.
    void setRunning(bool running);
    // Pause (F6): freezes a running bench.
    void pauseRun();
    bool running() const { return runState_ == RunState::Running; }
    RunState runState() const { return runState_; }
    std::size_t exampleCount() const { return examples_.size(); }

    // Saves the drawing as a PNG, as File > Export image does after asking where.
    bool exportImage(const QString& path);
    // Window > Arrange panels: the open panels side by side over the sheet's right side, from its
    // top right corner leftwards, in columns as wide as their first panel: the next panel goes
    // beside the one before while the column has room, else in a row below it, else at the top
    // of a new column to the left; each stays on the screen. Loading an example does this too.
    // Window > Close all panels hides them.
    void arrangePanels();
    void closePanels();
    // The windows of the panels made so far, shown or hidden.
    std::vector<QWidget*> panelWindows() const;

    const bench::RunControl& control() const { return control_; }
    sheet::SchematicView& sheet() { return *sheet_; }
    sheet::PartsPanel& parts() { return *parts_; }

    // Opens the front panel of the instrument called `name` on the sheet, as double-clicking its
    // symbol does; nullptr when there is none.
    QWidget* openPanel(const QString& name);
    // The open (or hidden) panel of instance `name`; nullptr when it has none of that kind.
    ScopePanel* scopePanel(const QString& name) const;
    MultimeterPanel* multimeterPanel(const QString& name) const;
    GeneratorPanel* generatorPanel(const QString& name) const;
    SupplyPanel* supplyPanel(const QString& name) const;
    // The panel of the first scope, multimeter or generator on the bench, opened: for the smoke
    // checks, after an example has put one there.
    ScopePanel& scope();
    MultimeterPanel& multimeter();
    GeneratorPanel& generator();

    // Frames the first scope's panel has shown since it was made.
    long long framesShown() const;
    // The last reading of the bench's first multimeter since the example was loaded.
    const std::optional<instruments::DmmReading>& reading() const { return reading_; }
    // Frames shown, and pumps run, per second over the last second or so.
    double frameRate() const { return frameRate_; }
    double pumpRate() const { return pumpRate_; }
    // The multimeter has read, and the scope shown a frame, since the example was loaded.
    bool hasReading() const { return reading_.has_value() && framesAtLoad_ < framesShown(); }

private:
    // An instance's front panel.
    struct Panel
    {
        QString name;
        schematic::InstrumentKind kind = schematic::InstrumentKind::Scope;
        QWidget* window = nullptr; // the tool window holding the face
        QWidget* face = nullptr; // a ScopePanel, MultimeterPanel, GeneratorPanel or SupplyPanel
        std::vector<QString> nets; // the nets the face shows: a meter's + and COM, a generator's
                                   // output, a scope's CH1 and CH2, a supply's + and - per channel
    };

    void showAbout();
    // Shows `project` on the sheet with its instruments' panels side by side, as from `path`
    // (empty: an example, with no file of its own yet).
    void showProject(const project::Project& project, const QString& path);
    void showSheet();
    void newSheet();
    void chooseAndOpen();
    bool chooseAndSave();
    void addRecent(const QString& path);
    void fillRecentMenu();
    void updateTitle();
    // The file's name without its extension, the example's name, or "Untitled".
    QString documentName() const;
    void pump();
    void showStatus();
    void setRunState(RunState state);
    void buildMenus();
    void buildToolBars();
    void updateEditActions();
    void fillInstrumentsMenu();
    void fillWindowMenu();
    // A menu action called `id`, with its shortcut in its tooltip, doing `slot`.
    template <typename Slot>
    QAction* makeAction(const char* id, const QString& text, const QKeySequence& key, Slot slot);
    void applyScope(const QString& name);
    void applyMultimeter(const QString& name);
    void applyGenerator(const QString& name);
    void applySupply(const QString& name);
    void applyScene();
    // Brings every panel in line with the bench: a panel whose instance is gone goes, the others
    // show their instance's settings and nets.
    void syncPanels();
    const Panel* panelOf(const QString& name) const;

    std::vector<project::Example> examples_;
    std::optional<std::size_t> example_; // the loaded one, until New sheet
    QString projectPath_;                // the open project's file; empty for none
    std::string description_;            // the project's description, kept when saved
    QMenu* recentMenu_ = nullptr;
    QStackedWidget* pages_ = nullptr; // the start page and the sheet
    StartPage* startPage_ = nullptr;
    bench::RunControl control_;
    RunState runState_ = RunState::Stopped; // the simulation switch
    QAction* run_ = nullptr;
    QAction* pause_ = nullptr;
    QAction* stop_ = nullptr;
    QLabel* runTime_ = nullptr; // the run's simulation time, beside the switch
    // Actions that need a selection, the clipboard or one item to edit.
    QAction* cut_ = nullptr;
    QAction* copy_ = nullptr;
    QAction* paste_ = nullptr;
    QAction* delete_ = nullptr;
    QAction* rotate_ = nullptr;
    QAction* mirror_ = nullptr;
    QAction* properties_ = nullptr;
    QActionGroup* tools_ = nullptr; // Select, Wire, Label, Junction
    QMenu* instrumentsMenu_ = nullptr;
    QMenu* windowMenu_ = nullptr;
    QDockWidget* partsDock_ = nullptr;
    QString reopen_; // a renamed instrument whose panel opens again under its new name
    // Every instance's settings by name, those deleted from the sheet included (bench::remember).
    bench::Bench::Config settings_;
    QElapsedTimer clock_; // wall-clock time for the pacer
    QTimer timer_;
    QLabel* status_ = nullptr;
    sheet::SchematicView* sheet_ = nullptr;
    sheet::PartsPanel* parts_ = nullptr;
    std::string sceneError_; // why the drawing does not run, from the scene conversion
    std::vector<Panel> panels_;

    // What the first multimeter read last, shown until it reads again.
    std::optional<instruments::DmmReading> reading_;
    long long framesAtLoad_ = 0;
    // The displayed frame rate, measured over about a second of wall-clock time.
    double frameRate_ = 0.0;
    long long rateFrames_ = 0;
    double pumpRate_ = 0.0;
    long long pumps_ = 0;
    long long ratePumps_ = 0;
    double rateStart_ = 0.0;
};

} // namespace proton::app
