// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/bench/examples.h"
#include "proton/bench/run_control.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QString>
#include <QTimer>

#include <cstddef>
#include <optional>
#include <vector>

class QLabel;

namespace proton::app {
class ScopePanel;
}

namespace proton::app {

// The app's main window: the bench, run by a bench::RunControl that a 16 ms UI timer pumps, and an
// Examples menu that loads a built-in circuit into it. Choosing an example sets the circuit and
// the bench's wiring; the next pump restarts the run. The scope is a dock panel bound to the
// bench's scope (ARCHITECTURE.md section 11); the multimeter and generator panels follow in M2.5
// and M2.6, until then the run's state and the multimeter's reading show as text.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Loads example `index` of bench::examples() and runs it, as its menu entry does.
    void loadExample(std::size_t index);
    std::size_t exampleCount() const { return examples_.size(); }

    const bench::RunControl& control() const { return control_; }
    const ScopePanel& scope() const { return *scope_; }
    ScopePanel& scope() { return *scope_; }
    // Frames shown, and pumps run, per second over the last second or so.
    double frameRate() const { return frameRate_; }
    double pumpRate() const { return pumpRate_; }
    // The multimeter has read, and the scope shown a frame, since the example was loaded.
    bool hasReading() const { return reading_.has_value() && framesAtLoad_ < framesShown(); }

private:
    void showAbout();
    void pump();
    void showStatus();
    void applyScope();
    long long framesShown() const;

    std::vector<bench::Example> examples_;
    std::optional<std::size_t> example_; // the loaded one
    bench::RunControl control_;
    QElapsedTimer clock_; // wall-clock time for the pacer
    QTimer timer_;
    QLabel* status_ = nullptr;
    ScopePanel* scope_ = nullptr;

    // What the multimeter read last, shown until it reads again.
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
