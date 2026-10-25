// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/knob.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace proton::instruments {

Knob Knob::stepped(std::vector<double> steps, double initial)
{
    Knob knob;
    knob.stepped_ = true;
    knob.steps_ = std::move(steps);
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
    if (stepped_) {
        return steps_.empty() ? 0.0 : steps_[index_];
    }
    return value_;
}

void Knob::setValue(double value)
{
    if (!std::isfinite(value)) {
        return;
    }
    if (!stepped_) {
        value_ = std::clamp(value, min_, max_);
        return;
    }
    // Nearest in log scale for positive steps, as the 1-2-5 sequence is; linear otherwise.
    const auto distance = [value](double s) {
        return s > 0.0 && value > 0.0 ? std::fabs(std::log(s / value)) : std::fabs(s - value);
    };
    for (std::size_t i = 0; i < steps_.size(); ++i) {
        if (distance(steps_[i]) < distance(steps_[index_])) {
            index_ = i;
        }
    }
}

void Knob::setStep(double step)
{
    if (std::isfinite(step) && step > 0.0) {
        step_ = step;
    }
}

bool Knob::turn(int clicks)
{
    return apply(static_cast<double>(clicks));
}

bool Knob::turnAccelerated(int clicks, double secondsSinceLastClick)
{
    double factor = 1.0;
    if (!stepped_ && secondsSinceLastClick >= 0.0) {
        if (secondsSinceLastClick < kFastInterval) {
            factor = kFastFactor;
        } else if (secondsSinceLastClick < kQuickInterval) {
            factor = kQuickFactor;
        }
    }
    return apply(static_cast<double>(clicks) * factor);
}

bool Knob::drag(double pixels)
{
    if (!std::isfinite(pixels)) {
        return false;
    }
    dragRemainder_ += pixels / kPixelsPerClick;
    const double clicks = std::trunc(dragRemainder_);
    dragRemainder_ -= clicks;
    return clicks != 0.0 && apply(clicks);
}

bool Knob::push()
{
    if (stepped_) {
        return false;
    }
    const double before = value_;
    value_ = std::clamp(reset_, min_, max_);
    return value_ != before;
}

double Knob::angle() const
{
    const double degrees = std::fmod(turned_ * kDegreesPerClick, 360.0);
    return degrees < 0.0 ? degrees + 360.0 : degrees;
}

bool Knob::apply(double clicks)
{
    if (clicks == 0.0) {
        return false;
    }
    if (stepped_) {
        if (steps_.empty()) {
            return false;
        }
        const double last = static_cast<double>(steps_.size() - 1);
        const double target = std::clamp(static_cast<double>(index_) + clicks, 0.0, last);
        const double moved = target - static_cast<double>(index_);
        if (moved == 0.0) {
            return false;
        }
        index_ = static_cast<std::size_t>(target);
        turned_ += moved;
        return true;
    }
    const double before = value_;
    value_ = std::clamp(value_ + clicks * step_, min_, max_);
    if (value_ == before) {
        return false;
    }
    turned_ += (value_ - before) / step_;
    return true;
}

} // namespace proton::instruments
