#pragma once

#include "IDialogMarlin.hpp"
#include "client_response.hpp"
#include "common/static_storage.hpp"
#include "window_header.hpp"

class DialogLoadUnload : public IDialogMarlin {
private:
    static constexpr size_t frame_static_storage_size = 1324;

public:
    DialogLoadUnload(fsm::BaseData data);
    ~DialogLoadUnload();

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    using FrameStorage = StaticStorage<frame_static_storage_size>;
#pragma GCC diagnostic pop

    void Change(fsm::BaseData data) override;

protected:
    void create_frame();
    void destroy_frame();
    void update_frame();

private:
    fsm::BaseData fsm_base_data;
    FrameStorage frame_storage;
    window_frame_t inner_frame;

    inline PhasesLoadUnload get_phase() const {
        return GetEnumFromPhaseIndex<PhasesLoadUnload>(fsm_base_data.GetPhase());
    }
};
