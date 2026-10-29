// SPDX-License-Identifier: AGPL-3.0-only
#include "smoke.h"

#include "generator_panel.h"
#include "main_window.h"
#include "multimeter_panel.h"
#include "panel/knob_widget.h"
#include "panel/lit_button.h"
#include "panel/selector_widget.h"
#include "scope_panel.h"
#include "schematic/parts_panel.h"
#include "schematic/schematic_view.h"
#include "scope_widget.h"
#include "supply_panel.h"

#include "proton/bench/bench.h"
#include "proton/core/spice_number.h"
#include "proton/schematic/netlist.h"
#include "proton/instruments/knob.h"
#include "proton/instruments/knob_steps.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHelpEvent>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QList>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointF>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QUndoStack>
#include <QWheelEvent>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace proton::app {

namespace {

using instruments::Scope;

constexpr std::chrono::seconds kExampleTimeout{30};      // generous: CI runners are slow
constexpr std::chrono::milliseconds kExampleDwell{1500}; // over a second for the frame rate
constexpr std::chrono::milliseconds kLatencyBound{2000};  // as the latency test's loose bound

void wheel(QWidget* widget, int notches, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    const QPointF at(widget->width() / 2.0, widget->height() / 4.0);
    QWheelEvent event(at, widget->mapToGlobal(at), QPoint(), QPoint(0, 120 * notches),
                      Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(widget, &event);
}

void key(QWidget* widget, int key)
{
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
}

void mouse(QWidget* widget, QEvent::Type type, QPointF at, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, at, widget->mapToGlobal(at), Qt::LeftButton, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
}

// A drag upward by `pixels`, in small moves as a hand would.
void drag(QWidget* widget, double pixels)
{
    QPointF at(widget->width() / 2.0, widget->height() / 4.0);
    mouse(widget, QEvent::MouseButtonPress, at, Qt::LeftButton);
    for (int i = 0; i < 4; ++i) {
        at.ry() -= pixels / 4.0;
        mouse(widget, QEvent::MouseMove, at, Qt::LeftButton);
    }
    mouse(widget, QEvent::MouseButtonRelease, at, Qt::NoButton);
}

// A click without moving: a push.
void push(QWidget* widget)
{
    const QPointF at(widget->width() / 2.0, widget->height() / 4.0);
    mouse(widget, QEvent::MouseButtonPress, at, Qt::LeftButton);
    mouse(widget, QEvent::MouseButtonRelease, at, Qt::NoButton);
}

// Presses Run, as a student switches the bench on.
void pressRun(MainWindow& window)
{
    if (!window.running()) {
        window.findChild<QAction*>(QStringLiteral("run"))->trigger();
    }
}

class Checker
{
public:
    void check(bool ok, const QString& what)
    {
        if (!ok) {
            std::fprintf(stderr, "proton: panel check failed: %s\n", what.toStdString().c_str());
            failed_ = true;
        }
    }
    bool failed() const { return failed_; }

private:
    bool failed_ = false;
};

} // namespace

void smokeExamples(MainWindow& window)
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
    // A loaded example waits for Run, as a bench waits to be switched on.
    if (window.control().state() != bench::RunControl::State::Idle) {
        std::fprintf(stderr, "proton: the example ran before Run was pressed\n");
        QTimer::singleShot(0, &window, [] { QApplication::exit(1); });
        return;
    }
    pressRun(window);
    QObject::connect(timer, &QTimer::timeout, &window, [&window, timer, progress] {
        using State = bench::RunControl::State;
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
        // The meter's display shows the reading as the meter formats it, as proton-spike prints
        // it; the voltage divider reads half its 5 V battery, on the 20 V range.
        const MultimeterPanel& meter = window.multimeter();
        const std::optional<instruments::DmmReading>& reading = window.reading();
        const QString shown = meter.displayText() + QLatin1Char(' ') + meter.displayUnit();
        if (!reading || meter.displayText() != QString::fromStdString(reading->display.text) ||
            meter.displayUnit() != QString::fromStdString(reading->display.unit)) {
            std::fprintf(stderr, "proton: example %zu: the meter shows '%s', not its reading\n",
                         progress->example, shown.toStdString().c_str());
            timer->stop();
            QApplication::exit(1);
            return;
        }
        if (progress->example == 1 && shown != QStringLiteral("2.500 V")) {
            std::fprintf(stderr, "proton: the voltage divider reads '%s', not 2.500 V\n",
                         shown.toStdString().c_str());
            timer->stop();
            QApplication::exit(1);
            return;
        }
        const ScopeWidget& screen = window.scope().screen();
        std::printf("proton: example %zu: meter %s; %.0f pumps/s, %.0f frames/s shown, "
                    "speed %.2f; paint mean %.2f ms, max %.2f ms over %lld paints\n",
                    progress->example, shown.toStdString().c_str(), window.pumpRate(),
                    window.frameRate(),
                    window.control().speed(), screen.meanPaintMs(), screen.maxPaintMs(),
                    screen.paints());
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

void smokePanel(MainWindow& window)
{
    window.loadExample(0);
    pressRun(window);
    ScopePanel& panel = window.scope();
    Checker checker;

    // Every knob turns by wheel, arrow key and a straight drag, and each turn changes the settings.
    panel::KnobWidget::setDragMode(panel::KnobWidget::DragMode::Straight);
    const QList<panel::KnobWidget*> knobs = panel.findChildren<panel::KnobWidget*>();
    checker.check(knobs.size() == 7, QStringLiteral("seven knobs, found %1").arg(knobs.size()));
    for (panel::KnobWidget* knob : knobs) {
        const QString name = knob->accessibleName();
        Scope::Config before = panel.config();
        wheel(knob, 1);
        checker.check(panel.config() != before, name + QStringLiteral(": wheel"));
        before = panel.config();
        knob->setFocus();
        key(knob, Qt::Key_Up);
        checker.check(panel.config() != before, name + QStringLiteral(": arrow key"));
        before = panel.config();
        drag(knob, 3 * instruments::Knob::kPixelsPerClick);
        checker.check(panel.config() != before, name + QStringLiteral(": drag"));
        before = panel.config();
        key(knob, Qt::Key_PageDown);
        checker.check(panel.config() != before, name + QStringLiteral(": page key"));
    }

    // Pushing a position knob centres its channel's ground; pushing the level knob centres the
    // trigger on its source's screen.
    for (panel::KnobWidget* knob : knobs) {
        const QString name = knob->accessibleName();
        if (name.endsWith(QStringLiteral("Position"))) {
            push(knob);
            const std::size_t c = name.startsWith(QStringLiteral("CH1")) ? 0 : 1;
            checker.check(panel.config().channels[c].offset == 0.0,
                          name + QStringLiteral(": push centres"));
        } else if (name == QStringLiteral("Level")) {
            push(knob);
            const Scope::Config& config = panel.config();
            const std::size_t source =
                config.trigger.source == Scope::TriggerSource::Channel2 ? 1 : 0;
            checker.check(config.trigger.level == config.channels[source].offset,
                          name + QStringLiteral(": push centres"));
        } else if (name == QStringLiteral("Horizontal position")) {
            push(knob);
            checker.check(panel.config().triggerPosition == 0.0,
                          name + QStringLiteral(": push centres the trigger"));
        }
    }

    // Every setting button changes the settings; Run/Stop and Single change the state.
    for (panel::LitButton* button : panel.findChildren<panel::LitButton*>()) {
        const QString name = button->accessibleName();
        if (name == QStringLiteral("Run / Stop") || name == QStringLiteral("Single") ||
            name == QStringLiteral("Force")) {
            continue;
        }
        Scope::Config before = panel.config();
        // CH1 as trigger source and Auto are already chosen: choose the other first.
        if (name == QStringLiteral("Trigger CH1") || name == QStringLiteral("Auto")) {
            for (panel::LitButton* other : panel.findChildren<panel::LitButton*>()) {
                if (other->accessibleName() ==
                    (name == QStringLiteral("Trigger CH1") ? QStringLiteral("Trigger CH2")
                                                           : QStringLiteral("Normal"))) {
                    other->click();
                }
            }
            before = panel.config();
        }
        button->click();
        checker.check(panel.config() != before, name + QStringLiteral(": click"));
        // The channel buttons hide their trace: show it again.
        if (name == QStringLiteral("CH1") || name == QStringLiteral("CH2")) {
            const std::size_t c = name == QStringLiteral("CH1") ? 0 : 1;
            checker.check(!panel.config().channels[c].shown, name + QStringLiteral(": hides"));
            button->click();
            checker.check(panel.config().channels[c].shown, name + QStringLiteral(": shows"));
        }
    }

    const auto knobNamed = [&](const QString& name) -> panel::KnobWidget* {
        for (panel::KnobWidget* knob : knobs) {
            if (knob->accessibleName() == name) {
                return knob;
            }
        }
        return nullptr;
    };
    const auto buttonNamed = [&](const QString& name) -> panel::LitButton* {
        for (panel::LitButton* button : panel.findChildren<panel::LitButton*>()) {
            if (button->accessibleName() == name) {
                return button;
            }
        }
        return nullptr;
    };

    // Coupling goes DC, AC, GND and round (M4.5).
    if (panel::LitButton* coupling = buttonNamed(QStringLiteral("CH2 coupling"))) {
        std::vector<Scope::Coupling> seen;
        for (int i = 0; i < 3; ++i) {
            coupling->click();
            seen.push_back(panel.config().channels[1].coupling);
        }
        checker.check(std::ranges::find(seen, Scope::Coupling::Gnd) != seen.end() &&
                          std::ranges::find(seen, Scope::Coupling::Ac) != seen.end() &&
                          std::ranges::find(seen, Scope::Coupling::Dc) != seen.end(),
                      QStringLiteral("CH2 coupling: DC, AC and GND"));
    }

    // Force asks for a frame at once.
    if (panel::LitButton* force = buttonNamed(QStringLiteral("Force"))) {
        int forced = 0;
        const QMetaObject::Connection counting =
            QObject::connect(&panel, &ScopePanel::forced, [&forced] { ++forced; });
        force->click();
        QObject::disconnect(counting);
        checker.check(forced == 1, QStringLiteral("Force: forced()"));
    } else {
        checker.check(false, QStringLiteral("a Force button"));
    }

    // Pushing a scale knob makes it variable between the 1-2-5 steps; pushing again puts it back.
    if (panel::KnobWidget* scale = knobNamed(QStringLiteral("CH1 Scale"))) {
        const auto onStep = [](double v) {
            for (const double s : instruments::oneTwoFiveSteps(1e-3, 10.0)) {
                if (std::abs(s - v) <= 1e-12 * s) {
                    return true;
                }
            }
            return false;
        };
        push(scale);
        checker.check(scale->variable(), QStringLiteral("CH1 Scale: push makes it variable"));
        wheel(scale, 1);
        checker.check(!onStep(panel.config().channels[0].voltsPerDiv),
                      QStringLiteral("CH1 Scale: variable between the steps"));
        push(scale);
        checker.check(!scale->variable() && onStep(panel.config().channels[0].voltsPerDiv),
                      QStringLiteral("CH1 Scale: push again puts it on a step"));
    }

    // Shift turns fine, Ctrl coarse: a tenth and ten clicks of the level knob.
    if (panel::KnobWidget* level = knobNamed(QStringLiteral("Level"))) {
        const auto source = static_cast<std::size_t>(panel.config().trigger.source);
        const double click = panel.config().channels[source].voltsPerDiv / 10.0;
        double before = panel.config().trigger.level;
        std::this_thread::sleep_for(std::chrono::milliseconds(150)); // not a fast turn
        wheel(level, 1, Qt::ShiftModifier);
        checker.check(std::abs(panel.config().trigger.level - before - click / 10.0) < 1e-9,
                      QStringLiteral("Level: Shift turns fine"));
        before = panel.config().trigger.level;
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        wheel(level, -1, Qt::ControlModifier);
        checker.check(std::abs(panel.config().trigger.level - before + click * 10.0) < 1e-9,
                      QStringLiteral("Level: Ctrl turns coarse"));

        // Dragged straight, a drag to the right turns it clockwise even when the hand drifts down,
        // and one to the left anticlockwise.
        const auto dragAcross = [&](double dx, double dy) {
            QPointF at(level->width() / 2.0, level->height() / 4.0);
            mouse(level, QEvent::MouseButtonPress, at, Qt::LeftButton);
            for (int i = 0; i < 4; ++i) {
                at += QPointF(dx / 4.0, dy / 4.0);
                mouse(level, QEvent::MouseMove, at, Qt::LeftButton);
            }
            mouse(level, QEvent::MouseButtonRelease, at, Qt::NoButton);
        };
        const double pixels = 4 * instruments::Knob::kPixelsPerClick;
        before = panel.config().trigger.level;
        dragAcross(pixels, pixels / 4); // right, a little down
        checker.check(panel.config().trigger.level > before,
                      QStringLiteral("Level: a drag to the right turns it up"));
        before = panel.config().trigger.level;
        dragAcross(-pixels, -pixels / 4); // left, a little up
        checker.check(panel.config().trigger.level < before,
                      QStringLiteral("Level: a drag to the left turns it down"));

        // Hovered, it shows that it can be turned.
        QEvent enter(QEvent::Enter);
        QCoreApplication::sendEvent(level, &enter);
        checker.check(level->hovered() && level->cursor().shape() == Qt::OpenHandCursor,
                      QStringLiteral("Level: hover feedback"));
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(level, &leave);

        // Dragged round its centre, the default: the hand turning it clockwise goes across the
        // top and down the right side, and the knob turns with it all the way; anticlockwise the
        // mirror image. A half turn is ten clicks.
        panel::KnobWidget::setDragMode(panel::KnobWidget::DragMode::Circular);
        const QPointF centre(level->width() / 2.0, 6.0 + 27.0); // a large continuous knob
        const auto turnRound = [&](double fromDegrees, double toDegrees) {
            const auto at = [&](double degrees) {
                const double a = degrees * std::numbers::pi / 180.0;
                return centre + QPointF(25 * std::sin(a), -25 * std::cos(a));
            };
            mouse(level, QEvent::MouseButtonPress, at(fromDegrees), Qt::LeftButton);
            for (int step = 1; step <= 18; ++step) {
                mouse(level, QEvent::MouseMove,
                      at(fromDegrees + (toDegrees - fromDegrees) * step / 18.0), Qt::LeftButton);
            }
            mouse(level, QEvent::MouseButtonRelease, at(toDegrees), Qt::NoButton);
        };
        before = panel.config().trigger.level;
        turnRound(0.0, 180.0); // from the top, right and down to the bottom
        checker.check(std::abs(panel.config().trigger.level - before - 10 * click) < 1e-9,
                      QStringLiteral("Level: a half turn clockwise round it is ten clicks up"));
        before = panel.config().trigger.level;
        turnRound(0.0, -180.0); // from the top, left and down to the bottom
        checker.check(std::abs(panel.config().trigger.level - before + 10 * click) < 1e-9,
                      QStringLiteral("Level: a half turn anticlockwise is ten clicks down"));
        // A stepped knob the same way: clockwise is a faster timebase, its pointer going round.
        if (panel::KnobWidget* time = knobNamed(QStringLiteral("Time/div"))) {
            const double slower = panel.config().timePerDiv;
            const QPointF timeCentre(time->width() / 2.0, 6.0 + 27.0 + 27.0);
            mouse(time, QEvent::MouseButtonPress, timeCentre + QPointF(0, -25), Qt::LeftButton);
            for (int step = 1; step <= 9; ++step) {
                const double a = step * 10.0 * std::numbers::pi / 180.0;
                mouse(time, QEvent::MouseMove,
                      timeCentre + QPointF(25 * std::sin(a), -25 * std::cos(a)), Qt::LeftButton);
            }
            mouse(time, QEvent::MouseButtonRelease, timeCentre + QPointF(25, 0), Qt::NoButton);
            checker.check(panel.config().timePerDiv < slower,
                          QStringLiteral("Time/div: a turn clockwise round it is faster"));
        }
    }
    for (panel::LitButton* button : panel.findChildren<panel::LitButton*>()) {
        if (button->accessibleName() == QStringLiteral("Single")) {
            button->click();
            checker.check(panel.config().mode == Scope::Mode::Single,
                          QStringLiteral("Single: single mode"));
        }
    }
    for (panel::LitButton* button : panel.findChildren<panel::LitButton*>()) {
        if (button->accessibleName() == QStringLiteral("Run / Stop")) {
            button->click(); // after Single: run again
            checker.check(panel.config().mode != Scope::Mode::Single,
                          QStringLiteral("Run / Stop: runs after Single"));
            button->click(); // stop
            checker.check(panel.screen().status() == QStringLiteral("Stop"),
                          QStringLiteral("Run / Stop: stops"));
        }
    }

    // The multimeter: the dial turns by wheel and keys, RANGE fixes the range and steps it, AUTO
    // goes back to auto range.
    MultimeterPanel& meter = window.multimeter();
    using Mode = instruments::Multimeter::Mode;
    for (panel::SelectorWidget* dial : meter.findChildren<panel::SelectorWidget*>()) {
        const Mode before = meter.config().mode;
        wheel(dial, before == Mode::DcVoltage ? 1 : -1);
        checker.check(meter.config().mode != before, QStringLiteral("Function: wheel"));
        dial->setFocus();
        key(dial, meter.config().mode == Mode::DcVoltage ? Qt::Key_Up : Qt::Key_Down);
        checker.check(meter.config().mode == before, QStringLiteral("Function: arrow key"));
    }
    using Range = instruments::Multimeter::Range;
    for (panel::LitButton* button : meter.findChildren<panel::LitButton*>()) {
        if (button->accessibleName() == QStringLiteral("Range")) {
            button->click();
            const Range fixed = meter.config().range;
            checker.check(fixed != Range::Auto, QStringLiteral("Range: fixes the range"));
            button->click();
            checker.check(meter.config().range != fixed && meter.config().range != Range::Auto,
                          QStringLiteral("Range: steps"));
        }
    }
    for (panel::LitButton* button : meter.findChildren<panel::LitButton*>()) {
        if (button->accessibleName() == QStringLiteral("Auto")) {
            button->click();
            checker.check(meter.config().range == Range::Auto, QStringLiteral("Auto: auto range"));
        }
    }

    // The generator: each knob turns the digit under its cursor, each waveform button selects its
    // waveform, a number typed in a unit applies on Enter or when the unit is chosen, and Output
    // switches the output (M2.6, M4.6).
    GeneratorPanel& generator = window.generator();
    using Generator = instruments::FunctionGenerator;
    using Setting = GeneratorPanel::Setting;
    const auto unitList = [&](const QString& name) -> QComboBox* {
        for (QComboBox* list : generator.findChildren<QComboBox*>()) {
            if (list->accessibleName() == name) {
                return list;
            }
        }
        return nullptr;
    };
    const auto field = [&](const QString& name) -> QLineEdit* {
        for (QLineEdit* edit : generator.findChildren<QLineEdit*>()) {
            if (edit->accessibleName() == name) {
                return edit;
            }
        }
        return nullptr;
    };
    const auto knob = [&](const QString& name) -> panel::KnobWidget* {
        for (panel::KnobWidget* widget : generator.findChildren<panel::KnobWidget*>()) {
            if (widget->accessibleName() == name) {
                return widget;
            }
        }
        return nullptr;
    };
    // Each field shows its setting exactly, as a short number in the unit beside it.
    const auto entriesShowTheSettings = [&](const QString& after) {
        const Generator::Config& config = generator.config();
        for (const QString name : {QStringLiteral("Frequency"), QStringLiteral("Amplitude"),
                                   QStringLiteral("Offset")}) {
            QLineEdit* edit = field(name + QStringLiteral(" entry"));
            QComboBox* unit = unitList(name + QStringLiteral(" unit"));
            if (edit == nullptr || unit == nullptr) {
                checker.check(false, name + QStringLiteral(": an entry field and a unit list"));
                continue;
            }
            const double value = name == QStringLiteral("Frequency")   ? config.frequency
                                 : name == QStringLiteral("Amplitude") ? config.amplitude
                                                                       : config.offset;
            const std::optional<double> number =
                core::parseSpiceNumber(edit->text().toStdString());
            checker.check(number && std::abs(*number * unit->currentData().toDouble() - value) <=
                                        1e-12 * std::max(1.0, std::abs(value)) &&
                              edit->text().size() <= 8,
                          name + QStringLiteral(" shows ") + edit->text() + QLatin1Char(' ') +
                              unit->currentText() + QStringLiteral(" after ") + after);
        }
    };
    for (panel::KnobWidget* widget : generator.findChildren<panel::KnobWidget*>()) {
        const Generator::Config before = generator.config();
        for (int click = 0; click < 3; ++click) {
            wheel(widget, 1);
        }
        checker.check(generator.config() != before,
                      widget->accessibleName() + QStringLiteral(": wheel"));
        entriesShowTheSettings(widget->accessibleName());
    }
    for (panel::LitButton* button : generator.findChildren<panel::LitButton*>()) {
        const QString name = button->accessibleName();
        const Generator::Config before = generator.config();
        button->click();
        if (name == QStringLiteral("Output")) {
            checker.check(generator.config().output != before.output,
                          name + QStringLiteral(": click"));
            button->click(); // back on
        } else {
            checker.check(button->lit(), name + QStringLiteral(": lit when chosen"));
        }
    }
    // Back to a sine, so the frequency's controls are enabled.
    for (panel::LitButton* button : generator.findChildren<panel::LitButton*>()) {
        if (button->accessibleName() == QStringLiteral("Sine")) {
            button->click();
        }
    }

    // 2.2 kHz by typing 2.2 with kHz chosen, then 1.5 applied by choosing MHz, as a unit key.
    QLineEdit* frequencyField = field(QStringLiteral("Frequency entry"));
    QComboBox* frequencyUnit = unitList(QStringLiteral("Frequency unit"));
    if (frequencyField != nullptr && frequencyUnit != nullptr) {
        frequencyField->setText(QStringLiteral("2.2"));
        frequencyUnit->setCurrentIndex(frequencyUnit->findText(QStringLiteral("kHz")));
        key(frequencyField, Qt::Key_Return);
        checker.check(generator.config().frequency == 2.2e3,
                      QStringLiteral("Frequency entry: 2.2 with kHz is 2.2 kHz"));
        frequencyField->setText(QStringLiteral("1.5"));
        const int mega = frequencyUnit->findText(QStringLiteral("MHz"));
        frequencyUnit->setCurrentIndex(mega);
        Q_EMIT frequencyUnit->activated(mega);
        checker.check(generator.config().frequency == 1.5e6,
                      QStringLiteral("Frequency unit: choosing MHz applies 1.5 MHz"));
        entriesShowTheSettings(QStringLiteral("typing"));
    }

    // 1.592 kHz with the cursor and the knob: from 1 kHz, click the hundreds on the display, turn
    // them to 5, Right to the tens, turn to 9, Right to the units, turn to 2.
    panel::KnobWidget* frequencyKnob = knob(QStringLiteral("Frequency"));
    if (frequencyField != nullptr && frequencyUnit != nullptr && frequencyKnob != nullptr) {
        frequencyField->setText(QStringLiteral("1"));
        frequencyUnit->setCurrentIndex(frequencyUnit->findText(QStringLiteral("kHz")));
        key(frequencyField, Qt::Key_Return);
        const QPoint hundreds = generator.digitPosition(Setting::Frequency, 2);
        checker.check(!hundreds.isNull(), QStringLiteral("the display shows the hundreds"));
        mouse(generator.display(), QEvent::MouseButtonPress, hundreds, Qt::LeftButton);
        mouse(generator.display(), QEvent::MouseButtonRelease, hundreds, Qt::NoButton);
        checker.check(generator.active() == Setting::Frequency &&
                          generator.digits(Setting::Frequency).cursor() == 2,
                      QStringLiteral("clicking the hundreds puts the cursor there"));
        wheel(frequencyKnob, 5);
        key(frequencyKnob, Qt::Key_Right);
        wheel(frequencyKnob, 9);
        key(frequencyKnob, Qt::Key_Right);
        wheel(frequencyKnob, 2);
        checker.check(generator.config().frequency == 1592.0,
                      QStringLiteral("1.592 kHz with the cursor and the knob, got %1 Hz")
                          .arg(generator.config().frequency, 0, 'g', 12));
        checker.check(generator.digits(Setting::Frequency).text("Hz").text == "1.592 kHz",
                      QStringLiteral("the display shows 1.592 kHz"));
        // Up one in the units from 1.599 kHz carries into the tens and hundreds.
        wheel(frequencyKnob, 7);
        wheel(frequencyKnob, 1);
        checker.check(generator.config().frequency == 1600.0,
                      QStringLiteral("1.599 kHz up one is 1.600 kHz"));
    }

    const bool failed = checker.failed();
    QTimer::singleShot(0, &window, [failed] { QApplication::exit(failed ? 1 : 0); });
}

void smokeLatency(MainWindow& window)
{
    struct Progress
    {
        QElapsedTimer clock;
        bool turned = false;
        std::size_t restartsBefore = 0;
        long long framesBefore = 0;
        double frequencyBefore = 0.0;
    };
    auto progress = std::make_shared<Progress>();
    auto* timer = new QTimer(&window);
    window.loadExample(0);
    pressRun(window);
    progress->clock.start();
    QObject::connect(timer, &QTimer::timeout, &window, [&window, timer, progress] {
        const auto fail = [&](const char* why) {
            std::fprintf(stderr, "proton: generator latency: %s\n", why);
            timer->stop();
            QApplication::exit(1);
        };
        if (progress->clock.elapsed() > std::chrono::milliseconds(kExampleTimeout).count()) {
            fail("timed out");
            return;
        }
        if (window.control().state() != bench::RunControl::State::Running) {
            fail(window.control().error().c_str());
            return;
        }
        if (!progress->turned) {
            if (window.framesShown() == 0) {
                return; // wait for the run to show something first
            }
            panel::KnobWidget* knob = nullptr;
            for (panel::KnobWidget* candidate :
                 window.generator().findChildren<panel::KnobWidget*>()) {
                if (candidate->accessibleName() == QStringLiteral("Frequency")) {
                    knob = candidate;
                }
            }
            if (knob == nullptr) {
                fail("no frequency knob");
                return;
            }
            progress->restartsBefore = window.control().restarts();
            progress->frequencyBefore = window.generator().config().frequency;
            progress->clock.restart();
            wheel(knob, 1);
            progress->turned = true;
            return;
        }
        // The first frame shown after the restart is from the new run.
        if (window.control().restarts() > progress->restartsBefore &&
            progress->framesBefore == 0) {
            progress->framesBefore = window.framesShown();
            return;
        }
        if (progress->framesBefore != 0 && window.framesShown() > progress->framesBefore) {
            const double ms = static_cast<double>(progress->clock.nsecsElapsed()) * 1e-6;
            std::printf("proton: generator %.4g Hz -> %.4g Hz: first new frame after %.1f ms\n",
                        progress->frequencyBefore, window.generator().config().frequency, ms);
            std::fflush(stdout);
            timer->stop();
            if (window.generator().config().frequency <= progress->frequencyBefore) {
                fail("the frequency did not go up");
                return;
            }
            if (ms > static_cast<double>(kLatencyBound.count())) {
                fail("the new frame came too late");
                return;
            }
            window.close();
        }
    });
    timer->start(1); // check often: this is a timing
}

namespace {

using schematic::ComponentKind;
using schematic::InstrumentKind;
using schematic::Point;

// Clicks `what`'s icon in the parts panel, on its tab, turns it `turns` quarter turns with R, and
// clicks the sheet at `at`, as a student places a part.
void placeFromPanel(MainWindow& window, const sheet::SchematicView::Placing& what, Point at,
                    int turns = 0)
{
    const auto [target, icon] = window.parts().locate(what);
    if (target == nullptr) {
        return;
    }
    mouse(target, QEvent::MouseButtonPress, icon, Qt::LeftButton);
    mouse(target, QEvent::MouseButtonRelease, icon, Qt::NoButton);
    sheet::SchematicView& sheet = window.sheet();
    for (int i = 0; i < turns; ++i) {
        key(&sheet, Qt::Key_R);
    }
    const QPointF point = sheet.viewPoint(at);
    mouse(sheet.viewport(), QEvent::MouseButtonPress, point, Qt::LeftButton);
    mouse(sheet.viewport(), QEvent::MouseButtonRelease, point, Qt::NoButton);
}

// The smallest and largest value of channel `c` in `scope`'s frame; nothing without one.
std::optional<std::pair<double, double>> extent(const ScopePanel* scope, std::size_t c)
{
    if (scope == nullptr || !scope->screen().frame() ||
        scope->screen().frame()->values[c].empty()) {
        return std::nullopt;
    }
    const std::vector<double>& values = scope->screen().frame()->values[c];
    const auto [low, high] = std::ranges::minmax_element(values);
    return std::pair{*low, *high};
}

void checkEditor(MainWindow& window)
{
    sheet::SchematicView& sheet = window.sheet();
    sheet.setScene({});
    // The divider: V1, R1 across the top, R2 down the right, ground along the bottom.
    placeFromPanel(window, ComponentKind::VoltageSource, {0, 2}); // V1: (0,0), (0,4)
    placeFromPanel(window, ComponentKind::Resistor, {4, 0});      // R1: (2,0), (6,0)
    placeFromPanel(window, ComponentKind::Resistor, {8, 2}, 1);   // R2: (8,0), (8,4)
    placeFromPanel(window, ComponentKind::Ground, {4, 6});
    placeFromPanel(window, ComponentKind::Inductor, {30, 30}); // placed, then deleted
    placeFromPanel(window, ComponentKind::Capacitor, {30, 34});
    placeFromPanel(window, ComponentKind::CurrentSource, {30, 38});
    sheet.drawWire({0, 0}, {2, 0});
    sheet.drawWire({6, 0}, {8, 0});
    sheet.drawWire({0, 4}, {0, 6});
    sheet.drawWire({0, 6}, {8, 6});
    sheet.drawWire({8, 4}, {8, 6});
    // dmm1 across R2: + to the middle, COM down to the bottom wire.
    placeFromPanel(window, InstrumentKind::Multimeter, {12, -5}); // +: (11,-3), COM: (13,-3)
    sheet.drawWire({8, 0}, {11, -3});
    sheet.drawWire({13, -3}, {13, 6});
    sheet.drawWire({13, 6}, {8, 6});
    // dmm2 on the top: + to R1's left end, COM round to the bottom.
    placeFromPanel(window, InstrumentKind::Multimeter, {2, -6}); // +: (1,-4), COM: (3,-4)
    sheet.drawWire({1, -4}, {1, 0});
    sheet.drawWire({3, -4}, {3, -2});
    sheet.drawWire({3, -2}, {-3, -2});
    sheet.drawWire({-3, -2}, {-3, 6});
    sheet.drawWire({-3, 6}, {0, 6});
    // gen1 into a 1k load, R3, standing below.
    placeFromPanel(window, InstrumentKind::Generator, {-10, 12}); // +: (-8,11), COM: (-8,13)
    placeFromPanel(window, ComponentKind::Resistor, {-6, 13}, 1); // R3: (-6,11), (-6,15)
    placeFromPanel(window, ComponentKind::Ground, {-6, 17});
    sheet.drawWire({-8, 11}, {-6, 11});
    sheet.drawWire({-6, 15}, {-6, 17});
    sheet.drawWire({-8, 13}, {-8, 17});
    sheet.drawWire({-8, 17}, {-6, 17});
    // scope1, placed first: CH1 on the divider's middle, CH2 on the generator's output.
    placeFromPanel(window, InstrumentKind::Scope, {25, -1}); // CH1: (22,-2), CH2: (22,0)
    // scope2: CH1 on the generator's output.
    placeFromPanel(window, InstrumentKind::Scope, {5, 12}); // CH1: (2,11), CH2: (2,13)
    sheet.drawWire({2, 11}, {-6, 11});
    sheet.drawWire({22, -2}, {16, -2});
    sheet.drawWire({16, -2}, {16, 0});
    sheet.drawWire({16, 0}, {11, 0});
    sheet.drawWire({22, 0}, {22, 11});
    sheet.drawWire({22, 11}, {2, 11});
    // The extra parts go again: every kind has been placed from the panel.
    sheet.removeItems({4, 5, 6}, {}, {});
    sheet.setTool(sheet::SchematicView::Tool::Select);
    pressRun(window);

    const schematic::Scene& scene = sheet.scene();
    if (scene.parts.size() != 6 || scene.instruments.size() != 5) {
        std::fprintf(stderr, "proton: the parts panel placed %zu parts and %zu instruments\n",
                     scene.parts.size(), scene.instruments.size());
        QApplication::exit(1);
        return;
    }
    // Every panel opened from its symbol, all at once.
    for (const char* name : {"dmm1", "dmm2", "gen1", "scope1", "scope2"}) {
        const schematic::PlacedInstrument* symbol = nullptr;
        for (const schematic::PlacedInstrument& instrument : scene.instruments) {
            if (instrument.name == name) {
                symbol = &instrument;
            }
        }
        if (symbol == nullptr) {
            std::fprintf(stderr, "proton: no instrument %s on the sheet\n", name);
            QApplication::exit(1);
            return;
        }
        const QPointF at = sheet.viewPoint(symbol->placement.position);
        mouse(sheet.viewport(), QEvent::MouseButtonDblClick, at, Qt::LeftButton);
    }

    auto* timer = new QTimer(&window);
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    QObject::connect(timer, &QTimer::timeout, &window, [&window, timer, clock] {
        const auto shown = [&](const char* name) {
            const MultimeterPanel* meter = window.multimeterPanel(QString::fromLatin1(name));
            if (meter == nullptr) {
                return QString();
            }
            return meter->displayText() + QLatin1Char(' ') + meter->displayUnit();
        };
        // Ideal measurements and an ideal generator: the frame holds the probed voltages exactly.
        const double sinePeak = 1.0;
        const auto flat = [](std::optional<std::pair<double, double>> e, double v) {
            return e && std::abs(e->first - v) < 1e-3 && std::abs(e->second - v) < 1e-3;
        };
        const auto sine = [&](std::optional<std::pair<double, double>> e) {
            return e && std::abs(e->second - sinePeak) < 0.02 &&
                   std::abs(e->first + sinePeak) < 0.02;
        };
        const ScopePanel* scope1 = window.scopePanel(QStringLiteral("scope1"));
        const ScopePanel* scope2 = window.scopePanel(QStringLiteral("scope2"));
        const bool dmm1 = shown("dmm1") == QStringLiteral("2.500 V");
        const bool dmm2 = shown("dmm2") == QStringLiteral("5.000 V");
        const bool ch1 = flat(extent(scope1, 0), 2.5);
        const bool ch2 = sine(extent(scope1, 1));
        const bool other = sine(extent(scope2, 0));
        const bool generator = window.generatorPanel(QStringLiteral("gen1")) != nullptr;
        if (dmm1 && dmm2 && ch1 && ch2 && other && generator) {
            std::printf("proton: drawn by hand: dmm1 %s, dmm2 %s, scope1 and scope2 on their "
                        "nets\n",
                        shown("dmm1").toStdString().c_str(), shown("dmm2").toStdString().c_str());
            std::fflush(stdout);
            timer->stop();
            window.close();
            return;
        }
        const bool late = clock->elapsed() > std::chrono::milliseconds(kExampleTimeout).count();
        if (late || window.control().state() == bench::RunControl::State::Invalid ||
            window.control().state() == bench::RunControl::State::Failed) {
            const auto range = [](std::optional<std::pair<double, double>> e) {
                if (!e) {
                    return QStringLiteral("no frame");
                }
                return QStringLiteral("%1..%2 V")
                    .arg(e->first, 0, 'f', 3)
                    .arg(e->second, 0, 'f', 3);
            };
            std::fprintf(stderr,
                         "proton: the hand-drawn bench is wrong: dmm1 '%s', dmm2 '%s', scope1 CH1 "
                         "%s CH2 %s, scope2 CH1 %s, gen1 panel %s; %s\n",
                         shown("dmm1").toStdString().c_str(), shown("dmm2").toStdString().c_str(),
                         range(extent(scope1, 0)).toStdString().c_str(),
                         range(extent(scope1, 1)).toStdString().c_str(),
                         range(extent(scope2, 0)).toStdString().c_str(),
                         generator ? "open" : "missing", window.control().error().c_str());
            timer->stop();
            QApplication::exit(1);
        }
    });
    timer->start(16);
}

} // namespace

void smokeEditor(MainWindow& window)
{
    // In the event loop, so that failing can end it, and with the parts panel laid out.
    QTimer::singleShot(0, &window, [&window] { checkEditor(window); });
}

namespace {

// The body of smokeUndo().
void checkUndo(MainWindow& window)
{
    using schematic::ComponentKind;
    using schematic::Rotation;
    using schematic::Scene;
    sheet::SchematicView& sheet = window.sheet();
    sheet.setScene(Scene{});
    auto* undo = window.findChild<QAction*>(QStringLiteral("undo"));
    auto* redo = window.findChild<QAction*>(QStringLiteral("redo"));
    if (undo == nullptr || redo == nullptr) {
        std::fprintf(stderr, "proton: no undo or redo action in the Edit menu\n");
        QApplication::exit(1);
        return;
    }
    auto edits = std::make_shared<int>(0);
    QObject::connect(&sheet, &sheet::SchematicView::edited, &window, [edits] { ++*edits; });

    // Every kind of edit, with the scene after each: states[i] is the sheet after i edits.
    std::vector<Scene> states{sheet.scene()};
    const auto record = [&] { states.push_back(sheet.scene()); };
    sheet.placePart(ComponentKind::VoltageSource, {0, 2});
    record();
    sheet.placePart(ComponentKind::Resistor, {4, 0}, Rotation::R0, "10k");
    record();
    sheet.placePart(ComponentKind::Resistor, {8, 2}, Rotation::R90);
    record();
    sheet.placePart(ComponentKind::Ground, {4, 6});
    record();
    sheet.drawWire({0, 0}, {2, 0});
    record();
    sheet.drawWire({6, 0}, {8, 4}); // two wires, one step
    record();
    sheet.addLabel("in", {1, 0});
    record();
    sheet.placeInstrument(schematic::InstrumentKind::Scope, {16, 0});
    record();
    sheet.drawWire({13, -1}, {8, -1}); // scope1's CH1
    record();
    sheet.moveItems({1, 2}, {0}, {0, -1});
    record();
    sheet.rotateItems({2}, {0});
    record();
    sheet.setPartText(1, "R7", "4.7k");
    record();
    sheet.renameLabel(0, "vin");
    record();
    sheet.removeItems({3}, {0}, {}, {0});
    record();
    sheet.clearSheet();
    record();
    // An edit that changes nothing is no step.
    sheet.moveItems({}, {}, {1, 1});

    const auto fail = [](const char* what, std::size_t step) {
        std::fprintf(stderr, "proton: undo/redo: %s at step %zu\n", what, step);
        QApplication::exit(1);
    };
    const std::size_t steps = states.size() - 1;
    if (static_cast<std::size_t>(sheet.undoStack().count()) != steps) {
        fail("not one undo step per edit", static_cast<std::size_t>(sheet.undoStack().count()));
        return;
    }
    *edits = 0;
    for (std::size_t i = steps; i-- > 0;) {
        undo->trigger();
        if (!(sheet.scene() == states[i])) {
            fail("undo did not restore the scene", i);
            return;
        }
    }
    if (undo->isEnabled() || !(sheet.scene() == Scene{})) {
        fail("undo did not end on the empty sheet", 0);
        return;
    }
    for (std::size_t i = 1; i <= steps; ++i) {
        redo->trigger();
        if (!(sheet.scene() == states[i])) {
            fail("redo did not restore the scene", i);
            return;
        }
    }
    if (redo->isEnabled() || *edits != static_cast<int>(2 * steps)) {
        fail("redo did not end on the last scene, or a step did not emit edited()", steps);
        return;
    }
    std::printf("proton: %zu edits undone to the empty sheet and redone\n", steps);
    std::fflush(stdout);
    window.close();
}

} // namespace

void smokeUndo(MainWindow& window)
{
    // In the event loop, so that failing can end it.
    QTimer::singleShot(0, &window, [&window] { checkUndo(window); });
}

namespace {

using Tool = sheet::SchematicView::Tool;

// Answers the next modal dialog with `answer` once it shows, from inside its own event loop.
void answerNextDialog(std::function<void(QDialog&)> answer)
{
    auto* timer = new QTimer(qApp);
    auto tries = std::make_shared<int>(0);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, tries, answer] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog != nullptr || ++*tries > 500) {
            timer->stop();
            timer->deleteLater();
        }
        if (dialog != nullptr) {
            answer(*dialog);
        }
    });
    timer->start(10);
}

