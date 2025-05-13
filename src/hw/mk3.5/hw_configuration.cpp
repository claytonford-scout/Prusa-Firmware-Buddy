/// @file
#include "hw_configuration.hpp"

#include "data_exchange.hpp"
#include "bsod.h"
#include "otp.hpp"
#include <option/bootloader.h>

/*
+-----++----------------------------------------------+-------------------------------+-------------------+
|pin  || MK4 xBuddy027 (with R191 0R)                 | MK4 xBuddy037                 | MK3.5 xBuddy037   |
+-----++----------------------------------------------+-------------------------------+-------------------+
|PA6  || Heatbreak temp                               | Heatbreak temp                | FILAMENT_SENSOR   |
|PA10 || accel CS                                     | accel CS                      | TACHO0            |
|PE9  || FAN1 PWM inverted                            | FAN1 PWM                      | FAN1 PWM          |
|PE10 || both fans tacho                              | both fans tacho               | TACHO1            |
|PE11 || FAN0 PWM inverted                            | FAN0 PWM                      | FAN0 PWM          |
|PE14 || NOT CONNECTED == same as MK4 on 037 (pullup) | EXTRUDER_SWITCH .. use pullup | EXTRUDER_SWITCH   |
|PF13 || eeprom, fan multiplexor                      | eeprom, fan multiplexor       | ZMIN (PINDA)      |
+-----++----------------------------------------------+-------------------------------+-------------------+


PC0 HOTEND_NTC is the same for all versions, but needs EXTRUDER_SWITCH enabled to provide pullup for MK3.5

xBuddy037 FW related changes
disconnected power panic cable will cause power panic
current measurement changed from 5V to 3V3 - need to recalculate values
MMU switching changed - no need to generate pulses anymore
MMU_RESET logic inverted
*/

namespace buddy::hw {

Configuration &Configuration::Instance() {
    static Configuration ths = Configuration();
    return ths;
}

Configuration::Configuration() {
    auto bom_id = otp_get_bom_id();

    if (!bom_id || *bom_id == 27) {
        bsod("Wrong board version");
    }

    loveboard_present = data_exchange::get_loveboard_status().data_valid;
}

bool Configuration::has_inverted_fans() const {
    return false;
}

bool Configuration::has_inverted_mmu_reset() const {
    return true;
}

bool Configuration::has_mmu_power_up_hw() const {
    return true;
}

bool Configuration::has_trinamic_oscillators() const {
    return true;
}

bool Configuration::is_fw_incompatible_with_hw() const {
    // valid data from loveboard means that we have MK4 HW, since MK3.5 does not have loveboard
    return loveboard_present;
}

float Configuration::curr_measurement_voltage_to_current(float voltage) const {
    constexpr float allegro_curr_from_voltage = 1 / 0.09F;
    const float allegro_zero_curr_voltage = 3.35F / 2.F; // choose half of 3V3 range
    return (voltage - allegro_zero_curr_voltage) * allegro_curr_from_voltage;
}

bool Configuration::needs_push_pull_mmu_reset_pin() const {
    return true;
}

bool Configuration::needs_software_mmu_powerup() const {
    return true;
}

} // namespace buddy::hw
