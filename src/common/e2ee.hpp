#pragma once

namespace e2ee {

// FIXME: This is temporary location, for development only. Eventually, we'll "hide" it somewhere in the xflash.
constexpr const char *const key_path = "/usb/e2ee.der";
constexpr const char *const pubkey_path = "/usb/pubkey.der";

class KeyGen {
public:
    enum class LoopResult {
        Done,
        Failed,
        Continue,
    };
    LoopResult loop();
    // TODO
};

bool export_key();

} // namespace e2ee
