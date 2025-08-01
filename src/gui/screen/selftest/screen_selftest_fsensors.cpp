#include "screen_selftest_fsensors.hpp"

#include <img_resources.hpp>

namespace {

class FrameInit {
public:
    FrameInit(window_t *) {}
};

using Phase = PhaseSelftestFSensors;

using Frames = FrameDefinitionList<ScreenSelftestFSensors::FrameStorage,
    FrameDefinition<Phase::init, FrameInit> //
    >;

} // namespace

ScreenSelftestFSensors::ScreenSelftestFSensors()
    : ScreenFSM(N_("FSENSOR SELFTEST"), GuiDefaults::RectScreenBody) {
    header.SetIcon(&img::selftest_16x16);
    CaptureNormalWindow(inner_frame);
    create_frame();
}

ScreenSelftestFSensors::~ScreenSelftestFSensors() {
    destroy_frame();
}

void ScreenSelftestFSensors::create_frame() {
    Frames::create_frame(frame_storage, get_phase(), &inner_frame);
}

void ScreenSelftestFSensors::destroy_frame() {
    Frames::destroy_frame(frame_storage, get_phase());
}

void ScreenSelftestFSensors::update_frame() {
    Frames::update_frame(frame_storage, get_phase(), fsm_base_data.GetData());
}
