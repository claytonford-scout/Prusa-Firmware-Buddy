/// \file
#pragma once

#include <marlin_server_types/client_response.hpp>

enum class PhaseSelftestFSensors : PhaseUnderlyingType {
    init,

    _cnt,
    _last = _cnt - 1
};

namespace ClientResponses {

inline constexpr EnumArray<PhaseSelftestFSensors, PhaseResponses, PhaseSelftestFSensors::_cnt> selftest_fsensors_responses {
    { PhaseSelftestFSensors::init, {} },
};

} // namespace ClientResponses

constexpr inline ClientFSM client_fsm_from_phase(PhaseSelftestFSensors) { return ClientFSM::SelftestFSensors; }
