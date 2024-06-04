#pragma once

namespace e2ee {

// FIXME: This is temporary location, for development only. Eventually, we'll "hide" it somewhere in the xflash.
constexpr const char *const key_path = "/usb/e2ee.der";

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

} // namespace e2ee
