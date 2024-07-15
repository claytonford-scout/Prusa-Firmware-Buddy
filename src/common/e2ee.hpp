#pragma once

#include <optional>
#include <memory>
#include <array>

namespace bgcode {
namespace core {
    struct BlockHeader;
}
} // namespace bgcode
struct mbedtls_pk_context;

namespace e2ee {

// FIXME: This is temporary location, for development only. Eventually, we'll "hide" it somewhere in the xflash.
constexpr const char *const key_path = "/usb/e2ee.der";
constexpr const char *const pubkey_path = "/usb/pubkey.der";

constexpr size_t HASH_SIZE = 32;
constexpr size_t KEY_SIZE = 16;
constexpr size_t SIGN_SIZE = 256;
// Size discovered by experimental means.
// FIXME: the key is probably smaller, investigate the size more and maybe make this smaller
constexpr size_t PRIVATE_KEY_BUFFER_SIZE = 2048;

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

struct IdentityBlockInfo {
    static constexpr size_t IDENTITY_NAME_LEN = 32;

    std::unique_ptr<mbedtls_pk_context> identity_pk;
    // TODO: how long sould we allow this to be??
    std::array<char, IDENTITY_NAME_LEN> identity_name;
    std::array<uint8_t, HASH_SIZE> key_block_hash;

    IdentityBlockInfo();
    ~IdentityBlockInfo();
    IdentityBlockInfo(const IdentityBlockInfo &) = delete;
    IdentityBlockInfo &operator=(const IdentityBlockInfo &) = delete;
    IdentityBlockInfo(IdentityBlockInfo &&);
    IdentityBlockInfo &operator=(IdentityBlockInfo &&);
};

const char *read_and_verify_identity_block(FILE *file, const bgcode::core::BlockHeader &block_header, uint8_t *computed_intro_hash, IdentityBlockInfo &info);

struct SymmetricKeys {
    bool valid = false;
    uint8_t encryption_key[KEY_SIZE];
    uint8_t sign_key[KEY_SIZE];

    bool extract_keys(uint8_t *key_block, size_t size);
};

std::optional<SymmetricKeys> decrypt_key_block(FILE *file, const bgcode::core::BlockHeader &block_header, mbedtls_pk_context &identity_pk, mbedtls_pk_context *printer_private_key);

bool rsa_sha256_sign_verify(mbedtls_pk_context &pk, const uint8_t *message, size_t message_size, const uint8_t *signature, size_t sig_size);
bool rsa_oaep_decrypt(mbedtls_pk_context &pk, const uint8_t *encrypted_msg, size_t msg_size, uint8_t *output, size_t out_size);

} // namespace e2ee