// Triggers the window's actions as a student does: through their menus and their toolbar
// buttons, failing with what went wrong.
class Actions
{
public:
    Actions(MainWindow& window, Checker& checker) : window_(window), checker_(checker) {}

    QAction* find(const char* id)
    {
        auto* action = window_.findChild<QAction*>(QString::fromLatin1(id));
        checker_.check(action != nullptr, QStringLiteral("an action %1").arg(id));
        return action;
    }

    // Chooses `id` from its menu: it must be in one and enabled.
    bool menu(const char* id)
    {
        QAction* action = find(id);
        if (action == nullptr) {
            return false;
        }
        const QList<QObject*> owners = action->associatedObjects();
        const bool inMenu = std::ranges::any_of(
            owners, [](QObject* owner) { return qobject_cast<QMenu*>(owner) != nullptr; });
        checker_.check(inMenu, QStringLiteral("%1 is in a menu").arg(id));
        checker_.check(action->isEnabled(), QStringLiteral("%1 is enabled").arg(id));
        if (!inMenu || !action->isEnabled()) {
            return false;
        }
        action->trigger();
        return true;
    }

    // Clicks `id`'s toolbar button: it must have one, enabled, with an icon and a tooltip.
    bool toolbar(const char* id)
    {
        QAction* action = find(id);
        if (action == nullptr) {
            return false;
        }
        QToolButton* button = nullptr;
        for (QToolBar* bar : window_.findChildren<QToolBar*>()) {
            if (auto* widget = qobject_cast<QToolButton*>(bar->widgetForAction(action))) {
                button = widget;
            }
        }
        checker_.check(button != nullptr, QStringLiteral("%1 is on a toolbar").arg(id));
        if (button == nullptr) {
            return false;
        }
        checker_.check(button->isEnabled(), QStringLiteral("%1's button is enabled").arg(id));
        checker_.check(!action->icon().isNull() && !action->toolTip().isEmpty(),
                       QStringLiteral("%1 has an icon and a tooltip").arg(id));
        button->click();
        return true;
    }

