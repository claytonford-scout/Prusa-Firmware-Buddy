#include <leds/side_strip_control.hpp>
#include <timing.h>
#include <mutex>
#include "marlin_server.hpp"

using namespace leds;

SideStripControl leds::side_strip_control;

SideStripControl::SideStripControl() {
    load_config();
}

void SideStripControl::ActivityPing() {
    std::unique_lock lock(mutex);
    active_start_timestamp.emplace(ticks_ms());
}

void SideStripControl::PresentColor(ColorRGBW color, uint32_t duration_ms, uint32_t transition_ms) {
    std::unique_lock lock(mutex);
    custom_color.emplace(CustomColorState { color, transition_ms, duration_ms, ticks_ms() });
}

void SideStripControl::TransitionToColor(ColorRGBW color, uint32_t transition_ms) {
    current_transition.emplace(side_strip.GetColor(0), color, transition_ms);
}

void SideStripControl::Tick() {
    std::unique_lock lock(mutex);

    const bool dont_dim = dimming_enabled == DimmingEnabled::never || (dimming_enabled == DimmingEnabled::not_printing && marlin_server::is_printing());

    bool active = false;
    if (dont_dim) {
        active = true;

    } else if (active_start_timestamp.has_value()) {
        if (ticks_diff(ticks_ms(), active_start_timestamp.value()) > active_timeout_ms) {
            active_start_timestamp.reset();
        } else {
            active = true;
        }
    }

    auto set_idle = [&](int transition_ms = 5000) {
        if (SideStrip::HasWhiteLed()) {
            TransitionToColor(ColorRGBW(0, 0, 0, dimmed_brightness_), transition_ms);
        } else {
            TransitionToColor(ColorRGBW(dimmed_brightness_, dimmed_brightness_, dimmed_brightness_, dimmed_brightness_), transition_ms);
        }
        state = State::Idle;
    };

    auto set_active = [&](int transition_ms = 500) {
        if (SideStrip::HasWhiteLed()) {
            TransitionToColor(ColorRGBW(0, 0, 0, max_brightness_), transition_ms);
        } else {
            TransitionToColor(ColorRGBW(max_brightness_, max_brightness_, max_brightness_, max_brightness_), transition_ms);
        }
        state = State::Active;
    };

    auto set_custom = [&]() {
        TransitionToColor(custom_color->color, custom_color->transition_duration);
        state = State::CustomColor;
    };

    switch (state) {
    case State::Startup: {
        if (custom_color.has_value()) {
            set_custom();
        } else if (active) {
            set_active();
        } else {
            set_idle();
        }
        break;
    }
    case State::Idle: {
        if (custom_color.has_value()) {
            set_custom();
        } else if (active) {
            set_active();
        }
        break;
    }
    case State::Active: {
        if (custom_color.has_value()) {
            set_custom();
        } else if (!active) {
            set_idle();
        }
        break;
    }
    case State::CustomColor: {
#if SIDE_STRIP_ENDLESS_CUSTOM_COLOR()
        if (custom_color->state_duration) {
#endif
            if (ticks_diff(ticks_ms(), custom_color->start_timestamp) > (int32_t)custom_color->state_duration) {
                uint32_t transition_ms = custom_color->transition_duration;
                custom_color.reset();
                if (active) {
                    set_active(transition_ms);
                } else {
                    set_idle(transition_ms);
                }
            }
#if SIDE_STRIP_ENDLESS_CUSTOM_COLOR()
        } else if (!current_transition.has_value()) {
            custom_color.reset();
            state = State::EndlessCustomColor;
        }

        break;
    }
    case State::EndlessCustomColor: {
        if (custom_color.has_value()) {
            set_custom();
        }
#endif
        break;
    }
    case State::SetOff: {
        TransitionToColor(ColorRGBW(0, 0, 0, 0), 500);
        state = State::Off;
        break;
    }
    case State::Off:
        break;
    }

    if (current_transition.has_value()) {
        bool finished = current_transition->IsFinished();
        auto color = current_transition->GetCurrentColor();

        side_strip.SetColor(color);
        side_strip.Update();
        if (finished) {
            current_transition.reset();
        }
    }
}

