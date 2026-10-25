// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/instruments/scope.h"

#include <QWidget>

#include <optional>

namespace proton::app {

// The oscilloscope's screen, drawn with QPainter (ADR 0004): a graticule of 10 x 8 divisions, the
// two traces, the trigger level marker on the right edge, the trigger position marker on the top
// edge, and each channel's ground marker on the left edge. The geometry comes from
// instruments/scope_display.h; this widget only scales it to pixels.
//
// A trace is drawn sample by sample when the screen has a pixel per sample or more, otherwise as
// min/max columns, one per pixel, so a frame costs the same whatever the record length.
class ScopeWidget : public QWidget {
    Q_OBJECT

public:
    explicit ScopeWidget(QWidget* parent = nullptr);

    // The settings the frame was taken with, for scaling and markers.
    void setConfig(const instruments::Scope::Config& config);
    // The frame to show, until the next one.
    void setFrame(instruments::ScopeFrame frame);
    // Back to an empty screen.
    void clear();

    // Paint times since the widget was made, ms: how many, their mean and the longest.
    long long paints() const { return paints_; }
    double meanPaintMs() const { return paints_ > 0 ? paintTotalMs_ / paints_ : 0.0; }
    double maxPaintMs() const { return paintMaxMs_; }

    QSize sizeHint() const override { return {800, 640}; }
    QSize minimumSizeHint() const override { return {320, 256}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void paint();

    instruments::Scope::Config config_;
    std::optional<instruments::ScopeFrame> frame_;
    long long paints_ = 0;
    double paintTotalMs_ = 0.0;
    double paintMaxMs_ = 0.0;
};

} // namespace proton::app