    // Both ways, checking `effect` after each; `between` puts things back in between.
    void both(const char* id, const std::function<bool()>& effect,
              const std::function<void()>& between = {})
    {
        if (menu(id)) {
            checker_.check(effect(), QStringLiteral("%1 from its menu").arg(id));
        }
        if (between) {
            between();
        }
        if (toolbar(id)) {
            checker_.check(effect(), QStringLiteral("%1 from the toolbar").arg(id));
        }
    }

private:
    MainWindow& window_;
    Checker& checker_;
};

// The menus' actions that need no simulation, one by one.
void checkEditingActions(MainWindow& window, Actions& actions, Checker& checker)
{
    using schematic::Scene;
    sheet::SchematicView& sheet = window.sheet();
    QUndoStack& undo = sheet.undoStack();

    actions.menu("file-example-0");
    const Scene example = sheet.scene();
    checker.check(!example.parts.empty() && !example.instruments.empty() && !window.running(),
                  QStringLiteral("File > Examples loads the example, not running"));

    // Edit: select all, copy and paste twice, undo and redo both ways.
    actions.menu("edit-select-all");
    const schematic::Selection all = sheet.selection();
    checker.check(all.parts.size() == example.parts.size() &&
                      all.wires.size() == example.wires.size() &&
                      all.instruments.size() == example.instruments.size(),
                  QStringLiteral("Select all selects everything"));
    actions.both("edit-copy", [&] { return sheet.canPaste(); },
                 [&] { sheet.selectAll(); });
    const std::size_t parts = example.parts.size();
    actions.menu("edit-paste");
    checker.check(sheet.scene().parts.size() == 2 * parts &&
                      sheet.scene().instruments.size() == 2 * example.instruments.size(),
                  QStringLiteral("Paste from the menu adds a copy"));
    actions.toolbar("edit-paste");
    checker.check(sheet.scene().parts.size() == 3 * parts,
                  QStringLiteral("Paste from the toolbar adds another"));
    actions.menu("undo");
    checker.check(sheet.scene().parts.size() == 2 * parts, QStringLiteral("Undo from its menu"));
    actions.toolbar("undo");
    checker.check(sheet.scene() == example, QStringLiteral("Undo from the toolbar"));
    actions.menu("redo");
    checker.check(sheet.scene().parts.size() == 2 * parts, QStringLiteral("Redo from its menu"));
    actions.toolbar("redo");
    checker.check(sheet.scene().parts.size() == 3 * parts, QStringLiteral("Redo from the toolbar"));
    undo.undo();
    undo.undo();

    // Cut and delete empty the sheet; undo brings it back.
    const auto emptied = [&] { return sheet.scene() == Scene{}; };
    const auto restore = [&] {
        undo.undo();
        sheet.selectAll();
    };
    sheet.selectAll();
    actions.both("edit-cut", [&] { return emptied() && sheet.canPaste(); }, restore);
    restore();
    actions.both("edit-delete", emptied, restore);
    undo.undo();
    checker.check(sheet.scene() == example, QStringLiteral("back to the example"));

    // Rotate and mirror the first resistor.
    std::size_t resistor = 0;
    while (resistor < example.parts.size() &&
           example.parts[resistor].kind != schematic::ComponentKind::Resistor) {
        ++resistor;
    }
    schematic::Selection one;
    one.parts = {resistor};
    sheet.select(one);
    auto rotation = example.parts[resistor].placement.rotation;
    actions.both("edit-rotate", [&] {
        const bool turned = sheet.scene().parts[resistor].placement.rotation != rotation;
        rotation = sheet.scene().parts[resistor].placement.rotation;
        return turned;
    });
    bool mirrored = false;
    actions.both("edit-mirror", [&] {
        mirrored = !mirrored;
        return sheet.scene().parts[resistor].placement.mirrored == mirrored;
    });

    // Properties: a part's value, then an instrument's name, its open panel following it.
    answerNextDialog([](QDialog& dialog) {
        const QList<QLineEdit*> fields = dialog.findChildren<QLineEdit*>();
        if (fields.size() == 2) {
            fields[1]->setText(QStringLiteral("2.2k"));
        }
        dialog.accept();
    });
    actions.menu("edit-properties");
    checker.check(sheet.scene().parts[resistor].value == "2.2k",
                  QStringLiteral("Properties sets the part's value"));
    window.openPanel(QString::fromStdString(example.instruments.back().name));
    schematic::Selection instrument;
    instrument.instruments = {example.instruments.size() - 1};
    sheet.select(instrument);
    answerNextDialog([](QDialog& dialog) {
        if (auto* input = qobject_cast<QInputDialog*>(&dialog)) {
            input->setTextValue(QStringLiteral("meter9"));
        }
        dialog.accept();
    });
    actions.menu("edit-properties");
    checker.check(sheet.scene().instruments.back().name == "meter9",
                  QStringLiteral("Properties renames the instrument"));
    const std::vector<QWidget*> panels = window.panelWindows();
    checker.check(std::ranges::any_of(panels,
                                      [](QWidget* w) {
                                          return w->isVisible() &&
                                                 w->windowTitle().endsWith(
                                                     QStringLiteral("meter9"));
                                      }),
                  QStringLiteral("the renamed instrument's panel is open under its new name"));

    // View.
    double zoom = sheet.zoom();
    actions.both("view-zoom-in", [&] {
        const bool larger = sheet.zoom() > zoom;
        zoom = sheet.zoom();
        return larger;
    });
    actions.both("view-zoom-out", [&] {
        const bool smaller = sheet.zoom() < zoom;
        zoom = sheet.zoom();
        return smaller;
    });
    const auto fits = [&] {
        const std::optional<schematic::Box> box = schematic::bounds(sheet.scene());
        const QRect visible = sheet.viewport()->rect();
        return box && visible.contains(sheet.viewPoint({box->left, box->top})) &&
               visible.contains(sheet.viewPoint({box->right, box->bottom}));
    };
    actions.both("view-zoom-fit", fits, [&] {
        sheet.zoomIn();
        sheet.zoomIn();
        sheet.zoomIn();
    });
    actions.both("view-grid", [&, shown = sheet.gridShown()]() mutable {
        shown = !shown;
        return sheet.gridShown() == shown;
    });
    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("parts"));
    actions.menu("view-parts-panel");
    checker.check(dock != nullptr && dock->isHidden(), QStringLiteral("Parts panel hides it"));
    actions.menu("view-parts-panel");
    checker.check(dock != nullptr && !dock->isHidden(), QStringLiteral("Parts panel shows it"));

    // Place: every part and instrument by name, then the drawing tools.
    for (const sheet::SchematicView::Placing& what : window.parts().entries()) {
        sheet.setTool(Tool::Select);
        QAction* action = nullptr;
        for (QAction* candidate : window.findChildren<QAction*>()) {
            if (candidate->objectName().startsWith(QStringLiteral("place-")) &&
                candidate->text() == window.parts().name(what)) {
                action = candidate;
            }
        }
        checker.check(action != nullptr,
                      QStringLiteral("Place > %1").arg(window.parts().name(what)));
        if (action != nullptr && actions.menu(action->objectName().toLatin1().constData())) {
            checker.check(sheet.tool() == Tool::Place && sheet.placing() == what,
                          QStringLiteral("Place > %1 places it").arg(window.parts().name(what)));
        }
    }
    const std::pair<const char*, Tool> tools[] = {{"tool-wire", Tool::Wire},
                                                  {"tool-label", Tool::Label},
                                                  {"tool-junction", Tool::Junction},
                                                  {"tool-select", Tool::Select}};
    for (const auto& [id, tool] : tools) {
        actions.both(id, [&sheet, tool] { return sheet.tool() == tool; },
                     [&sheet] { sheet.setTool(Tool::Place); });
    }
    actions.menu("tool-junction");
    const std::size_t junctions = sheet.scene().junctions.size();
    const QPointF at = sheet.viewPoint({40, 40});
    mouse(sheet.viewport(), QEvent::MouseButtonPress, at, Qt::LeftButton);
    mouse(sheet.viewport(), QEvent::MouseButtonRelease, at, Qt::NoButton);
    checker.check(sheet.scene().junctions.size() == junctions + 1,
                  QStringLiteral("the Junction tool places a junction"));
    sheet.setTool(Tool::Select);

    // Export image, answering the file dialog with a file in the temporary directory.
    const QString base = QDir::temp().filePath(QStringLiteral("proton-smoke-export-%1.png"));
    int exports = 0;
    const auto answerSave = [&base](int n) {
        answerNextDialog([path = base.arg(n)](QDialog& dialog) {
            if (auto* files = qobject_cast<QFileDialog*>(&dialog)) {
                files->selectFile(path);
            }
            dialog.accept();
        });
    };
    answerSave(0);
    actions.both(
        "file-export-image",
        [&] {
            const QString path = base.arg(exports);
            const bool saved = !QImage(path).isNull();
            QFile::remove(path);
            ++exports;
            return saved;
        },
        [&] { answerSave(1); });

    // Options and Help.
    const auto dragMode = panel::KnobWidget::dragMode();
    actions.menu("circular-knobs");
    checker.check(panel::KnobWidget::dragMode() != dragMode, QStringLiteral("Options: knobs"));
    actions.menu("circular-knobs");
    for (const char* id : {"help-about", "help-about-qt"}) {
        answerNextDialog([](QDialog& dialog) { dialog.accept(); });
        actions.menu(id);
    }

    // File > New both ways, undone in between.
    actions.both("file-new", [&] { return sheet.scene() == Scene{}; }, [&] { undo.undo(); });
    actions.menu("file-example-0");
}

} // namespace