leds::ColorRGBW SideStripControl::HsvToRgb(HsvColor hsv) {
    ColorRGBW rgb;
    unsigned char region, remainder, p, q, t;

    if (hsv.s == 0) {
        rgb.r = hsv.v;
        rgb.g = hsv.v;
        rgb.b = hsv.v;
        return rgb;
    }

    region = hsv.h / 43;
    remainder = (hsv.h - (region * 43)) * 6;

    p = (hsv.v * (255 - hsv.s)) >> 8;
    q = (hsv.v * (255 - ((hsv.s * remainder) >> 8))) >> 8;
    t = (hsv.v * (255 - ((hsv.s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
    case 0:
        rgb.r = hsv.v;
        rgb.g = t;
        rgb.b = p;
        break;
    case 1:
        rgb.r = q;
        rgb.g = hsv.v;
        rgb.b = p;
        break;
    case 2:
        rgb.r = p;
        rgb.g = hsv.v;
        rgb.b = t;
        break;
    case 3:
        rgb.r = p;
        rgb.g = q;
        rgb.b = hsv.v;
        break;
    case 4:
        rgb.r = t;
        rgb.g = p;
        rgb.b = hsv.v;
        break;
    default:
        rgb.r = hsv.v;
        rgb.g = p;
        rgb.b = q;
        break;
    }

    return rgb;
}

SideStripControl::HsvColor SideStripControl::RgbToHsv(ColorRGBW rgb) {
    uint8_t rgb_min = std::min({ rgb.r, rgb.g, rgb.b });
    uint8_t rgb_max = std::max({ rgb.r, rgb.g, rgb.b });

    HsvColor hsv;
    hsv.v = rgb_max;
    if (hsv.v == 0) {
        hsv.h = 0;
        hsv.s = 0;
        return hsv;
    }

    hsv.s = 255 * long(rgb_max - rgb_min) / hsv.v;
    if (hsv.s == 0) {
        hsv.h = 0;
        return hsv;
    }

    if (rgb_max == rgb.r) {
        hsv.h = 0 + 43 * (rgb.g - rgb.b) / (rgb_max - rgb_min);
    } else if (rgb_max == rgb.g) {
        hsv.h = 85 + 43 * (rgb.b - rgb.r) / (rgb_max - rgb_min);
    } else {
        hsv.h = 171 + 43 * (rgb.r - rgb.g) / (rgb_max - rgb_min);
    }

    return hsv;
}

void SideStripControl::load_config() {
    std::unique_lock lock(mutex);
    max_brightness_ = config_store().side_leds_max_brightness.get();
    dimmed_brightness_ = config_store().side_leds_dimmed_brightness.get();
    dimming_enabled = config_store().side_leds_dimming_enabled.get();
    // Force startup state so that the control is woken up and does the transition
    state = State::Startup;
}

void SideStripControl::set_max_brightness(uint8_t set) {
    config_store().side_leds_max_brightness.set(set);

    std::unique_lock lock(mutex);

    if (max_brightness_ == set) {
        return;
    }

    // Force startup state so that the control is woken up and does the transition
    state = State::Startup;
    max_brightness_ = set;
}

uint8_t SideStripControl::max_brightness() {
    std::unique_lock lock(mutex);
    return max_brightness_;
}

void SideStripControl::set_dimmed_brightness(uint8_t value) {
    config_store().side_leds_dimmed_brightness.set(value);

    std::unique_lock lock(mutex);

    if (dimmed_brightness_ == value) {
        return;
    }

    // Force startup state so that the control is woken up and does the transition
    state = State::Startup;
    dimmed_brightness_ = value;
}

uint8_t SideStripControl::dimmed_brightness() {
    std::unique_lock lock(mutex);
    return dimmed_brightness_;
}

void SideStripControl::set_dimming_enabled(DimmingEnabled set) {
    config_store().side_leds_dimming_enabled.set(set);
    std::unique_lock lock(mutex);
    dimming_enabled = set;
}

DimmingEnabled SideStripControl::is_dimming_enabled() {
    std::unique_lock lock(mutex);
    return dimming_enabled;
}

void SideStripControl::PanicOff() {
    std::unique_lock lock(mutex);
    state = State::Off;
}
