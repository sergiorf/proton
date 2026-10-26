// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/bench/examples.h"
#include "proton/bench/run_control.h"
#include "proton/schematic/scene.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QString>
#include <QTimer>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

class QAction;
class QLabel;

namespace proton::app {
namespace sheet {
class PartsPanel;
class SchematicView;
} // namespace sheet
class GeneratorPanel;
class MultimeterPanel;
class ScopePanel;
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
// circuit simulator: then the drawing runs, and every edit or knob restarts it on the next pump;
// Stop ends the run and the panels keep their last frame and reading. The Examples menu loads a
// built-in circuit with its instruments, opening their panels, without switching on. The run's
// state shows in the status bar.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Loads example `index` of bench::examples(), as its menu entry does; it runs if Run is on.
    void loadExample(std::size_t index);
    // Run (F5) on or off, as the toolbar's switch does.
    void setRunning(bool running);
    bool running() const { return running_; }
    std::size_t exampleCount() const { return examples_.size(); }

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
        QWidget* face = nullptr; // a ScopePanel, MultimeterPanel or GeneratorPanel
        std::vector<QString> nets; // the nets the face shows: a meter's + and COM, a generator's
                                   // output, a scope's CH1 and CH2
    };

    void showAbout();
    void pump();
    void showStatus();
    void applyScope(const QString& name);
    void applyMultimeter(const QString& name);
    void applyGenerator(const QString& name);
    void applyScene();
    // Brings every panel in line with the bench: a panel whose instance is gone goes, the others
    // show their instance's settings and nets.
    void syncPanels();
    void buildToolBar();
    const Panel* panelOf(const QString& name) const;

    std::vector<bench::Example> examples_;
    std::optional<std::size_t> example_; // the loaded one, until New sheet
    bench::RunControl control_;
    bool running_ = false; // the Run switch
    QAction* run_ = nullptr;
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
