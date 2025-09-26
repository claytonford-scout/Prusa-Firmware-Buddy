#include "DialogHandler.hpp"

#include "DialogLoadUnload.hpp"
#include "IScreenPrinting.hpp"
#include "ScreenHandler.hpp"
#include "screen_printing.hpp"
#include "config_features.h"
#include "screen_print_preview.hpp"
#include "window_dlg_quickpause.hpp"
#include "window_dlg_wait.hpp"
#include "window_dlg_warning.hpp"
#include <option/has_gearbox_alignment.h>
#include <option/has_phase_stepping_calibration.h>
#include <option/has_input_shaper_calibration.h>
#include <option/has_coldpull.h>
#include <option/has_door_sensor_calibration.h>
#include <option/has_manual_belt_tuning.h>
#include <option/has_loadcell.h>
#include <gui/screen/screen_preheat.hpp>
#if HAS_LOADCELL()
    #include <gui/screen/screen_nozzle_cleaning_failed.hpp>
#endif

#if HAS_MANUAL_BELT_TUNING()
    #include <screen/selftest/screen_manual_belt_tuning.hpp>
#endif

#if HAS_COLDPULL()
    #include "screen_cold_pull.hpp"
#endif

#if HAS_SELFTEST()
    #include <screen_fan_selftest.hpp>
    #include "ScreenSelftest.hpp"
#endif

#if ENABLED(CRASH_RECOVERY)
    #include "screen_crash_recovery.hpp"
#endif

#include <option/has_serial_print.h>
#if HAS_SERIAL_PRINT()
    #include "screen_printing_serial.hpp"
#endif

#if HAS_PHASE_STEPPING_CALIBRATION()
    #include "screen_phase_stepping_calibration.hpp"
#endif

#if HAS_INPUT_SHAPER_CALIBRATION()
    #include "screen_input_shaper_calibration.hpp"
#endif

#if HAS_BELT_TUNING()
    #include <gui/wizard/screen_belt_tuning_wizard.hpp>
#endif

#if HAS_GEARBOX_ALIGNMENT()
    #include "feature/gearbox_alignment/screen_gearbox_alignment.hpp"
#endif

#if HAS_DOOR_SENSOR_CALIBRATION()
    #include <feature/door_sensor_calibration/screen_door_sensor_calibration.hpp>
#endif

#include <option/has_esp.h>
#if HAS_ESP()
    #include <screen_network_setup.hpp>
#endif

alignas(std::max_align_t) static std::array<uint8_t, 2560> mem_space;

// safer than make_static_unique_ptr, checks storage size
template <class T, class... Args>
static static_unique_ptr<IDialogMarlin> make_dialog_ptr(Args &&...args) {
    static_assert(sizeof(T) <= mem_space.size(), "Error dialog does not fit");
    return make_static_unique_ptr<T>(mem_space.data(), std::forward<Args>(args)...);
}

template <ClientFSM fsm_, typename Screen>
struct FSMScreenDef {
    static constexpr ClientFSM fsm = fsm_;

    [[nodiscard]] static bool open([[maybe_unused]] fsm::BaseData data) {
        // We need to get out of the GUI loop to open the screen, windows are on the stack
        if (gui_get_nesting()) {
            return false;
        }

        constexpr auto screen = ScreenFactory::Screen<Screen>;

        if (Screens::Access()->IsScreenOpened(screen)) {
            // Already opened, no need to do anything
            return true;
        }

        Screens::Access()->Open(screen);

        // Do loop immediately for the window open to take effect
        // Otherwise, the change() that follows right after open() would not do anything
        Screens::Access()->Loop();

        return true;
    }

    static void close() {
        assert(Screens::Access()->IsScreenOnStack<Screen>());
        Screens::Access()->Close<Screen>();
    }

    [[nodiscard]] static bool change(fsm::BaseData data) {
        // DO NOT change screens while in the nested loop
        // We would be potentially changing phases while the GUI widgets are on the stack
        if (gui_get_nesting()) {
            return false;
        }

        if (auto s = Screens::Access()->get<Screen>()) {
            s->Change(data);
            return true;

        } else {
            // The screen is not on the top of the stack, we cannot notify it about the change
            // TODO: This should be okay as the screen should read the current state at the creation, but it's not doing that currently
            return false;
        }
    }
};

