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

#include "proton/core/spice_number.h"
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

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cmath>
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
    // A knob sets a round number, so each entry field shows the whole setting, short.
    const auto entriesShowTheSettings = [&](const QString& after) {
        const Generator::Config& config = generator.config();
        for (QLineEdit* field : generator.findChildren<QLineEdit*>()) {
            const QString name = field->accessibleName();
            const double value = name == QStringLiteral("Frequency entry") ? config.frequency
                                 : name == QStringLiteral("Amplitude entry") ? config.amplitude
                                                                              : config.offset;
            checker.check(core::parseSpiceNumber(field->text().toStdString()) == value &&
                              field->text().size() <= 6,
                          name + QStringLiteral(" shows ") + field->text() + QStringLiteral(" after ") +
                              after);
        }
    };
    for (panel::KnobWidget* knob : generator.findChildren<panel::KnobWidget*>()) {
        const Generator::Config before = generator.config();
        for (int click = 0; click < 3; ++click) {
            wheel(knob, 1);
        }
        checker.check(generator.config() != before, knob->accessibleName() + QStringLiteral(": wheel"));
        entriesShowTheSettings(knob->accessibleName());
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

// Clicks `what`'s icon in the parts panel, on its tab, turns it `turns` quarter turns with R, and clicks the sheet at
// `at`, as a student places a part.
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
            return meter == nullptr ? QString()
                                    : meter->displayText() + QLatin1Char(' ') + meter->displayUnit();
        };
        // Ideal measurements: the frame holds the probed voltages exactly, give or take the
        // generator's 50 ohm into the 1k load.
        const double sinePeak = 1000.0 / 1050.0;
        const auto flat = [](std::optional<std::pair<double, double>> e, double v) {
            return e && std::abs(e->first - v) < 1e-3 && std::abs(e->second - v) < 1e-3;
        };
        const auto sine = [&](std::optional<std::pair<double, double>> e) {
            return e && std::abs(e->second - sinePeak) < 0.02 && std::abs(e->first + sinePeak) < 0.02;
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
            std::printf("proton: drawn by hand: dmm1 %s, dmm2 %s, scope1 and scope2 on their nets\n",
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
                return e ? QStringLiteral("%1..%2 V").arg(e->first, 0, 'f', 3).arg(e->second, 0, 'f', 3)
                         : QStringLiteral("no frame");
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

} // namespace proton::app
