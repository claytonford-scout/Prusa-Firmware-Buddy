#include <e2ee/e2ee.hpp>
#include <e2ee/key.hpp>
#include "unique_file_ptr.hpp"

#include <mbedtls/pk.h>

namespace e2ee {

// Note: right now the only difference from the real implementation is the file path
//  we arte reading, but the real implementation will eventually read it from xflash
//  , but this one still from the file.
mbedtls_pk_context *PrinterPrivateKey::get_printer_private_key() {
    if (key_valid) {
        return &key->pk;
    }
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[e2ee::PRIVATE_KEY_BUFFER_SIZE]);
    //  Get the private key, this will eventually live in flash
    unique_file_ptr inf(fopen("printer_private_key.der", "rb"));
    if (!inf) {
        return nullptr;
    }

    size_t ins = fread(buffer.get(), 1, e2ee::PRIVATE_KEY_BUFFER_SIZE, inf.get());
    if (ins == 0 || ferror(inf.get()) || !feof(inf.get())) {
        return nullptr;
    }
    inf.reset();
    if (mbedtls_pk_parse_key(&key->pk, buffer.get(), ins, NULL /* No password */, 0) != 0) {
        return nullptr;
    }

    key_valid = true;
    return &key->pk;
}

} // namespace e2ee