void smokeMenus(MainWindow& window)
{
    // Qt's own file dialog, which the check can answer; the platform's is not a widget.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QTimer::singleShot(0, &window, [&window] {
        auto checker = std::make_shared<Checker>();
        auto actions = std::make_shared<Actions>(window, *checker);
        checkEditingActions(window, *actions, *checker);

        // Simulate > Instruments, and the Window menu.
        actions->menu("window-close-all");
        const auto shown = [&window] {
            int count = 0;
            for (QWidget* panel : window.panelWindows()) {
                count += panel->isVisible() ? 1 : 0;
            }
            return count;
        };
        checker->check(shown() == 0, QStringLiteral("Close all panels closes them"));
        if (auto* instruments = window.findChild<QMenu*>(QStringLiteral("simulate-instruments"))) {
            Q_EMIT instruments->aboutToShow();
            actions->menu("simulate-instrument-scope1");
            actions->menu("simulate-instrument-gen1");
            checker->check(shown() == 2, QStringLiteral("Simulate > Instruments opens panels"));
        }
        if (auto* windows = window.findChild<QMenu*>(QStringLiteral("window"))) {
            actions->menu("window-close-all");
            Q_EMIT windows->aboutToShow();
            actions->menu("window-panel-scope1");
            checker->check(shown() == 1, QStringLiteral("Window > a panel shows it"));
        }
        window.openPanel(QStringLiteral("gen1"));
        window.openPanel(QStringLiteral("dmm1"));
        actions->menu("window-arrange");
        const QRect area(window.sheet().viewport()->mapToGlobal(QPoint(0, 0)),
                         window.sheet().viewport()->size());
        for (QWidget* panel : window.panelWindows()) {
            if (panel->isVisible()) {
                const QRect frame = panel->frameGeometry();
                // A panel taller than the sheet (a small screen, or a large scale) is moved up to
                // stay on the screen, as arrangePanels() does.
                checker->check(frame.right() <= area.right() + 1 &&
                                   (frame.top() >= area.top() - 1 ||
                                    frame.height() > area.height()),
                               QStringLiteral("Arrange puts %1 over the sheet's right side")
                                   .arg(panel->windowTitle()));
            }
        }

        // Simulate: Run, Pause, Run on, Pause, Stop, Run again, Stop; menu and toolbar.
        struct Sim
        {
            int step = 0;
            double pausedAt = 0.0;
            std::size_t restarts = 0;
            QElapsedTimer clock; // since the step began
        };
        auto sim = std::make_shared<Sim>();
        sim->clock.start();
        auto* timer = new QTimer(&window);
        QObject::connect(timer, &QTimer::timeout, &window, [&window, timer, sim, checker,
                                                            actions] {
            using State = bench::RunControl::State;
            const bench::RunControl& control = window.control();
            const auto next = [&] {
                ++sim->step;
                sim->clock.restart();
            };
            const auto failed = [&](const QString& why) {
                checker->check(false, why);
                timer->stop();
                QApplication::exit(1);
            };
            if (sim->clock.elapsed() > std::chrono::milliseconds(kExampleTimeout).count()) {
                failed(QStringLiteral("Simulate: step %1 timed out").arg(sim->step));
                return;
            }
            auto* runTime = window.findChild<QLabel*>(QStringLiteral("run-time"));
            switch (sim->step) {
            case 0:
                actions->menu("run");
                next();
                break;
            case 1: // running for a while
                if (control.state() == State::Running && control.lastTime() > 0.05) {
                    sim->restarts = control.restarts();
                    actions->menu("simulate-pause");
                    sim->pausedAt = control.lastTime();
                    checker->check(window.runState() == MainWindow::RunState::Paused &&
                                       control.state() == State::Paused,
                                   QStringLiteral("Pause from its menu"));
                    next();
                }
                break;
            case 2: // paused: time stands still
                if (control.lastTime() != sim->pausedAt || control.state() != State::Paused) {
                    failed(QStringLiteral("Pause: the run went on"));
                    return;
                }
                if (sim->clock.elapsed() > 300) {
                    checker->check(runTime != nullptr &&
                                       runTime->text().startsWith(QStringLiteral("Paused")),
                                   QStringLiteral("the run time shows Paused"));
                    actions->toolbar("run");
                    next();
                }
                break;
            case 3: // on from the pause, without a restart
                if (control.lastTime() > sim->pausedAt + 0.05) {
                    checker->check(control.restarts() == sim->restarts,
                                   QStringLiteral("Run after Pause goes on without a restart"));
                    actions->toolbar("simulate-pause");
                    checker->check(control.state() == State::Paused,
                                   QStringLiteral("Pause from the toolbar"));
                    actions->menu("simulate-stop");
                    checker->check(control.state() == State::Idle && !window.running(),
                                   QStringLiteral("Stop from its menu"));
                    actions->menu("run");
                    next();
                } else if (control.lastTime() < sim->pausedAt) {
                    failed(QStringLiteral("Run after Pause started again from t = 0"));
                    return;
                }
                break;
            case 4: // started again from t = 0
                if (control.restarts() > sim->restarts && control.state() == State::Running) {
                    actions->toolbar("simulate-stop");
                    checker->check(control.state() == State::Idle,
                                   QStringLiteral("Stop from the toolbar"));
                    next();
                }
                break;
            case 5:
                timer->stop();
                actions->menu("file-quit");
                checker->check(!window.isVisible(), QStringLiteral("Quit closes the window"));
                std::printf("proton: every menu and toolbar action checked\n");
                std::fflush(stdout);
                QApplication::exit(checker->failed() ? 1 : 0);
                break;
            }
        });
        timer->start(16);
    });
}