template <ClientFSM fsm_, typename Dialog>
struct FSMDialogDef {
    static constexpr ClientFSM fsm = fsm_;

    [[nodiscard]] static bool open(fsm::BaseData data) {
        auto &ptr = DialogHandler::Access().ptr;
        if (ptr) {
            // This should never happen - appropriate close() should always be called before
            bsod("Opening 2nd FSM dialog");
        }

        ptr = make_dialog_ptr<Dialog>(data);
        return true;
    }

    static void close() {
        // Ptr is a static_unique_ptr, it will call the destructor
        DialogHandler::Access().ptr = nullptr;
    }

    [[nodiscard]] static bool change(fsm::BaseData data) {
        // We CANNOT check for gui nesting for dialogs - dialogs can be shown blockingly over screens
        // One just has to hope that noone would call DialogHandler::Loop() inside a FSM dialog function - there's no nice way to check it
        // assert(!gui_get_nesting())

        if (auto &ptr = DialogHandler::Access().ptr) {
            ptr->Change(data);
            return true;

        } else {
            // The dialog is not on the stact - should not happen
            assert(false);
            return false;
        }
    }
};

template <ClientFSM fsm_>
struct FSMPrintDef {
    static constexpr ClientFSM fsm = fsm_;

    [[nodiscard]] static bool open([[maybe_unused]] fsm::BaseData data) {
        // We need to get out of the GUI loop to open the screen, windows are on the stack
        if (gui_get_nesting()) {
            return false;
        }

        if (IScreenPrinting::GetInstance()) {
            IScreenPrinting::NotifyMarlinStart();
            return true;
        }

        if constexpr (fsm == ClientFSM::Serial_printing) {
            Screens::Access()->ClosePrinting();
            Screens::Access()->Open(ScreenFactory::Screen<screen_printing_serial_data_t>);

        } else if constexpr (fsm == ClientFSM::Printing) {
            Screens::Access()->CloseAll();
            Screens::Access()->Open(ScreenFactory::Screen<screen_printing_data_t>);

        } else {
            static_assert(0);
        }

        // Do loop immediately for the window open to take effect
        // Otherwise, the change() that follows right after open() would not do anything
        Screens::Access()->Loop();
        return true;
    }

    static void close() {
        Screens::Access()->CloseAll();
    }

    [[nodiscard]] static bool change([[maybe_unused]] fsm::BaseData data) {
        // Do nothing
        return true;
    }
};

// Just so that we have something at the end of the list and don't have to care about commas
struct FSMEndDef {
    static constexpr ClientFSM fsm = ClientFSM::_count;

    [[nodiscard]] static bool open(fsm::BaseData) {
        return true;
    }

    static void close() {}

    [[nodiscard]] static bool change(fsm::BaseData) {
        return true;
    }
};

template <class... T>
struct FSMDisplayConfigDef {
};

using FSMDisplayConfig = FSMDisplayConfigDef<
    FSMDialogDef<ClientFSM::Wait, window_dlg_wait_t>,
    FSMPrintDef<ClientFSM::Serial_printing>,
    FSMDialogDef<ClientFSM::Load_unload, DialogLoadUnload>,
    FSMScreenDef<ClientFSM::Preheat, ScreenPreheat>,
#if HAS_SELFTEST()
    FSMScreenDef<ClientFSM::Selftest, ScreenSelftest>,
    FSMScreenDef<ClientFSM::FansSelftest, ScreenFanSelftest>,
#endif
#if HAS_ESP()
    FSMScreenDef<ClientFSM::NetworkSetup, ScreenNetworkSetup>,
#endif
    FSMPrintDef<ClientFSM::Printing>,
#if ENABLED(CRASH_RECOVERY)
    FSMScreenDef<ClientFSM::CrashRecovery, ScreenCrashRecovery>,
#endif
    FSMDialogDef<ClientFSM::QuickPause, DialogQuickPause>,
    FSMDialogDef<ClientFSM::Warning, DialogWarning>,
    FSMScreenDef<ClientFSM::PrintPreview, ScreenPrintPreview>,
