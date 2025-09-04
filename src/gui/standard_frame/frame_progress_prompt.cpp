#include "frame_progress_prompt.hpp"

#include <gui/auto_layout.hpp>

FrameProgressPrompt::FrameProgressPrompt(window_frame_t *parent, FSMAndPhase fsm_phase, const string_view_utf8 &txt_title, const string_view_utf8 &txt_info, Align_t info_alignment)
    : title(parent, {}, is_multiline::yes, is_closed_on_click_t::no, txt_title)
    , progress_bar(parent, {}, COLOR_ORANGE, COLOR_GRAY)
    , info(parent, {}, is_multiline::yes, is_closed_on_click_t::no, txt_info)
    , radio(parent, {}, fsm_phase) //
{
    title.SetAlignment(Align_t::CenterBottom());
    title.set_font(GuiDefaults::FontBig);

    info.SetAlignment(info_alignment);
#if HAS_MINI_DISPLAY()
    info.set_font(Font::small);
#endif

    static_cast<window_frame_t *>(parent)->CaptureNormalWindow(radio);

    static constexpr std::initializer_list layout {
        StackLayoutItem { .height = 64 },
        standard_stack_layout::for_progress_bar,
        StackLayoutItem { .height = StackLayoutItem::stretch, .margin_side = 16, .margin_top = 16 },
        standard_stack_layout::for_radio,
    };
    layout_vertical_stack(parent->GetRect(), { &title, &progress_bar, &info, &radio }, layout);
}
