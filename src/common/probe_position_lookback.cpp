
#include "probe_position_lookback.hpp"
#include <raii/scope_guard.hpp>
#include <bsod/bsod.h>
#include "timing.h"

#ifndef UNITTESTS
    #include <module/planner.h>
#endif

namespace buddy {

void ProbePositionLookbackBase::add_sample(Sample sample) {
    const auto new_newest_sample = (newest_sample_pos + 1) % NUM_SAMPLES;

    // First invalidate the position to indicate that the record is being manipulated with
    samples[new_newest_sample].position = NAN;

    samples[new_newest_sample].time = sample.time;
    samples[new_newest_sample].position = sample.position;

    // Update the sample as the last thing to reduce the probability of the reader hitting the value
    newest_sample_pos = new_newest_sample;
}

float ProbePositionLookbackBase::get_position_at(uint32_t time_us, Sample current_sample) const {
    // store position of last sample before proceeding (new sample might be added later from interrupt)
    size_t s1_pos = newest_sample_pos;

    // get current sample so we can also interpolate between newest sample and now
    Sample s2 = current_sample;

    while (true) {
        const Sample s1 {
            .time = samples[s1_pos].time,
            .position = samples[s1_pos].position,
        };

        // If the position is NAN, it means that the sample is currently being updated (which means that it's the last sample and we can fail)
        if (std::isnan(s1.position)) {
            return NAN;
        }

        const int32_t time_diff = ticks_diff(s2.time, s1.time);

        // s1 should be older than s2, if that is not the case, we wrapped through whole buffer.
        if (time_diff < 0) {
            return NAN;
        }

        // check if searched time is between s1 & s2, but in a way that is fine with timer overflow
        // s1.time s1.time <= time_us && time_us <= s2->time
        if (static_cast<uint32_t>(time_diff) >= (s2.time - time_us)) {
            float time_coef = (time_us - s1.time) / (float)time_diff;
            return s1.position + ((s2.position - s1.position) * time_coef);
        }

        s2 = s1;
        s1_pos = (s1_pos + NUM_SAMPLES - 1) % NUM_SAMPLES;

        // we reached newest sample again - stop
        if (s1_pos == newest_sample_pos) {
            return NAN;
        }
    }
}

#ifndef UNITTESTS
void ProbePositionLookback::update() {
    // Check that we are in an ISR
    assert(__get_IPSR());

    if (is_reading) {
        // This should never happen, update() should be called from an ISR of lower priority
        bsod_unreachable();
    }

    const Sample sample = generate_sample();

    if (sample.time - samples[newest_sample_pos].time < SAMPLES_REQUESTED_DIFF) {
        // last sample still fresh enough - skip for now
        return;
    }

    add_sample(sample);
}

float ProbePositionLookback::get_position_at(uint32_t time_us) const {
    // Check that we are in an ISR
    assert(__get_IPSR());

    is_reading++;
    ScopeGuard _sg = [&] {
        is_reading--;
    };

    return ProbePositionLookbackBase::get_position_at(time_us, generate_sample());
}

ProbePositionLookback::Sample ProbePositionLookback::generate_sample() {
    return Sample {
        .time = ticks_us(),
        .position = planner.get_axis_position_mm(AxisEnum::Z_AXIS),
    };
}

ProbePositionLookback probePositionLookback;
#endif

} // namespace buddy