#if HAS_COLDPULL()
    FSMScreenDef<ClientFSM::ColdPull, ScreenColdPull>,
#endif
#if HAS_PHASE_STEPPING_CALIBRATION()
    FSMScreenDef<ClientFSM::PhaseSteppingCalibration, ScreenPhaseSteppingCalibration>,
#endif
#if HAS_INPUT_SHAPER_CALIBRATION()
    FSMScreenDef<ClientFSM::InputShaperCalibration, ScreenInputShaperCalibration>,
#endif
#if HAS_BELT_TUNING()
    FSMScreenDef<ClientFSM::BeltTuning, ScreenBeltTuningWizard>,
#endif
#if HAS_GEARBOX_ALIGNMENT()
    FSMScreenDef<ClientFSM::GearboxAlignment, ScreenGearboxAlignment>,
#endif
#if HAS_DOOR_SENSOR_CALIBRATION()
    FSMScreenDef<ClientFSM::DoorSensorCalibration, ScreenDoorSensorCalibration>,
#endif
#if HAS_MANUAL_BELT_TUNING()
    FSMScreenDef<ClientFSM::BeltTuning, ScreenManualBeltTuning>,
#endif
#if HAS_LOADCELL()
    FSMScreenDef<ClientFSM::NozzleCleaningFailed, ScreenNozzleCleaningFailed>,
#endif
    // This is here so that we can worry-free write commas at the end of each argument
    FSMEndDef>;

void visit_display_config(ClientFSM fsm, auto f) {
    [&]<class... T>(FSMDisplayConfigDef<T...>) {
        ((fsm == T::fsm ? f(T()) : void()), ...);
    }(FSMDisplayConfig());
};

static constexpr size_t fsm_display_config_size = []<class... T>(FSMDisplayConfigDef<T...>) { return sizeof...(T); }(FSMDisplayConfig());
static_assert(fsm_display_config_size == std::to_underlying(ClientFSM::_count) + 1);

//*****************************************************************************
// method definitions
bool DialogHandler::open(ClientFSM fsm_type, fsm::BaseData data) {
    bool result = false;
    visit_display_config(fsm_type, [&]<typename Config>(Config) {
        result = Config::open(data);
    });
    return result;
}

void DialogHandler::close(ClientFSM fsm_type) {
    visit_display_config(fsm_type, []<typename Config>(Config) {
        Config::close();
    });
}

bool DialogHandler::change(ClientFSM fsm_type, fsm::BaseData data) {
    bool result = false;

    visit_display_config(fsm_type, [&]<typename Config>(Config) {
        result = Config::change(data);
    });

    return result;
}

bool DialogHandler::IsOpen() const {
    return ptr != nullptr;
}

DialogHandler &DialogHandler::Access() {
    static DialogHandler instance;
    return instance;
}

void DialogHandler::Loop() {
    const auto new_top = marlin_vars().peek_fsm_states([](const auto &states) { return states.get_top(); });

    if (new_top == current_fsm_top) {
        return;
    }

    // Shortcut - we're just changing the data
    if (new_top && current_fsm_top && new_top->fsm_type == current_fsm_top->fsm_type) {
        if (!change(new_top->fsm_type, new_top->data)) {
            // Failed to change state - try again later
            return;
        }
        current_fsm_top = new_top;
        return;
    }

    // Close the previous screen/dialog
    if (current_fsm_top) {
        // Closing should never fail
        // Dialog - destroys the dialog (we're praying that noone got the idea to call DialogHandler::Loop() inside a FSM dialog function - there's no nice way to check it)
        // Screen - just marks the screen to be destroyed on the next Screens::Access()->Loop
        close(current_fsm_top->fsm_type);
        current_fsm_top = std::nullopt;
    }

    // Open the new one
    if (new_top) {
        if (!open(new_top->fsm_type, new_top->data)) {
            return;
        }

        if (!change(new_top->fsm_type, new_top->data)) {
            bsod_unreachable(); // Should never fail, we should just have opened the screen
        }

        current_fsm_top = new_top;
    }
}
