/**
 * @file hw_configuration_common.cpp
 */

#include "hw_configuration_common.hpp"
#include "data_exchange.hpp"
#include "otp.hpp"
#include <device/hal.h>
#include <option/bootloader.h>

namespace buddy::hw {

ConfigurationCommon::ConfigurationCommon()
    : xlcd_eeprom { data_exchange::get_xlcd_eeprom() }
    , xlcd_status { data_exchange::get_xlcd_status() } {
    bom_id = otp_get_bom_id().value_or(0);
    bom_id_xlcd = otp_parse_bom_id(reinterpret_cast<uint8_t *>(&xlcd_eeprom), sizeof(XlcdEeprom)).value_or(0);
}

} // namespace buddy::hw
