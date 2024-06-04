#pragma once

#include "screen_menu.hpp"

namespace detail {
using ScreenMenuE2ee = ScreenMenu<GuiDefaults::MenuFooter, MI_RETURN>;
}

class ScreenMenuE2ee : public detail::ScreenMenuE2ee {
public:
    constexpr static const char *label = N_("Encryption");
    ScreenMenuE2ee();
};
