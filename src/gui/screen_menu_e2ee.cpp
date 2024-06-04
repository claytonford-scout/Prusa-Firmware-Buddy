#include "screen_menu_e2ee.hpp"
#include "ScreenHandler.hpp"

#include <common/e2ee.hpp>
#include <common/stat_retry.hpp>

#include <sys/stat.h>

using e2ee::KeyGen;

namespace {
bool file_exists(const char *fname) {
    struct stat st;
    return stat_retry(fname, &st) == 0 && S_ISREG(st.st_mode);
}
} // namespace

namespace detail_e2ee {

MI_KEY::MI_KEY()
    : WI_INFO_t(_(label), nullptr, is_enabled_t::yes, is_hidden_t::no) {
    update(nullptr);
}

void MI_KEY::update(ScreenMenuE2ee *parent) {
    if (parent != nullptr && parent->keygen) {
        ChangeInformation(_("Generating"));
    } else {
        // TODO: Shall we show some kind of fingerprint?
        // What _is_ even a fingerprint for a raw RSA key?
        ChangeInformation(_(file_exists(e2ee::key_path) ? N_("Present") : N_("---")));
    }
}

MI_KEYGEN::MI_KEYGEN()
    : IWindowMenuItem(_(label), nullptr, is_enabled_t::yes, is_hidden_t::no) {}

void MI_KEYGEN::click(IWindowMenu &) {
    Screens::Access()->Get()->WindowEvent(nullptr, GUI_event_t::CHILD_CLICK, this);
}
} // namespace detail_e2ee

ScreenMenuE2ee::ScreenMenuE2ee()
    : detail_e2ee::Menu(_(label)) {
}

ScreenMenuE2ee::~ScreenMenuE2ee() {
    // Intentionally left blank;
}

void ScreenMenuE2ee::update() {
    Item<detail_e2ee::MI_KEY>().update(this);
}

void ScreenMenuE2ee::windowEvent(window_t *, GUI_event_t event, void *param) {
    switch (event) {
    case GUI_event_t::CHILD_CLICK: {
        if (param == &Item<detail_e2ee::MI_KEYGEN>()) {
            if (!keygen) {
                // TODO: Ask about overwriting the previous file, if present
                keygen.reset(new KeyGen());
                update();
            }
            return;
        }
        break;
    }
    case GUI_event_t::LOOP: {
        if (keygen) {
            switch (keygen->loop()) {
            case KeyGen::LoopResult::Continue:
                break;
            case KeyGen::LoopResult::Done:
                keygen.reset();
                update();
                break;
            case KeyGen::LoopResult::Failed:
                // TODO: Some kind of indication why?
                keygen.reset();
                MsgBoxWarning(_("Failed to generate key"), Responses_Ok);
                update();
                break;
            }
        }
    }
    default:
        break;
    }
}
