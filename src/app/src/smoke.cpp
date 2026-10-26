// SPDX-License-Identifier: AGPL-3.0-only
#include "smoke.h"

#include "generator_panel.h"
#include "main_window.h"
#include "multimeter_panel.h"
#include "panel/knob_widget.h"
#include "panel/lit_button.h"
#include "panel/selector_widget.h"
#include "scope_panel.h"
#include "schematic/schematic_view.h"
#include "scope_widget.h"

#include "proton/instruments/knob.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLineEdit>
#include <QList>
#include <QMouseEvent>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QUndoStack>
#include <QWheelEvent>

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace proton::app {

namespace {

using instruments::Scope;

constexpr std::chrono::seconds kExampleTimeout{30};      // generous: CI runners are slow
constexpr std::chrono::milliseconds kExampleDwell{1500}; // over a second for the frame rate
constexpr std::chrono::milliseconds kLatencyBound{2000};  // as the latency test's loose bound

void wheel(QWidget* widget, int notches)
{
    const QPointF at(widget->width() / 2.0, widget->height() / 4.0);
    QWheelEvent event(at, widget->mapToGlobal(at), QPoint(), QPoint(0, 120 * notches),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
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
    ScopePanel& panel = window.scope();
    Checker checker;

    // Every knob turns by wheel, arrow key and drag, and each turn changes the settings.
    const QList<panel::KnobWidget*> knobs = panel.findChildren<panel::KnobWidget*>();
    checker.check(knobs.size() == 6, QStringLiteral("six knobs, found %1").arg(knobs.size()));
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
        }
    }

    // Every setting button changes the settings; Run/Stop and Single change the state.
    for (panel::LitButton* button : panel.findChildren<panel::LitButton*>()) {
        const QString name = button->accessibleName();
        if (name == QStringLiteral("Run / Stop") || name == QStringLiteral("Single")) {
            continue;
        }
        Scope::Config before = panel.config();
        // CH1 as trigger source and Auto are already chosen: choose the other first.
        if (name == QStringLiteral("CH1") || name == QStringLiteral("Auto")) {
            for (panel::LitButton* other : panel.findChildren<panel::LitButton*>()) {
                if (other->accessibleName() ==
                    (name == QStringLiteral("CH1") ? QStringLiteral("CH2")
                                                   : QStringLiteral("Normal"))) {
                    other->click();
                }
            }
            before = panel.config();
        }
        button->click();
        checker.check(panel.config() != before, name + QStringLiteral(": click"));
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

    // The generator: each knob turns by wheel, each waveform button selects its waveform, a typed
    // value applies on Enter, and Output switches the output.
    GeneratorPanel& generator = window.generator();
    using Generator = instruments::FunctionGenerator;
    for (panel::KnobWidget* knob : generator.findChildren<panel::KnobWidget*>()) {
        const Generator::Config before = generator.config();
        wheel(knob, 1);
        checker.check(generator.config() != before, knob->accessibleName() + QStringLiteral(": wheel"));
    }
    for (panel::LitButton* button : generator.findChildren<panel::LitButton*>()) {
        const QString name = button->accessibleName();
        const Generator::Config before = generator.config();
        button->click();
        if (name == QStringLiteral("Output")) {
            checker.check(generator.config().output != before.output, name + QStringLiteral(": click"));
            button->click(); // back on
        } else {
            checker.check(button->lit(), name + QStringLiteral(": lit when chosen"));
        }
    }
    // Back to a sine, so the frequency field is enabled.
    for (panel::LitButton* button : generator.findChildren<panel::LitButton*>()) {
        if (button->accessibleName() == QStringLiteral("Sine")) {
            button->click();
        }
    }
    for (QLineEdit* field : generator.findChildren<QLineEdit*>()) {
        if (field->accessibleName() == QStringLiteral("Frequency entry")) {
            field->setText(QStringLiteral("2.2k"));
            key(field, Qt::Key_Return);
            checker.check(generator.config().frequency == 2.2e3,
                          QStringLiteral("Frequency entry: 2.2k"));
        }
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

void smokeEditor(MainWindow& window)
{
    using schematic::ComponentKind;
    using schematic::Rotation;
    window.loadExample(1); // the divider's bench: dmm1 on `out`, DC V
    sheet::SchematicView& sheet = window.sheet();
    sheet.clearSheet();
    sheet.placePart(ComponentKind::VoltageSource, {0, 2});                 // V1: (0,0), (0,4)
    sheet.placePart(ComponentKind::Resistor, {4, 0}, Rotation::R0, "10k");  // R1: (2,0), (6,0)
    sheet.placePart(ComponentKind::Resistor, {8, 2}, Rotation::R90, "10k"); // R2: (8,0), (8,4)
    sheet.placePart(ComponentKind::Ground, {4, 6});
    sheet.drawWire({0, 0}, {2, 0});
    sheet.drawWire({6, 0}, {8, 0});
    sheet.drawWire({0, 4}, {0, 6});
    sheet.drawWire({0, 6}, {8, 6});
    sheet.drawWire({8, 4}, {8, 6});
    sheet.addLabel("in", {1, 0});
    sheet.addLabel("out", {7, 0});

    auto* timer = new QTimer(&window);
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    QObject::connect(timer, &QTimer::timeout, &window, [&window, timer, clock] {
        const std::optional<instruments::DmmReading>& reading = window.reading();
        const QString shown = window.multimeter().displayText() + QLatin1Char(' ') +
                              window.multimeter().displayUnit();
        if (reading && shown == QStringLiteral("2.500 V")) {
            std::printf("proton: the hand-drawn divider reads %s\n", shown.toStdString().c_str());
            std::fflush(stdout);
            timer->stop();
            window.close();
            return;
        }
        const bool late = clock->elapsed() > std::chrono::milliseconds(kExampleTimeout).count();
        if (late || window.control().state() == bench::RunControl::State::Invalid ||
            window.control().state() == bench::RunControl::State::Failed) {
            std::fprintf(stderr, "proton: the hand-drawn divider did not read 2.500 V: %s %s\n",
                         shown.toStdString().c_str(), window.control().error().c_str());
            timer->stop();
            QApplication::exit(1);
        }
    });
    timer->start(16);
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
    sheet.moveParts({1, 2}, {0, -1});
    record();
    sheet.rotateParts({2});
    record();
    sheet.setPartText(1, "R7", "4.7k");
    record();
    sheet.renameLabel(0, "vin");
    record();
    sheet.removeItems({3}, {0}, {});
    record();
    sheet.clearSheet();
    record();
    // An edit that changes nothing is no step.
    sheet.moveParts({}, {1, 1});

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

} // namespace proton::app
