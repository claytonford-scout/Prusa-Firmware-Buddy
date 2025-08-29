#pragma once

#include <stdint.h>
#include <stddef.h>
#include <atomic>
#include <cmath>
#include <array>

namespace buddy {

class ProbePositionLookbackBase {

public:
    struct Sample {
        uint32_t time = 0;
        float position = NAN;
    };

    static constexpr uint8_t NUM_SAMPLES = 16;

protected:
    /// Called from an ISR of HIGHER priority than update
    float get_position_at(uint32_t time_us, Sample current_sample) const;

    /// Called from an ISR
    void add_sample(Sample sample);

protected:
    struct AtomicSample {
        std::atomic<uint32_t> time = 0;
        std::atomic<float> position = NAN;
    };
    std::array<AtomicSample, NUM_SAMPLES> samples;
    std::atomic<uint8_t> newest_sample_pos = 0;
};

#ifndef UNITTESTS
class ProbePositionLookback : public ProbePositionLookbackBase {
public:
    /// Minimum time between samples (in us)
    static constexpr size_t SAMPLES_REQUESTED_DIFF = 1900;

    /// Called from an ISR of HIGHER priority than update
    float get_position_at(uint32_t time_us) const;

    /// Called from an ISR
    void update();

private:
    static Sample generate_sample();

    mutable std::atomic<uint8_t> is_reading = 0;
};

extern ProbePositionLookback probePositionLookback;
#endif

} // namespace buddy
