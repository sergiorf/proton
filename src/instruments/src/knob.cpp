// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/knob.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace proton::instruments {

namespace {

double speedFactor(Knob::Speed speed)
{
    switch (speed) {
    case Knob::Speed::Normal:
        return 1.0;
    case Knob::Speed::Fine:
        return Knob::kFineFactor;
    case Knob::Speed::Coarse:
        return Knob::kCoarseFactor;
    }
    return 1.0;
}

// Degrees clockwise from straight up of `at` seen from `centre`, on a screen whose y goes down.
double bearing(Knob::Point centre, Knob::Point at)
{
    return std::atan2(at.x - centre.x, centre.y - at.y) * 180.0 / std::numbers::pi;
}

double normalised(double degrees)
{
    const double wrapped = std::fmod(degrees, 360.0);
    return wrapped < 0.0 ? wrapped + 360.0 : wrapped;
}

} // namespace

Knob Knob::stepped(std::vector<double> steps, double initial)
{
    Knob knob;
    knob.stepped_ = true;
    knob.steps_ = std::move(steps);
    if (!knob.steps_.empty()) {
        knob.min_ = *std::ranges::min_element(knob.steps_);
        knob.max_ = *std::ranges::max_element(knob.steps_);
    }
    knob.setValue(initial);
    return knob;
}

Knob Knob::continuous(double min, double max, double step, double initial, double reset)
{
    Knob knob;
    knob.min_ = std::min(min, max);
    knob.max_ = std::max(min, max);
    knob.setStep(step);
    knob.reset_ = reset;
    knob.setValue(initial);
    return knob;
}

double Knob::value() const
{
    if (stepped_ && !variable_) {
        return steps_.empty() ? 0.0 : steps_[index_];
    }
    return value_;
}

std::size_t Knob::nearestStep(double value) const
{
    // Nearest in log scale for positive steps, as the 1-2-5 sequence is; linear otherwise.
    const auto distance = [value](double s) {
        return s > 0.0 && value > 0.0 ? std::fabs(std::log(s / value)) : std::fabs(s - value);
    };
    std::size_t nearest = 0;
    for (std::size_t i = 0; i < steps_.size(); ++i) {
        if (distance(steps_[i]) < distance(steps_[nearest])) {
            nearest = i;
        }
    }
    return nearest;
}

void Knob::setValue(double value)
{
    if (!std::isfinite(value)) {
        return;
    }
    if (!stepped_ || variable_) {
        value_ = std::clamp(value, min_, max_);
        return;
    }
    index_ = nearestStep(value);
}

void Knob::setStep(double step)
{
    if (std::isfinite(step) && step > 0.0) {
        step_ = step;
    }
}

void Knob::setVariableAllowed(bool allowed)
{
    variableAllowed_ = allowed && stepped_ && !steps_.empty() && min_ > 0.0;
    if (!variableAllowed_ && variable_) {
        variable_ = false;
        index_ = nearestStep(value_);
    }
}

bool Knob::turn(int clicks, Speed speed)
{
    return apply(static_cast<double>(clicks), speed);
}

bool Knob::turnAccelerated(int clicks, double secondsSinceLastClick, Speed speed)
{
    double factor = 1.0;
    if (!stepped_ && speed == Speed::Normal && secondsSinceLastClick >= 0.0) {
        if (secondsSinceLastClick < kFastInterval) {
            factor = kFastFactor;
        } else if (secondsSinceLastClick < kQuickInterval) {
            factor = kQuickFactor;
        }
    }
    return apply(static_cast<double>(clicks) * factor, speed);
}

bool Knob::drag(double pixels, Speed speed)
{
    return std::isfinite(pixels) && accumulate(pixels / kPixelsPerClick, speed);
}

bool Knob::rotate(double degrees, Speed speed)
{
    return std::isfinite(degrees) && accumulate(degrees / kDegreesPerClick, speed);
}

bool Knob::dragBetween(Point from, Point to, Point centre, DragMode mode, Speed speed)
{
    const auto away = [&](Point p) { return std::hypot(p.x - centre.x, p.y - centre.y); };
    if (mode == DragMode::Circular && away(from) >= kDeadZone && away(to) >= kDeadZone) {
        double swept = bearing(centre, to) - bearing(centre, from);
        if (swept > 180.0) { // the short way round, through the bottom
            swept -= 360.0;
        } else if (swept <= -180.0) {
            swept += 360.0;
        }
        return rotate(swept, speed);
    }
    return drag((to.x - from.x) + (from.y - to.y), speed);
}

bool Knob::accumulate(double clicks, Speed speed)
{
    dragRemainder_ += clicks;
    // Whole clicks, counting one a hair short of whole (36 turns of 10 degrees add up to just
    // under 20) as whole.
    const double whole = std::trunc(dragRemainder_ + std::copysign(1e-9, dragRemainder_));
    dragRemainder_ -= whole;
    return whole != 0.0 && apply(whole, speed);
}

bool Knob::push()
{
    if (!stepped_) {
        const double before = value_;
        value_ = std::clamp(reset_, min_, max_);
        return value_ != before;
    }
    if (!variableAllowed_) {
        return false;
    }
    if (!variable_) {
        variable_ = true;
        value_ = steps_[index_];
        return false;
    }
    variable_ = false;
    index_ = nearestStep(value_);
    return steps_[index_] != value_;
}

double Knob::scalePosition() const
{
    if (!variable_ || steps_.size() < 2) {
        return static_cast<double>(index_);
    }
    // Between the two steps around the value, in log scale.
    for (std::size_t i = 0; i + 1 < steps_.size(); ++i) {
        const double a = steps_[i];
        const double b = steps_[i + 1];
        if (value_ >= std::min(a, b) && value_ <= std::max(a, b)) {
            return static_cast<double>(i) + std::log(value_ / a) / std::log(b / a);
        }
    }
    return static_cast<double>(nearestStep(value_));
}

double Knob::markAngle(std::size_t index) const
{
    if (steps_.size() < 2) {
        return 0.0;
    }
    const double fraction = static_cast<double>(index) / static_cast<double>(steps_.size() - 1);
    return normalised(-kScaleArc / 2.0 + kScaleArc * fraction);
}

double Knob::angle() const
{
    if (stepped_) {
        if (steps_.size() < 2) {
            return 0.0;
        }
        const double fraction = scalePosition() / static_cast<double>(steps_.size() - 1);
        return normalised(-kScaleArc / 2.0 + kScaleArc * fraction);
    }
    return normalised(turned_ * kDegreesPerClick);
}

bool Knob::apply(double clicks, Speed speed)
{
    if (clicks == 0.0) {
        return false;
    }
    if (stepped_ && !variable_) {
        if (steps_.empty()) {
            return false;
        }
        const double last = static_cast<double>(steps_.size() - 1);
        const double target = std::clamp(static_cast<double>(index_) + clicks, 0.0, last);
        if (target == static_cast<double>(index_)) {
            return false;
        }
        index_ = static_cast<std::size_t>(target);
        return true;
    }
    const double before = value_;
    const double amount = clicks * speedFactor(speed);
    if (stepped_) {
        // Variable: by kVariableRatio a click, the way the steps go clockwise.
        const double direction = steps_.size() > 1 && steps_.back() < steps_.front() ? -1.0 : 1.0;
        value_ = std::clamp(value_ * std::pow(kVariableRatio, direction * amount), min_, max_);
        return value_ != before;
    }
    value_ = std::clamp(value_ + amount * step_, min_, max_);
    if (value_ == before) {
        return false;
    }
    turned_ += (value_ - before) / step_;
    return true;
}

} // namespace proton::instruments
