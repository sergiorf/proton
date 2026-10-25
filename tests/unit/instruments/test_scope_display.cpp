// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/scope_display.h"
#include "proton/test/test.h"

#include <cstddef>
#include <vector>

using proton::instruments::decimate;
using proton::instruments::Scope;
using proton::instruments::ScopeFrame;
using proton::instruments::TraceColumn;

TEST(volts_map_to_divisions_from_the_offset)
{
    Scope::ChannelConfig channel;
    channel.voltsPerDiv = 0.5;
    channel.offset = 1.0;
    CHECK_EQ(proton::instruments::toDivisions(1.0, channel), 0.0);
    CHECK_EQ(proton::instruments::toDivisions(3.0, channel), 4.0); // top edge
    CHECK_EQ(proton::instruments::toDivisions(-1.0, channel), -4.0);
}

TEST(samples_map_to_divisions_from_the_trigger)
{
    ScopeFrame frame;
    frame.startTime = -1e-3;
    frame.sampleInterval = 2e-6;
    CHECK_NEAR(proton::instruments::sampleX(frame, 0, 200e-6), -5.0, 1e-12);
    CHECK_NEAR(proton::instruments::sampleX(frame, 500, 200e-6), 0.0, 1e-12);
    CHECK_NEAR(proton::instruments::sampleX(frame, 1000, 200e-6), 5.0, 1e-12);
}

TEST(the_trigger_marker_sits_at_the_level_on_its_source_channel)
{
    Scope::Config config;
    config.channels[0].voltsPerDiv = 1.0;
    config.channels[1].voltsPerDiv = 0.2;
    config.channels[1].offset = 0.5;
    config.trigger.level = 0.9;
    CHECK_NEAR(proton::instruments::triggerLevelY(config), 0.9, 1e-12);
    config.trigger.source = Scope::TriggerSource::Channel2;
    CHECK_NEAR(proton::instruments::triggerLevelY(config), 2.0, 1e-12);
}

TEST(decimation_keeps_every_peak)
{
    std::vector<double> values(1000, 0.0);
    values[123] = 3.0;  // a one-sample spike
    values[777] = -2.0; // and a dip
    const std::vector<TraceColumn> columns = decimate(values, 100);
    CHECK_EQ(columns.size(), 100u);
    CHECK_EQ(columns[12].max, 3.0);
    CHECK_EQ(columns[77].min, -2.0);
    for (std::size_t c = 0; c < columns.size(); ++c) {
        CHECK(columns[c].min <= columns[c].max);
    }
}

TEST(decimated_columns_join_up)
{
    // A ramp: each column starts where the one before ended, so the trace has no gaps.
    std::vector<double> values;
    for (int i = 0; i < 10; ++i) {
        values.push_back(static_cast<double>(i));
    }
    const std::vector<TraceColumn> columns = decimate(values, 5);
    CHECK(columns == (std::vector<TraceColumn>{{0, 1}, {1, 3}, {3, 5}, {5, 7}, {7, 9}}));
}

TEST(more_columns_than_samples_repeat_the_last_sample)
{
    const std::vector<double> values = {1.0, 2.0};
    const std::vector<TraceColumn> columns = decimate(values, 4);
    CHECK(columns == (std::vector<TraceColumn>{{1, 1}, {1, 1}, {1, 1}, {1, 2}}));
}

TEST(nothing_to_decimate_gives_no_columns)
{
    CHECK(decimate({}, 10).empty());
    const std::vector<double> values = {1.0};
    CHECK(decimate(values, 0).empty());
}