namespace {

// The first time at or after the trigger where channel `channel` of `frame` rises through 0 V,
// relative to the trigger, by linear interpolation between samples.
std::optional<double> risingZero(const instruments::ScopeFrame& frame, std::size_t channel)
{
    const std::vector<double>& v = frame.values[channel];
    for (std::size_t i = 1; i < v.size(); ++i) {
        const double t = frame.startTime + static_cast<double>(i) * frame.sampleInterval;
        if (t >= 0.0 && v[i - 1] < 0.0 && v[i] >= 0.0) {
            return t - frame.sampleInterval * v[i] / (v[i] - v[i - 1]);
        }
    }
    return std::nullopt;
}

} // namespace

void smokeFiles(MainWindow& window)
{
    // Qt's own file dialog, which the check can answer; the platform's is not a widget.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QTimer::singleShot(0, &window, [&window] {
        Checker checker;
        Actions actions(window, checker);
        sheet::SchematicView& sheet = window.sheet();
        const QString dir = QDir::temp().filePath(QStringLiteral("proton-smoke-files"));
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/my-filter.proton");
        const auto answerFile = [](const QString& file) {
            answerNextDialog([file](QDialog& dialog) {
                if (auto* files = qobject_cast<QFileDialog*>(&dialog)) {
                    files->selectFile(file);
                }
                dialog.accept();
            });
        };
        // What the bench would run: the drawing's circuit with the instruments' contributions.
        const auto netlist = [&window, &sheet] {
            schematic::Circuit circuit = schematic::convert(sheet.scene()).circuit;
            bench::Bench bench(window.control().bench().config());
            bench.contributeTo(circuit);
            return schematic::buildNetlist(circuit);
        };

        // The start page lists every example and loads the one clicked.
        window.showStartPage();
        checker.check(window.startPageShown(), QStringLiteral("the start page shows"));
        for (std::size_t i = 0; i < window.exampleCount(); ++i) {
            checker.check(window.findChild<QWidget*>(QStringLiteral("start-example-%1").arg(i)),
                          QStringLiteral("the start page has example %1").arg(i));
        }
        checker.check(window.exampleCount() == 6, QStringLiteral("six examples"));
        if (auto* card = window.findChild<QWidget*>(QStringLiteral("start-example-0"))) {
            const QPointF centre(card->width() / 2.0, card->height() / 2.0);
            mouse(card, QEvent::MouseButtonRelease, centre, Qt::NoButton);
        }
        checker.check(!window.startPageShown() && !sheet.scene().parts.empty() &&
                          window.windowTitle().startsWith(QStringLiteral("RC low-pass")),
                      QStringLiteral("an example card loads it: '%1'").arg(window.windowTitle()));

        // Save on an example asks where, and the title then names the file.
        answerFile(path);
        actions.toolbar("file-save");
        checker.check(QFile::exists(path) && window.projectPath() == path &&
                          window.windowTitle().startsWith(QStringLiteral("my-filter")) &&
                          !window.isWindowModified(),
                      QStringLiteral("Save on an example writes the file chosen"));

        // A part's value and an instrument's setting changed, then Save writes them, unasked.
        const auto r1 = std::ranges::find(sheet.scene().parts, std::string("R1"),
                                          &schematic::PlacedPart::designator);
        if (r1 != sheet.scene().parts.end()) {
            sheet.setPartText(static_cast<std::size_t>(r1 - sheet.scene().parts.begin()), "R1",
                              "2.2k");
        }
        checker.check(window.isWindowModified(), QStringLiteral("an edit marks the title"));
        for (panel::KnobWidget* knob : window.scope().findChildren<panel::KnobWidget*>()) {
            if (knob->accessibleName() == QStringLiteral("Time/div")) {
                wheel(knob, 1);
            }
        }
        const bench::Bench::Config settings = window.control().bench().config();
        checker.check(settings.scopes.size() == 1 && settings.scopes[0].config.timePerDiv != 200e-6,
                      QStringLiteral("the scope's time/div changed"));
        const schematic::Scene saved = sheet.scene();
        const std::string savedNetlist = netlist();
        actions.menu("file-save");
        checker.check(!window.isWindowModified(), QStringLiteral("Save clears the mark"));

        // New, then Open: the same scene, settings and netlist.
        actions.menu("file-new");
        checker.check(sheet.scene().parts.empty() && window.projectPath().isEmpty(),
                      QStringLiteral("New empties the sheet"));
        answerFile(path);
        actions.toolbar("file-open");
        checker.check(sheet.scene() == saved, QStringLiteral("Open brings back the scene"));
        checker.check(window.control().bench().config() == settings,
                      QStringLiteral("Open brings back every instrument's settings"));
        checker.check(netlist() == savedNetlist, QStringLiteral("Open gives the same netlist"));
        checker.check(window.projectPath() == path, QStringLiteral("Open names the file"));

        // Open recent, from the menu and from the start page.
        actions.menu("file-new");
        if (auto* recent = window.findChild<QMenu*>(QStringLiteral("file-recent"))) {
            Q_EMIT recent->aboutToShow();
        }
        actions.menu("file-recent-0");
        checker.check(sheet.scene() == saved, QStringLiteral("Open recent reopens the file"));
        actions.menu("file-new");
        window.showStartPage();
        if (auto* button = window.findChild<QPushButton*>(QStringLiteral("start-recent-0"))) {
            button->click();
        }
        checker.check(!window.startPageShown() && sheet.scene() == saved,
                      QStringLiteral("the start page's recent list reopens it"));

        // Save as a second file, through its menu.
        const QString copy = dir + QStringLiteral("/copy.proton");
        answerFile(copy);
        actions.menu("file-save-as");
        checker.check(QFile::exists(copy) && window.projectPath() == copy &&
                          window.recentFiles().value(0) == QFileInfo(copy).absoluteFilePath(),
                      QStringLiteral("Save as writes a second file, first among the recent"));

        // A file that is not a project is refused, with a message, and the sheet stays.
        const QString broken = dir + QStringLiteral("/broken.proton");
        if (QFile file(broken); file.open(QIODevice::WriteOnly)) {
            file.write("{");
        }
        QString message;
        answerNextDialog([&message](QDialog& dialog) {
            if (auto* box = qobject_cast<QMessageBox*>(&dialog)) {
                message = box->text();
            }
            dialog.accept();
        });
        checker.check(!window.openProject(broken) && sheet.scene() == saved &&
                          message.contains(QStringLiteral("not a Proton project")),
                      QStringLiteral("a broken file is refused: '%1'").arg(message));

        // The start page's New sheet.
        window.showStartPage();
        if (auto* button = window.findChild<QPushButton*>(QStringLiteral("start-new"))) {
            button->click();
        }
        checker.check(!window.startPageShown() && sheet.scene().parts.empty(),
                      QStringLiteral("the start page's New sheet"));

        QDir(dir).removeRecursively();
        std::printf("proton: projects saved, opened and reopened\n");
        std::fflush(stdout);
        QApplication::exit(checker.failed() ? 1 : 0);
    });
}

