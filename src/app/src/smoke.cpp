// SPDX-License-Identifier: AGPL-3.0-only
#include "smoke.h"

#include "main_window.h"
#include "multimeter_panel.h"
#include "panel/knob_widget.h"
#include "panel/lit_button.h"
#include "panel/selector_widget.h"
#include "scope_panel.h"
#include "scope_widget.h"

#include "proton/instruments/knob.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QList>
#include <QMouseEvent>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QWheelEvent>

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

namespace proton::app {

namespace {

using instruments::Scope;

constexpr std::chrono::seconds kExampleTimeout{30};      // generous: CI runners are slow
constexpr std::chrono::milliseconds kExampleDwell{1500}; // over a second for the frame rate

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

    const bool failed = checker.failed();
    QTimer::singleShot(0, &window, [failed] { QApplication::exit(failed ? 1 : 0); });
}

} // namespace proton::app
