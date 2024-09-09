#pragma once

#include <async_job/async_job_execution_control.hpp>
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

struct Pk;
class SHA256MultiuseHash;
// FIXME: This is temporary location, for development only. Eventually, we'll "hide" it somewhere in the xflash.
#ifdef UNITTESTS
constexpr const char *const key_path = "printer_private_key.der";
#else
constexpr const char *const key_path = "/internal/e2ee/printer/pk.der";
#endif
constexpr const char *const pubkey_path = "/usb/pubkey.der";

constexpr size_t HASH_SIZE = 32;
constexpr size_t HMAC_SIZE = 32;
constexpr size_t KEY_SIZE = 16;
constexpr size_t SIGN_SIZE = 256;
// Size discovered by experimental means.
// FIXME: the key is probably smaller, investigate the size more and maybe make this smaller
constexpr size_t PRIVATE_KEY_BUFFER_SIZE = 2048;

// Error texts
constexpr const char *encrypted_for_different_printer = "Bgcode not encrypted for this printer!";
constexpr const char *key_block_hash_mismatch = "Key block hash mismatch";
constexpr const char *metadata_not_beggining = "Corrupted bgcode, metadata not at the beggining.";
constexpr const char *additional_data = "Additional non authorized data found.";
constexpr const char *key_before_identity = "Corrupted bgcode, key block before identity block.";
constexpr const char *encrypted_before_identity = "Corrupted bgcode, encrypted block before identity block.";
constexpr const char *encrypted_before_key = "Corrupted bgcode, encrypted block before key block.";
constexpr const char *unencrypted_in_encrypted = "Unencrypted gcode block found in encrypted bgcode.";
constexpr const char *file_error = "Error while reading file.";
constexpr const char *unknown_identity_cypher = "Unknown Identity block cypher";
constexpr const char *compressed_identity_block = "Compressed identity block not supported";
constexpr const char *identity_parsing_error = "Identity block parsing error";
constexpr const char *identity_verification_fail = "Identity verification failed!";
constexpr const char *identity_name_too_long = "Identity name too long";
constexpr const char *corrupted_metadata = "File has corrupted metadata";

struct IdentityBlockInfo {
    static constexpr size_t IDENTITY_NAME_LEN = 32;

    std::unique_ptr<Pk> identity_pk;
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

class PrinterPrivateKey {
public:
    PrinterPrivateKey();
    ~PrinterPrivateKey();
    PrinterPrivateKey(const PrinterPrivateKey &) = delete;
    PrinterPrivateKey &operator=(const PrinterPrivateKey &) = delete;
    PrinterPrivateKey(PrinterPrivateKey &&);
    PrinterPrivateKey &operator=(PrinterPrivateKey &&);
    mbedtls_pk_context *get_printer_private_key();

private:
    bool key_valid = false;
    std::unique_ptr<Pk> key;
};

// if computed_intro_hash is nullptr the hash is not checked
const char *read_and_verify_identity_block(FILE *file, const bgcode::core::BlockHeader &block_header, uint8_t *computed_intro_hash, IdentityBlockInfo &info, bool verify_signature);

struct SymmetricCipherInfo {
    bool valid = false;
    uint8_t encryption_key[KEY_SIZE];
    uint8_t sign_key[KEY_SIZE];
    uint32_t num_of_hmacs = 0;
    uint32_t hmac_index = 0; // where our HMAC is

    bool extract_keys(uint8_t *key_block, size_t size);
};

std::optional<SymmetricCipherInfo> decrypt_key_block(FILE *file, const bgcode::core::BlockHeader &block_header, Pk &identity_pk, mbedtls_pk_context *printer_private_key, SHA256MultiuseHash *hash);

bool rsa_sha256_sign_verify(mbedtls_pk_context &pk, const uint8_t *message, size_t message_size, const uint8_t *signature, size_t sig_size);
bool rsa_oaep_decrypt(mbedtls_pk_context &pk, const uint8_t *encrypted_msg, size_t msg_size, uint8_t *output, size_t out_size);

} // namespace e2ee
