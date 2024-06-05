#pragma once

#include "screen_menu.hpp"

#include <memory>

namespace e2ee {
class KeyGen;
}

class ScreenMenuE2ee;

namespace detail_e2ee {

class MI_KEY final : public WI_INFO_t {
    constexpr static const char *const label = N_("Key");

public:
    MI_KEY();
    void update(ScreenMenuE2ee *parent);
};

class MI_KEYGEN final : public IWindowMenuItem {
    constexpr static const char *const label = N_("Generate");

public:
    MI_KEYGEN();

protected:
    virtual void click(IWindowMenu &window_menu) override;
};

class MI_EXPORT final : public IWindowMenuItem {
    constexpr static const char *const label = N_("Export");

public:
    MI_EXPORT();

protected:
    virtual void click(IWindowMenu &window_menu) override;
};

// TODO:
// * Delete key? Do we need it?
using Menu = ScreenMenu<GuiDefaults::MenuFooter, MI_RETURN, MI_KEY, MI_KEYGEN, MI_EXPORT>;
} // namespace detail_e2ee

class ScreenMenuE2ee final : public detail_e2ee::Menu {
private:
    friend class detail_e2ee::MI_KEY;
    std::unique_ptr<e2ee::KeyGen> keygen;
    void update();

protected:
    void windowEvent(window_t *sender, GUI_event_t event, void *param);

public:
    constexpr static const char *label = N_("Encryption");
    ScreenMenuE2ee();
    // Because of the unique_ptr and forward-declared class, we need a destructor elsewhere.
    ~ScreenMenuE2ee();
};