void smokeSelection(MainWindow& window)
{
    Checker checker;
    sheet::SchematicView& sheet = window.sheet();
    QWidget* viewport = sheet.viewport();
    const auto at = [&](Point p) { return QPointF(sheet.viewPoint(p)); };
    const auto mouseAt = [&](QEvent::Type type, Point p, Qt::MouseButton button,
                             Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = {}) {
        QMouseEvent event(type, at(p), viewport->mapToGlobal(at(p)), button, buttons, modifiers);
        QCoreApplication::sendEvent(viewport, &event);
    };
    // A box drag from `from` to `to`, in steps as a hand would.
    const auto boxDrag = [&](Point from, Point to, Qt::KeyboardModifiers modifiers = {}) {
        mouseAt(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, modifiers);
        for (int i = 1; i <= 4; ++i) {
            const Point step{from.x + (to.x - from.x) * i / 4, from.y + (to.y - from.y) * i / 4};
            mouseAt(QEvent::MouseMove, step, Qt::NoButton, Qt::LeftButton, modifiers);
        }        mouseAt(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, modifiers);
    };
    using Indices = std::vector<std::size_t>;

    // R1 with a wire on its right, scope1 beyond it, and R2 below, outside the box.
    sheet.placePart(ComponentKind::Resistor, {0, 0});       // part 0, pins (-2, 0), (2, 0)
    sheet.drawWire({2, 0}, {6, 0});                         // wire 0
    sheet.placeInstrument(InstrumentKind::Scope, {12, 0});  // instrument 0
    sheet.placePart(ComponentKind::Resistor, {0, 10});      // part 1
    sheet.drawWire({2, 10}, {30, 10});                      // wire 1, half inside the Shift box
    const schematic::Scene drawn = sheet.scene();

    boxDrag({-4, -4}, {20, 5});
    schematic::Selection selected = sheet.selection();
    checker.check(selected.parts == Indices{0} && selected.wires == Indices{0} &&
                      selected.instruments == Indices{0},
                  QStringLiteral("a box selects R1, its wire and scope1, not R2 or the long wire"));
    // Shift and a second box add R2.
    boxDrag({-4, 8}, {4, 12}, Qt::ShiftModifier);
    selected = sheet.selection();
    checker.check(selected.parts == (Indices{0, 1}) && selected.instruments == Indices{0},
                  QStringLiteral("Shift and a box add to the selection"));
    key(&sheet, Qt::Key_Delete);
    checker.check(sheet.scene().parts.empty() && sheet.scene().instruments.empty() &&
                      sheet.scene().wires == std::vector<schematic::Wire>{drawn.wires[1]},
                  QStringLiteral("Delete removes what the boxes selected"));
    sheet.undoStack().undo();
    checker.check(sheet.scene() == drawn, QStringLiteral("undo brings them back"));

    // Escape clears the selection.
    sheet.selectAll();
    key(&sheet, Qt::Key_Escape);
    checker.check(sheet.selection().empty(), QStringLiteral("Escape clears the selection"));

    // A right-click opens the menu where it is, the item under it selected first.
    QStringList shown;
    QTimer::singleShot(0, &window, [&shown] {
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            for (const QAction* action : menu->actions()) {
                shown << action->objectName();
            }
            menu->close();
        }
    });
    mouseAt(QEvent::MouseButtonPress, {0, 10}, Qt::RightButton, Qt::RightButton);
    mouseAt(QEvent::MouseButtonRelease, {0, 10}, Qt::RightButton, Qt::NoButton);
    checker.check(sheet.selection().parts == Indices{1} &&
                      shown.contains(QStringLiteral("edit-delete")),
                  QStringLiteral("a right-click on R2 selects it and shows its menu: %1")
                      .arg(shown.join(QLatin1Char(' '))));

    // Each entry of each menu: on a part, on an instrument, on a wire and on empty sheet.
    const auto entry = [&](Point p, const char* id) {
        const std::unique_ptr<QMenu> menu = sheet.contextMenu(p);
        for (QAction* action : menu->actions()) {
            if (action->objectName() == QLatin1String(id)) {
                checker.check(action->isEnabled(), QStringLiteral("%1 is enabled").arg(id));
                action->trigger();
                return;
            }
        }
        checker.check(false,
                      QStringLiteral("the menu at (%1, %2) has %3").arg(p.x).arg(p.y).arg(id));
    };
    const schematic::PlacedPart before = sheet.scene().parts[0];
    entry({0, 0}, "edit-rotate");
    checker.check(sheet.scene().parts[0].placement.rotation != before.placement.rotation,
                  QStringLiteral("Rotate on R1 turns it"));
    entry({0, 0}, "edit-mirror");
    checker.check(sheet.scene().parts[0].placement.mirrored != before.placement.mirrored,
                  QStringLiteral("Mirror on R1 mirrors it"));
    answerNextDialog([](QDialog& dialog) {
        const QList<QLineEdit*> fields = dialog.findChildren<QLineEdit*>();
        if (fields.size() == 2) {
            fields[1]->setText(QStringLiteral("4.7k"));
        }
        dialog.accept();
    });
    entry({0, 0}, "edit-properties");
    checker.check(sheet.scene().parts[0].value == "4.7k",
                  QStringLiteral("Properties on R1 sets its value"));
    entry({0, 0}, "edit-delete");
    checker.check(sheet.scene().parts.size() == 1, QStringLiteral("Delete on R1 removes it"));

    entry({12, 0}, "sheet-open-panel");
    checker.check(window.scopePanel(QStringLiteral("scope1")) != nullptr &&
                      window.scopePanel(QStringLiteral("scope1"))->isVisible(),
                  QStringLiteral("Open panel on scope1 opens its panel"));
    entry({12, 0}, "edit-rotate");
    checker.check(sheet.scene().instruments[0].placement.rotation != schematic::Rotation::R0,
                  QStringLiteral("Rotate on scope1 turns it"));
    entry({12, 0}, "edit-rotate");
    entry({12, 0}, "edit-rotate");
    entry({12, 0}, "edit-rotate"); // back upright
    entry({12, 0}, "edit-mirror");
    checker.check(sheet.scene().instruments[0].placement.mirrored,
                  QStringLiteral("Mirror on scope1 mirrors it"));
    answerNextDialog([](QDialog& dialog) {
        if (auto* input = qobject_cast<QInputDialog*>(&dialog)) {
            input->setTextValue(QStringLiteral("probe"));
        }
        dialog.accept();
    });
    entry({12, 0}, "edit-properties");
    checker.check(sheet.scene().instruments[0].name == "probe",
                  QStringLiteral("Properties on scope1 renames it"));
    entry({12, 0}, "edit-delete");
    checker.check(sheet.scene().instruments.empty(), QStringLiteral("Delete on probe removes it"));

    const std::size_t wires = sheet.scene().wires.size();
    entry({20, 10}, "edit-delete");
    checker.check(sheet.scene().wires.size() == wires - 1,
                  QStringLiteral("Delete on a wire removes it"));

    entry({40, 40}, "edit-select-all");
    checker.check(!sheet.selection().empty(), QStringLiteral("Select all on empty sheet"));
    sheet.copy();
    const std::size_t parts = sheet.scene().parts.size();
    entry({40, 40}, "edit-paste");
    checker.check(sheet.scene().parts.size() == 2 * parts,
                  QStringLiteral("Paste on empty sheet pastes the copy"));

    std::printf("proton: box selection and every right-click menu entry checked\n");
    std::fflush(stdout);
    const bool failed = checker.failed();
    QTimer::singleShot(0, &window, [failed] { QApplication::exit(failed ? 1 : 0); });
}

