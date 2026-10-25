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

// The app's main window: the bench, run by a bench::RunControl that a 16 ms UI timer pumps, and an
// Examples menu that loads a built-in circuit into it. Choosing an example sets the circuit and
// the bench's wiring; the next pump restarts the run. For now the bench shows as text (state, the
// multimeter's reading, the scope's last trigger); the instruments arrive in M2.3 to M2.5 as dock
// panels, each bound to an instrument of the bench (ARCHITECTURE.md section 11).
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Loads example `index` of bench::examples() and runs it, as its menu entry does.
    void loadExample(std::size_t index);
    std::size_t exampleCount() const { return examples_.size(); }

    const bench::RunControl& control() const { return control_; }
    // The multimeter has read since the example was loaded.
    bool hasReading() const { return reading_.has_value(); }

private:
    void showAbout();
    void pump();
    void showStatus();

    std::vector<bench::Example> examples_;
    std::optional<std::size_t> example_; // the loaded one
    bench::RunControl control_;
    QElapsedTimer clock_; // wall-clock time for the pacer
    QTimer timer_;
    QLabel* status_ = nullptr;

    // What the bench gave last, shown until it gives the next.
    std::optional<instruments::DmmReading> reading_;
    std::optional<instruments::ScopeFrame> frame_;
};

} // namespace proton::app
