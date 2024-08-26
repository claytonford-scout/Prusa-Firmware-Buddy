#include "screen_menu_e2ee.hpp"
#include "ScreenHandler.hpp"

#include <common/e2ee/e2ee.hpp>
#include <common/e2ee/key.hpp>
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
    update(false);
}

void MI_KEY::update(bool generating) {
    if (generating) {
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

MI_EXPORT::MI_EXPORT()
    : IWindowMenuItem(_(label), nullptr, is_enabled_t::yes, is_hidden_t::no) {}

void MI_EXPORT::click(IWindowMenu &) {
    if (e2ee::export_key()) {
        MsgBox(_("Exported"), Responses_Ok);
    } else {
        // TODO: More details?
        MsgBoxWarning(_("Failed to export"), Responses_Ok);
    }
}

} // namespace detail_e2ee

ScreenMenuE2ee::ScreenMenuE2ee()
    : detail_e2ee::Menu(_(label)) {
}

ScreenMenuE2ee::~ScreenMenuE2ee() {
    // Intentionally left blank;
}

void ScreenMenuE2ee::update(bool generating) {
    Item<detail_e2ee::MI_KEY>().update(generating);
}

void ScreenMenuE2ee::windowEvent(window_t *, GUI_event_t event, void *param) {
    switch (event) {
    case GUI_event_t::CHILD_CLICK: {
        if (param == &Item<detail_e2ee::MI_KEYGEN>()) {
            finished_handled = false;
            // TODO: Ask about overwriting the already existing key
            key_generation.issue(&e2ee::generate_key);
            update(true);
            return;
        }
        break;
    }
    case GUI_event_t::LOOP: {
        switch (key_generation.state()) {
        case AsyncJob::State::finished:
            if (!key_generation.result() && !finished_handled) {
                finished_handled = true;
                MsgBoxWarning(_("Failed to generate key"), Responses_Ok);
            }
            update(false);
            break;
        case AsyncJob::State::running:
        case AsyncJob::State::queued:
            update(true);
            break;
        default:
            update(false);
            break;
        }
    }
    default:
        break;
    }
}