void smokeDiagnostics(MainWindow& window)
{
    using schematic::DiagnosticCode;
    using Mark = sheet::SchematicView::Mark;
    Checker checker;
    sheet::SchematicView& sheet = window.sheet();
    // A mark of `code`, `severity`, at `p`: what the tooltip there says includes its message.
    const auto markedAt = [&](DiagnosticCode code, schematic::Severity severity, Point p,
                              const QString& what) {
        const auto mark = std::ranges::find_if(sheet.marks(), [&](const Mark& m) {
            return m.code == code && m.severity == severity &&
                   schematic::contains(sheet.scene(), m.place, p);
        });
        checker.check(mark != sheet.marks().end() &&
                          sheet.diagnosticsAt(p).join(QLatin1Char('\n')).contains(mark->message),
                      what + QStringLiteral(" at (%1, %2)").arg(p.x).arg(p.y));
    };
    using enum schematic::Severity;

    checker.check(sheet.marks().empty() && sheet.sheetDiagnostics().isEmpty(),
                  QStringLiteral("an empty sheet has no diagnostics"));

    // R1 alone: no ground, about the whole drawing, in the bar.
    sheet.placePart(ComponentKind::Resistor, {0, 0}); // pins (-2, 0), (2, 0)
    const QLabel* bar = sheet.findChild<QLabel*>(QStringLiteral("sheet-diagnostics"));
    checker.check(bar != nullptr && bar->isVisible() &&
                      bar->text().contains(QStringLiteral("ground")),
                  QStringLiteral("no ground shows in the bar along the sheet's top"));
    checker.check(sheet.diagnosticsAt({0, 0}).isEmpty(),
                  QStringLiteral("no ground marks no part"));
    // Grounded on its left: its right pin has a single connection.
    sheet.placePart(ComponentKind::Ground, {-2, 2});
    sheet.drawWire({-2, 0}, {-2, 2});
    checker.check(bar != nullptr && !bar->isVisible(), QStringLiteral("the bar goes with ground"));
    markedAt(DiagnosticCode::SingleConnectionNet, Warning, {2, 0},
             QStringLiteral("single connection"));

    // Its value, then its designator.
    sheet.setPartText(0, "R1", "abc");
    markedAt(DiagnosticCode::InvalidValue, Error, {0, 0}, QStringLiteral("invalid value"));
    sheet.setPartText(0, "R1", "0");
    markedAt(DiagnosticCode::ZeroValue, Error, {0, 0}, QStringLiteral("zero value"));
    sheet.setPartText(0, "X1", "1k");
    markedAt(DiagnosticCode::InvalidDesignator, Error, {0, 0},
             QStringLiteral("invalid designator"));
    sheet.setPartText(0, "R1", "1k");

    // R2 below it, renamed R1: both are marked.
    sheet.placePart(ComponentKind::Resistor, {0, 6}); // index 2, after the ground
    sheet.drawWire({-2, 6}, {-2, 2});
    sheet.drawWire({2, 6}, {2, 0});
    sheet.setPartText(2, "R1", "1k");
    markedAt(DiagnosticCode::DuplicateDesignator, Error, {0, 0},
             QStringLiteral("duplicate, first"));
    markedAt(DiagnosticCode::DuplicateDesignator, Error, {0, 6},
             QStringLiteral("duplicate, second"));
    sheet.setPartText(2, "R2", "1k");

    // A label on the right net with a name SPICE cannot read, then a reserved one.
    sheet.addLabel("a/b", {2, 3});
    markedAt(DiagnosticCode::InvalidNetName, Error, {2, 3}, QStringLiteral("invalid net name"));
    markedAt(DiagnosticCode::InvalidNetName, Error, {2, 0}, QStringLiteral("invalid net name"));
    sheet.renameLabel(0, "proton_halt");
    markedAt(DiagnosticCode::ReservedName, Error, {2, 3}, QStringLiteral("reserved name"));
    sheet.renameLabel(0, "out");

    // A label on the ground net.
    sheet.addLabel("x", {-2, 1});
    markedAt(DiagnosticCode::ConflictingLabels, Error, {-2, 1}, QStringLiteral("label on ground"));
    sheet.removeItems({}, {}, {1});

    // C1 from `out` to a net of its own, which has no DC path to ground.
    sheet.placePart(ComponentKind::Capacitor, {6, 0}); // pins (4, 0), (8, 0)
    sheet.drawWire({2, 0}, {4, 0});
    sheet.drawWire({8, 0}, {10, 0});
    markedAt(DiagnosticCode::NoDcPathToGround, Error, {9, 0}, QStringLiteral("no DC path, wire"));
    markedAt(DiagnosticCode::NoDcPathToGround, Error, {8, 0}, QStringLiteral("no DC path, pin"));

    // A multimeter wired to nothing: each terminal.
    sheet.placeInstrument(InstrumentKind::Multimeter, {20, 0}); // + (19, 2), COM (21, 2)
    markedAt(DiagnosticCode::UnconnectedTerminal, Warning, {19, 2}, QStringLiteral("meter +"));
    markedAt(DiagnosticCode::UnconnectedTerminal, Warning, {21, 2}, QStringLiteral("meter COM"));

    // Hovering a mark shows its message in a tooltip (which fades in, so its text is checked, not
    // whether it is visible yet); an empty spot has none.
    QHelpEvent help(QEvent::ToolTip, sheet.viewPoint({19, 2}),
                    sheet.viewport()->mapToGlobal(sheet.viewPoint({19, 2})));
    QCoreApplication::sendEvent(sheet.viewport(), &help);
    const QString tip = QToolTip::text();
    checker.check(tip.startsWith(QStringLiteral("Warning: ")) &&
                      tip.contains(QStringLiteral("dmm1")),
                  QStringLiteral("hovering the meter's terminal shows '%1'").arg(tip));
    checker.check(sheet.diagnosticsAt({0, 12}).isEmpty(),
                  QStringLiteral("no diagnostic on an empty spot"));

    std::printf("proton: %zu diagnostics marked on the sheet\n", sheet.marks().size());
    std::fflush(stdout);
    const bool failed = checker.failed();
    QTimer::singleShot(0, &window, [failed] { QApplication::exit(failed ? 1 : 0); });
}

void smokeSession(MainWindow& window)
{
    window.showMaximized();
    // Once the window has its maximised size.
    QTimer::singleShot(500, &window, [&window] {
        auto checker = std::make_shared<Checker>();
        window.findChild<QAction*>(QStringLiteral("file-example-0"))->trigger();

        // Side by side: when the sheet has room for every panel, none covers another.
        const QWidget* viewport = window.sheet().viewport();
        const QRect sheet(viewport->mapToGlobal(QPoint(0, 0)), viewport->size());
        std::vector<QRect> panels;
        for (const QWidget* panel : window.panelWindows()) {
            if (panel->isVisible()) {
                panels.push_back(panel->frameGeometry());
            }
        }
        checker->check(panels.size() == 3, QStringLiteral("the example opens its three panels"));
        if (std::ranges::all_of(panels, [&](const QRect& r) { return sheet.contains(r); })) {
            for (std::size_t i = 0; i < panels.size(); ++i) {
                for (std::size_t j = i + 1; j < panels.size(); ++j) {
                    checker->check(!panels[i].intersects(panels[j]),
                                   QStringLiteral("panels %1 and %2 overlap").arg(i).arg(j));
                }
            }
        } else {
            std::printf("proton: the sheet (%dx%d) has no room for the panels side by side; "
                        "their layout is not checked\n",
                        sheet.width(), sheet.height());
        }

        // 1.592 kHz with the cursor and the knob, as in M4.6.
        GeneratorPanel& generator = window.generator();
        panel::KnobWidget* frequency = nullptr;
        for (panel::KnobWidget* knob : generator.findChildren<panel::KnobWidget*>()) {
            if (knob->accessibleName() == QStringLiteral("Frequency")) {
                frequency = knob;
            }
        }
        const QPoint hundreds = generator.digitPosition(GeneratorPanel::Setting::Frequency, 2);
        if (frequency != nullptr) {
            mouse(generator.display(), QEvent::MouseButtonPress, hundreds, Qt::LeftButton);
            mouse(generator.display(), QEvent::MouseButtonRelease, hundreds, Qt::NoButton);
            wheel(frequency, 5);
            key(frequency, Qt::Key_Right);
            wheel(frequency, 9);
            key(frequency, Qt::Key_Right);
            wheel(frequency, 2);
        }
        checker->check(generator.config().frequency == 1592.0,
                       QStringLiteral("the generator is set to 1.592 kHz"));

        // 50 us/div: two steps faster than the example's 200 us/div, clockwise.
        ScopePanel& scope = window.scope();
        for (panel::KnobWidget* knob : scope.findChildren<panel::KnobWidget*>()) {
            if (knob->accessibleName() == QStringLiteral("Time/div")) {
                for (int click = 0; click < 2 && scope.config().timePerDiv > 50e-6; ++click) {
                    wheel(knob, 1);
                }
            }
        }
        checker->check(scope.config().timePerDiv == 50e-6,
                       QStringLiteral("the scope is at 50 us/div, not %1 s/div")
                           .arg(scope.config().timePerDiv));

        pressRun(window);
        auto* timer = new QTimer(&window);
        auto clock = std::make_shared<QElapsedTimer>();
        clock->start();
        QObject::connect(timer, &QTimer::timeout, &window, [&window, timer, clock, checker] {
            const bool late = clock->elapsed() > std::chrono::milliseconds(kExampleTimeout).count();
            if (window.control().state() != bench::RunControl::State::Running || late) {
                checker->check(false, late ? QStringLiteral("no reading in time")
                                           : QString::fromStdString(window.control().error()));
            } else if (!window.reading() || window.reading()->time < 0.2 ||
                       !window.scope().screen().frame()) {
                return; // past the second gate, as a student waits for the reading to settle
            } else {
                // -3 dB of 1 V peak is 0.5000 V RMS, give or take the last digit.
                const MultimeterPanel& meter = window.multimeter();
                const double volts = meter.displayText().toDouble();
                checker->check(meter.displayUnit() == QStringLiteral("V") &&
                                   std::abs(volts - 0.5) <= 2e-4,
                               QStringLiteral("the meter shows %1 %2, not 0.5000 V")
                                   .arg(meter.displayText(), meter.displayUnit()));
                const instruments::ScopeFrame& frame = *window.scope().screen().frame();
                const std::optional<double> in = risingZero(frame, 0);
                const std::optional<double> out = risingZero(frame, 1);
                const double lag = in && out ? (*out - *in) * 1592.0 * 360.0 : 0.0;
                checker->check(std::abs(lag - 45.0) < 1.0,
                               QStringLiteral("CH2 lags CH1 by %1 degrees, not 45").arg(lag));
                std::printf("proton: at 1.592 kHz the meter shows %s V and CH2 lags CH1 by "
                            "%.1f degrees (%.1f us)\n",
                            meter.displayText().toStdString().c_str(), lag,
                            in && out ? (*out - *in) * 1e6 : 0.0);
                std::fflush(stdout);
            }
            timer->stop();
            QApplication::exit(checker->failed() ? 1 : 0);
        });
        timer->start(16);
    });
}

namespace {

// The body of smokeSupply().
void checkSupply(MainWindow& window)
{
    sheet::SchematicView& sheet = window.sheet();
    sheet.setScene({});
    // psu1 with CH1 across R1, 1k, standing to its right, and ground below.
    placeFromPanel(window, InstrumentKind::Supply, {0, 0}); // CH1+: (2,-3), CH1-: (2,-1)
    placeFromPanel(window, ComponentKind::Resistor, {6, -1}, 1); // R1: (6,-3), (6,1)
    placeFromPanel(window, ComponentKind::Ground, {6, 3});
    sheet.drawWire({2, -3}, {6, -3});
    sheet.drawWire({6, 1}, {6, 3});
    sheet.drawWire({2, -1}, {4, -1});
    sheet.drawWire({4, -1}, {4, 3});
    sheet.drawWire({4, 3}, {6, 3});
    sheet.setTool(sheet::SchematicView::Tool::Select);
    if (sheet.scene().instruments.size() != 1 || sheet.scene().instruments[0].name != "psu1") {
        std::fprintf(stderr, "proton: the parts panel did not place psu1\n");
        QApplication::exit(1);
        return;
    }
    mouse(sheet.viewport(), QEvent::MouseButtonDblClick, sheet.viewPoint({0, 0}), Qt::LeftButton);
    SupplyPanel* panel = window.supplyPanel(QStringLiteral("psu1"));
    if (panel == nullptr) {
        std::fprintf(stderr, "proton: psu1's panel did not open from its symbol\n");
        QApplication::exit(1);
        return;
    }
    pressRun(window);

    // Stage 0: V/R on the displays; 1: the limit typed down to 1 mA lights LIMIT; 2: Output off.
    auto* timer = new QTimer(&window);
    auto clock = std::make_shared<QElapsedTimer>();
    auto stage = std::make_shared<int>(0);
    clock->start();
    QObject::connect(timer, &QTimer::timeout, &window, [&window, panel, timer, clock, stage] {
        const QString volts = panel->voltageText(0);
        const QString amps = panel->currentText(0);
        bool done = false;
        switch (*stage) {
        case 0:
            done = volts == QStringLiteral("5.000") && amps == QStringLiteral("0.0050") &&
                   !panel->limitLit(0);
            if (done) {
                std::printf("proton: psu1 CH1 into 1k: %s V, %s A\n", volts.toStdString().c_str(),
                            amps.toStdString().c_str());
                panel->limitEntry(0)->setText(QStringLiteral("0.001"));
                key(panel->limitEntry(0), Qt::Key_Return);
            }
            break;
        case 1:
            done = amps == QStringLiteral("0.0050") && panel->limitLit(0);
            if (done) {
                std::printf("proton: psu1 CH1 beyond a 1 mA limit: LIMIT lit\n");                panel->outputButton(0)->click();
            }
            break;
        default:
            done = amps == QStringLiteral("0.0000") && !panel->limitLit(0);
            if (done) {
                std::printf("proton: psu1 CH1 output off: %s A\n", amps.toStdString().c_str());
                std::fflush(stdout);
                timer->stop();
                window.close();
                return;
            }
            break;
        }
        if (done) {
            ++*stage;
            clock->restart();
            return;
        }
        const bool late = clock->elapsed() > std::chrono::milliseconds(kExampleTimeout).count();
        if (late || window.control().state() == bench::RunControl::State::Invalid ||
            window.control().state() == bench::RunControl::State::Failed) {
            std::fprintf(stderr,
                         "proton: psu1 at stage %d shows '%s V', '%s A', LIMIT %s; %s\n", *stage,
                         volts.toStdString().c_str(), amps.toStdString().c_str(),
                         panel->limitLit(0) ? "lit" : "dark", window.control().error().c_str());
            timer->stop();
            QApplication::exit(1);
        }
    });
    timer->start(16);
}

} // namespace

void smokeSupply(MainWindow& window)
{
    QTimer::singleShot(0, &window, [&window] { checkSupply(window); });
}

namespace {

// The body of smokeAmmeter().
void checkAmmeter(MainWindow& window)
{
    sheet::SchematicView& sheet = window.sheet();
    sheet.setScene({});
    // V1 5 V, R1 1k, dmm1 in series, R2 1k down to the bottom wire with ground on it.
    placeFromPanel(window, ComponentKind::VoltageSource, {0, 2});  // V1: (0,0), (0,4)
    placeFromPanel(window, ComponentKind::Resistor, {4, 0});       // R1: (2,0), (6,0)
    placeFromPanel(window, InstrumentKind::Multimeter, {9, -3});   // +: (8,-1), COM: (10,-1)
    placeFromPanel(window, ComponentKind::Resistor, {14, 2}, 1);   // R2: (14,0), (14,4)
    placeFromPanel(window, ComponentKind::Ground, {7, 6});
    sheet.drawWire({0, 0}, {2, 0});
    sheet.drawWire({6, 0}, {8, 0});
    sheet.drawWire({8, 0}, {8, -1});
    sheet.drawWire({10, -1}, {10, 0});
    sheet.drawWire({10, 0}, {14, 0});
    sheet.drawWire({0, 4}, {0, 6});
    sheet.drawWire({0, 6}, {14, 6});
    sheet.drawWire({14, 4}, {14, 6});
    sheet.setTool(sheet::SchematicView::Tool::Select);
    mouse(sheet.viewport(), QEvent::MouseButtonDblClick, sheet.viewPoint({9, -3}), Qt::LeftButton);
    MultimeterPanel* meter = window.multimeterPanel(QStringLiteral("dmm1"));
    const auto dials = meter != nullptr ? meter->findChildren<panel::SelectorWidget*>()
                                        : QList<panel::SelectorWidget*>();
    if (dials.isEmpty()) {
        std::fprintf(stderr, "proton: dmm1's panel did not open from its symbol\n");
        QApplication::exit(1);
        return;
    }
    wheel(dials.front(), 1); // DC V to AC V
    wheel(dials.front(), 1); // to DC A
    if (meter->config().mode != instruments::Multimeter::Mode::DcCurrent) {
        std::fprintf(stderr, "proton: the dial did not reach DC A\n");
        QApplication::exit(1);
        return;
    }
    pressRun(window);

    auto* timer = new QTimer(&window);
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    QObject::connect(timer, &QTimer::timeout, &window, [&window, meter, timer, clock] {
        const QString shown = meter->displayText() + QLatin1Char(' ') + meter->displayUnit();
        if (shown == QStringLiteral("2.500 mA")) {
            std::printf("proton: dmm1 in series reads %s\n", shown.toStdString().c_str());            std::fflush(stdout);
            timer->stop();
            window.close();
            return;
        }
        const bool late = clock->elapsed() > std::chrono::milliseconds(kExampleTimeout).count();
        if (late || window.control().state() == bench::RunControl::State::Invalid ||
            window.control().state() == bench::RunControl::State::Failed) {
            std::fprintf(stderr, "proton: dmm1 in series shows '%s'; %s\n",
                         shown.toStdString().c_str(), window.control().error().c_str());
            timer->stop();
            QApplication::exit(1);
        }
    });
    timer->start(16);
}

} // namespace

void smokeAmmeter(MainWindow& window)
{
    QTimer::singleShot(0, &window, [&window] { checkAmmeter(window); });
}

namespace {

// Presses at grid point `from`, moves the mouse to `to` in a few steps and lets go there, as a
// hand drags on the sheet.
void dragOnSheet(sheet::SchematicView& sheet, Point from, Point to)
{
    QWidget* viewport = sheet.viewport();
    mouse(viewport, QEvent::MouseButtonPress, sheet.viewPoint(from), Qt::LeftButton);
    for (int i = 1; i <= 4; ++i) {
        const Point step{from.x + (to.x - from.x) * i / 4, from.y + (to.y - from.y) * i / 4};
        mouse(viewport, QEvent::MouseMove, sheet.viewPoint(step), Qt::LeftButton);
    }
    mouse(viewport, QEvent::MouseButtonRelease, sheet.viewPoint(to), Qt::NoButton);
}

bool square(const schematic::Scene& scene)
{
    return std::ranges::all_of(scene.wires, [](const schematic::Wire& wire) {
        return wire.from.x == wire.to.x || wire.from.y == wire.to.y;
    });
}

// The last wire of the chain from `start` (an instrument's terminal) to where it first meets
// something other than a bend; nothing if no wire ends at `start`.
std::optional<std::size_t> leadEnd(const schematic::Scene& scene, Point start, Point& far)
{
    std::optional<std::size_t> wire;
    Point at = start;
    for (int guard = 0; guard < 100; ++guard) {
        std::vector<std::size_t> ends;
        for (std::size_t i = 0; i < scene.wires.size(); ++i) {
            if ((scene.wires[i].from == at || scene.wires[i].to == at) && wire != i) {
                ends.push_back(i);
            }
        }
        if (ends.size() != 1 || (wire && schematic::pinAt(scene, at))) {
            break;
        }
        wire = ends[0];
        at = scene.wires[*wire].from == at ? scene.wires[*wire].to : scene.wires[*wire].from;
        if (schematic::pinAt(scene, at)) {
            break;
        }
    }
    far = at;
    return wire;
}

// The body of smokeWiring().
void checkWiring(MainWindow& window)
{
    sheet::SchematicView& sheet = window.sheet();
    sheet.setScene({});
    placeFromPanel(window, InstrumentKind::Generator, {-10, 0});  // +: (-8,-1), COM: (-8,1)
    placeFromPanel(window, ComponentKind::Resistor, {-3, -1});     // R1: (-5,-1), (-1,-1)
    placeFromPanel(window, ComponentKind::Capacitor, {3, 1}, 1);   // C1: (3,-1), (3,3)
    placeFromPanel(window, ComponentKind::Ground, {-8, 5});
    placeFromPanel(window, ComponentKind::Ground, {3, 6});
    placeFromPanel(window, InstrumentKind::Scope, {14, 0});        // CH1: (11,-1), CH2: (11,1)
    sheet.setTool(sheet::SchematicView::Tool::Select);
    // Every wire drawn from a pin with the Select tool: press on one pin, let go on the other.
    dragOnSheet(sheet, {-8, -1}, {-5, -1});
    dragOnSheet(sheet, {-1, -1}, {3, -1});
    dragOnSheet(sheet, {3, 3}, {3, 6});
    dragOnSheet(sheet, {-8, 1}, {-8, 5});
    dragOnSheet(sheet, {11, 1}, {3, -1});  // CH2 on out
    dragOnSheet(sheet, {11, -1}, {-5, -1}); // CH1 on in
    const auto fail = [](const std::string& why) {
        std::fprintf(stderr, "proton: wiring: %s\n", why.c_str());
        QApplication::exit(1);
    };
    const auto connected = [&sheet] {
        // Parts: R1, C1 and the grounds; instruments: gen1, scope1.
        const schematic::Conversion c = schematic::convert(sheet.scene());
        const auto& r1 = c.circuit.components[0].nets;
        const auto& scope = c.instruments[1].nets;
        return std::pair{scope[0] == r1[0] && !r1[0].empty(),
                         scope[1] == r1[1] && c.circuit.components[1].nets[0] == r1[1]};
    };
    if (sheet.tool() != sheet::SchematicView::Tool::Select || !square(sheet.scene()) ||
        connected() != std::pair{true, true}) {
        fail("drawing from the pins did not wire the RC low-pass and the scope");
        return;
    }
    window.openPanel(QStringLiteral("scope1"));
    pressRun(window);

    // 0: the scope shows in and out; 1: R1 dragged with its wires; 2: CH2's lead moved to in.
    auto* timer = new QTimer(&window);
    auto clock = std::make_shared<QElapsedTimer>();
    auto stage = std::make_shared<int>(0);
    clock->start();
    QObject::connect(timer, &QTimer::timeout, &window, [&window, &sheet, timer, clock, stage,
                                                        fail, connected] {
        const ScopePanel* scope = window.scopePanel(QStringLiteral("scope1"));
        const auto peak = [](std::optional<std::pair<double, double>> e, double v) {
            return e && std::abs(e->second - v) < 0.03 && std::abs(e->first + v) < 0.03;
        };
        // 1 V at 1 kHz into 1k and 100n: out is 1 / sqrt(1 + (f / fc)^2) of in.
        const double fc = 1.0 / (2.0 * std::numbers::pi * 1e3 * 100e-9);
        const double out = 1.0 / std::sqrt(1.0 + (1e3 / fc) * (1e3 / fc));
        const bool ch1 = peak(extent(scope, 0), 1.0);
        bool done = false;
        switch (*stage) {
        case 0:
            done = ch1 && peak(extent(scope, 1), out);
            if (done) {
                const schematic::Scene before = sheet.scene();
                if (schematic::wireAt(before, {-3, -1})) {
                    fail("a wire runs over R1's middle");
                    timer->stop();
                    return;
                }
                dragOnSheet(sheet, {-3, -1}, {-3, -5});
                const schematic::Scene& after = sheet.scene();
                if (after.parts[0].placement.position != Point{-3, -5} || !square(after) ||
                    !schematic::sameNets(before, after)) {
                    fail("R1 did not move with its wires square and its connections kept");
                    timer->stop();
                    return;
                }
                std::printf("proton: wiring: drawn from pins, R1 dragged with %zu wires square\n",
                            after.wires.size());
            }
            break;
        case 1:
            done = ch1 && peak(extent(scope, 1), out);
            if (done) {
                // Select CH2's lead, then drag its end from out to the generator's + (in).
                Point far;
                const std::optional<std::size_t> lead = leadEnd(sheet.scene(), {11, 1}, far);
                if (!lead || far != Point{3, -1}) {
                    fail("CH2's lead does not end on C1");
                    timer->stop();
                    return;
                }
                sheet.select({{}, {*lead}, {}, {}, {}});
                dragOnSheet(sheet, {3, -1}, {-8, -1});
                if (!square(sheet.scene()) || connected().second ||
                    schematic::convert(sheet.scene()).instruments[1].nets[1] !=
                        schematic::convert(sheet.scene()).circuit.components[0].nets[0]) {
                    fail("CH2's lead did not move from out to in");
                    timer->stop();
                    return;
                }
                std::printf("proton: wiring: CH2's lead dragged from out to in\n");
            }
            break;
        default:
            done = ch1 && peak(extent(scope, 1), 1.0);
            if (done) {
                std::printf("proton: wiring: CH2 on in reads the generator's 1 V\n");
                std::fflush(stdout);
                timer->stop();
                window.close();
                return;
            }
            break;
        }
        if (done) {
            ++*stage;
            clock->restart();
            return;
        }
        const bool late = clock->elapsed() > std::chrono::milliseconds(kExampleTimeout).count();
        if (late || window.control().state() == bench::RunControl::State::Invalid ||
            window.control().state() == bench::RunControl::State::Failed) {
            fail("stage " + std::to_string(*stage) + ": the scope does not show what it should; " +
                 window.control().error());
            timer->stop();
        }
    });
    timer->start(16);
}

} // namespace

void smokeWiring(MainWindow& window)
{
    QTimer::singleShot(0, &window, [&window] { checkWiring(window); });
}

} // namespace proton::app
