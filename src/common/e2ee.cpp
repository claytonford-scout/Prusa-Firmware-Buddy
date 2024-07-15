#include "e2ee.hpp"

#include "core/core.hpp"
#include "heap.h"
#include <sha256.h>
#include "unique_file_ptr.hpp"
#include <raii/deleter.hpp>

#include <mbedtls/config.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>

#include <utility_extensions.hpp>

#include <cstring>
#include <cstdio>
#include <cstdlib>

using bgcode::core::block_parameters_size;
using bgcode::core::BlockHeader;
using bgcode::core::EBlockType;
using bgcode::core::ECompressionType;
using bgcode::core::EIdentityBlockSignCypher;
using bgcode::core::EKeyBlockEncryption;
using std::unique_ptr;

using string_view_u8 = std::basic_string_view<uint8_t>;

namespace {

// So we get RAII for all the init-free contexts of mbedtls
struct KeyGenContexts {
    mbedtls_rsa_context rsa;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_pk_context pk;

    KeyGenContexts() {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_rsa_init(&rsa, MBEDTLS_RSA_PKCS_V15, 0);
        mbedtls_pk_init(&pk);
        mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
        pk.pk_ctx = &rsa;
    }
    ~KeyGenContexts() {
        // For some reasons, the pk works fine when it is a result of reading
        // it from somewhere. But when we "compose" it from parts, as above, it
        // does weird things and corrupts the memory. Therefore, we have to
        // decompose it before destruction.
        pk.pk_ctx = nullptr;
        mbedtls_pk_free(&pk);
        mbedtls_rsa_free(&rsa);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
    }
};

struct RandomContexts {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    RandomContexts() {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);
    }
    ~RandomContexts() {
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
    }
};

struct Pk {
    mbedtls_pk_context pk;
    Pk() {
        mbedtls_pk_init(&pk);
    }
    ~Pk() {
        mbedtls_pk_free(&pk);
    }
};

size_t get_block_header_bytes(BlockHeader header, uint8_t *buffer, size_t buffer_size) {
    if (buffer_size < header.get_size()) {
        // TODO
        return 0;
    }
    size_t pos = 0;
    memcpy(&buffer[pos], &header.type, sizeof(header.type));
    pos += sizeof(header.type);
    memcpy(&buffer[pos], &header.compression, sizeof(header.compression));
    pos += sizeof(header.compression);
    memcpy(&buffer[pos], &header.uncompressed_size, sizeof(header.uncompressed_size));
    pos += sizeof(header.uncompressed_size);
    if ((ECompressionType)header.compression != ECompressionType::None) {
        memcpy(&buffer[pos], &header.compressed_size, sizeof(header.compressed_size));
        pos += sizeof(header.compressed_size);
    }
    return pos;
}

bool read_from_file(void *data, size_t data_size, FILE *file) {
    const size_t rsize = fread(data, 1, data_size, file);
    return !ferror(file) && rsize == data_size;
}
} // namespace

namespace e2ee {

// TODO: Somehow make this non-blocking.
//
// Either:
// * Bring in newer mbedtls, that has mbedtls_rsa_gen_key_step. Unfortunately,
//   back then, we didn't use newer version, because it needed more resources.
//   Maybe that's different now?
// * Run it in some kind of background task or create a background executor.
KeyGen::LoopResult KeyGen::loop() {
    unique_ptr<uint8_t, FreeDeleter> buffer;
    int export_res = 0;
    {
        struct KeyGenContexts contexts;

        const char *pers = "ecp_keypair";

        if (mbedtls_ctr_drbg_seed(&contexts.ctr_drbg, mbedtls_entropy_func, &contexts.entropy, (const unsigned char *)pers, strlen(pers)) != 0) {
            return LoopResult::Failed;
        }

        if (mbedtls_rsa_gen_key(&contexts.rsa, mbedtls_ctr_drbg_random, &contexts.ctr_drbg, 2048, 65537) != 0) {
            return LoopResult::Failed;
        }

        buffer.reset(reinterpret_cast<uint8_t *>(malloc_fallible(PRIVATE_KEY_BUFFER_SIZE)));

        if (!buffer) {
            return LoopResult::Failed;
        }

        export_res = mbedtls_pk_write_key_der(&contexts.pk, buffer.get(), PRIVATE_KEY_BUFFER_SIZE);
        if (export_res <= 0) {
            return LoopResult::Failed;
        }
    } // Free the mbedtls contexts, better chance to have RAM for the file

    unique_file_ptr fout(fopen(key_path, "wb"));
    if (!fout) {
        return LoopResult::Failed;
    }

    // Note: The mbedtls_pk_write_key_der writes to the _end_ of the buffer.
    if (fwrite(buffer.get() + PRIVATE_KEY_BUFFER_SIZE - export_res, export_res, 1, fout.get()) != 1) {
        return LoopResult::Failed;
    }

    return LoopResult::Done;
}

bool export_key() {
    unique_ptr<uint8_t, FreeDeleter> buffer(reinterpret_cast<uint8_t *>(malloc_fallible(PRIVATE_KEY_BUFFER_SIZE)));
    if (!buffer) {
        return false;
    }

    unique_file_ptr inf(fopen(key_path, "rb"));
    if (!inf) {
        return false;
    }

    size_t ins = fread(buffer.get(), 1, PRIVATE_KEY_BUFFER_SIZE, inf.get());
    if (ins == 0 || ferror(inf.get()) || !feof(inf.get())) {
        return false;
    }

    inf.reset();

    int ret = 0;
    {
        Pk pk;
        if (mbedtls_pk_parse_key(&pk.pk, buffer.get(), ins, NULL /* No password */, 0) != 0) {
            return false;
        }

        ret = mbedtls_pk_write_pubkey_der(&pk.pk, buffer.get(), PRIVATE_KEY_BUFFER_SIZE);

        if (ret <= 0) {
            return false;
        }
    } // Destroy the pk

    unique_file_ptr outf(fopen(pubkey_path, "wb"));
    if (!outf) {
        return false;
    }

    // Note: mbedtls writes to the _end_ of the buffer.
    if (fwrite(buffer.get() + PRIVATE_KEY_BUFFER_SIZE - ret, ret, 1, outf.get()) != 1) {
        outf.reset();
        // Result not checked - no way we can fail twice anyway.
        remove(pubkey_path);
        return false;
    }

    return true;
}

IdentityBlockInfo::IdentityBlockInfo()
    : identity_pk(std::make_unique<mbedtls_pk_context>()) {
    mbedtls_pk_init(identity_pk.get());
}

IdentityBlockInfo::~IdentityBlockInfo() {
    mbedtls_pk_free(identity_pk.get());
}

IdentityBlockInfo::IdentityBlockInfo(IdentityBlockInfo &&other)
    : identity_pk(std::move(other.identity_pk))
    , identity_name(std::move(other.identity_name))
    , key_block_hash(std::move(other.key_block_hash)) {}

IdentityBlockInfo &IdentityBlockInfo::operator=(IdentityBlockInfo &&other) {
    identity_pk = std::move(other.identity_pk);
    identity_name = std::move(other.identity_name);
    key_block_hash = std::move(other.key_block_hash);
    return *this;
}

bool rsa_sha256_sign_verify(mbedtls_pk_context &pk, const uint8_t *message, size_t message_size, const uint8_t *signature, size_t sig_size) {
    unsigned char hash[HASH_SIZE];

    // Calculate the SHA-256 hash of the message
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if ((mbedtls_md(md_info, message, message_size, hash)) != 0) {
        return false;
    }

    // Note: Enabling PSS padding in mbedtls adds ~ 2KB of flash, but it is more secure
    // then PKCS1_V15 which we already have in the code rn. The same define brings in OAEP padding
    // for encryption and that one is also more secure then PKCS1_V15, so maybe it is worth it
    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(pk);
    if (sig_size != rsa->len) {
        return false;
    }
    if ((mbedtls_rsa_rsassa_pss_verify(rsa, nullptr, nullptr, MBEDTLS_RSA_PUBLIC, MBEDTLS_MD_SHA256, HASH_SIZE, hash, signature)) != 0) {
        return false;
    }

    return true;
}

bool rsa_oaep_decrypt(mbedtls_pk_context &pk, const uint8_t *encrypted_msg, size_t msg_size, uint8_t *output, size_t out_size, size_t &decrypted_size) {
    // Ensure the key is an RSA key
    if (!mbedtls_pk_can_do(&pk, MBEDTLS_PK_RSA)) {
        return false;
    }

    // Perform the decryption
    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(pk);
    mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);
    if (msg_size != rsa->len) {
        return false;
    }
    if (mbedtls_rsa_rsaes_oaep_decrypt(rsa, nullptr, nullptr,
            MBEDTLS_RSA_PRIVATE, nullptr, 0,
            &decrypted_size, encrypted_msg, output, out_size)
        != 0) {
        return false;
    }

    return true;
}

const char *read_and_verify_identity_block(FILE *file, const BlockHeader &block_header, uint8_t *computed_intro_hash, IdentityBlockInfo &info) {
    const char *file_error = "Error while reading file.";
    uint16_t algo;
    if (!read_from_file(&algo, sizeof(algo), file)) {
        return file_error;
    }
    if (algo != ftrstd::to_underlying(EIdentityBlockSignCypher::RSA)) {
        return "Unknown Identity block cypher";
    }
    if (block_header.compression != ftrstd::to_underlying(ECompressionType::None)) {
        return "Compressed identity block not supported";
    }
    size_t block_size = block_header.uncompressed_size;
    size_t signed_bytes_size = block_header.get_size() + block_parameters_size(EBlockType::IdentityBlock) + block_size - SIGN_SIZE;

    // TODO: Should this be dynamic? fallible or not?
    unique_ptr<uint8_t[]> bytes(new uint8_t[signed_bytes_size]);
    size_t pos = get_block_header_bytes(block_header, bytes.get(), signed_bytes_size);
    memcpy(bytes.get() + pos, &algo, sizeof(algo));
    pos += sizeof(algo);
    memcpy(bytes.get() + pos, &flags, sizeof(flags));
    pos += sizeof(flags);
    // Read the data, but not the signature
    if (!read_from_file(&bytes.get()[pos], block_size - SIGN_SIZE, file)) {
        return file_error;
    }
    uint16_t key_len;
    memcpy(&key_len, &bytes.get()[pos], sizeof(key_len));
    pos += sizeof(key_len);
    string_view_u8 key(&bytes.get()[pos], key_len);
    if (mbedtls_pk_parse_public_key(info.identity_pk.get(), key.data(), key.length()) != 0) {
        return "Identity block parsing error";
    }
    uint8_t sign[SIGN_SIZE];
    if (!read_from_file(sign, SIGN_SIZE, file)) {
        return file_error;
    }
    auto res = rsa_sha256_sign_verify(*info.identity_pk, bytes.get(), signed_bytes_size, sign, SIGN_SIZE);
    if (!res) {
        return "Identity verification failed!";
    }
    pos += key_len;
    uint8_t name_len;
    memcpy(&name_len, &bytes.get()[pos], sizeof(name_len));
    pos += sizeof(name_len);
    if (name_len > IdentityBlockInfo::IDENTITY_NAME_LEN - 1) {
        return "Identity name too long";
    }
    memcpy(info.identity_name.data(), &bytes.get()[pos], name_len);
    info.identity_name[name_len] = '\0';
    pos += name_len;
    uint8_t intro_hash[HASH_SIZE];
    memcpy(intro_hash, &bytes.get()[pos], HASH_SIZE);
    pos += HASH_SIZE;
    memcpy(info.key_block_hash.data(), &bytes.get()[pos], HASH_SIZE);
    if (memcmp(computed_intro_hash, intro_hash, HASH_SIZE) != 0) {
        return "File has corrupted metadata";
    }

    return nullptr;
}

bool SymmetricKeys::extract_keys(uint8_t *key_block, size_t size) {
    if (size != 2 * KEY_SIZE) {
        return false;
    }
    memcpy(encryption_key, key_block, KEY_SIZE);
    memcpy(sign_key, key_block + KEY_SIZE, KEY_SIZE);
    return true;
}

std::optional<SymmetricKeys> decrypt_key_block(FILE *file, const bgcode::core::BlockHeader &block_header, mbedtls_pk_context &identity_pk, mbedtls_pk_context *printer_private_key) {
    if (printer_private_key == nullptr) {
        return std::nullopt;
    }
    if (block_header.compression != ftrstd::to_underlying(ECompressionType::None)) {
        return std::nullopt;
    }
    uint16_t encryption;
    if (!read_from_file(&encryption, sizeof(encryption), file)) {
        return std::nullopt;
    }
    // early return, so we don't allocate buffers etc.
    if (encryption != ftrstd::to_underlying(EKeyBlockEncryption::None)
        && encryption != ftrstd::to_underlying(EKeyBlockEncryption::RSA_ENC_SHA256_SIGN)) {
        return std::nullopt;
    }

    if (encryption == ftrstd::to_underlying(EKeyBlockEncryption::RSA_ENC_SHA256_SIGN)) {
        if (block_header.uncompressed_size != 512) {
            return std::nullopt;
        }
        unique_ptr<uint8_t[]> buffer(new uint8_t[block_header.uncompressed_size]);
        if (!read_from_file(buffer.get(), block_header.uncompressed_size, file)) {
            return std::nullopt;
        }
        string_view_u8 encrypted_block(buffer.get(), block_header.uncompressed_size - SIGN_SIZE);
        string_view_u8 sign(buffer.get() + block_header.uncompressed_size - SIGN_SIZE, SIGN_SIZE);
        if (!rsa_sha256_sign_verify(identity_pk, encrypted_block.data(), encrypted_block.size(), sign.data(), sign.size())) {
            return std::nullopt;
        }
        const size_t correct_decrypted_size = 2 * HASH_SIZE + 2 * KEY_SIZE;
        size_t decrypted_size;
        uint8_t decrypted_key_block[correct_decrypted_size];
        if (!rsa_oaep_decrypt(*printer_private_key, encrypted_block.data(), encrypted_block.size(), decrypted_key_block, sizeof(decrypted_key_block), decrypted_size)) {
            return std::nullopt;
        }
        if (decrypted_size != correct_decrypted_size) {
            return std::nullopt;
        }
        auto ret = mbedtls_pk_write_pubkey_der(printer_private_key, buffer.get(), block_header.uncompressed_size);
        if (ret <= 0) {
            return std::nullopt;
        }
        uint8_t printer_public_key_hash[HASH_SIZE];
        mbedtls_sha256_ret(buffer.get() + block_header.uncompressed_size - ret, ret, printer_public_key_hash, false);

        ret = mbedtls_pk_write_pubkey_der(&identity_pk, buffer.get(), block_header.uncompressed_size);
        if (ret <= 0) {
            return std::nullopt;
        }
        uint8_t identity_public_key_hash[HASH_SIZE];
        mbedtls_sha256_ret(buffer.get() + block_header.uncompressed_size - ret, ret, identity_public_key_hash, false);

        string_view_u8 identity_pub_key_hash_file(decrypted_key_block, HASH_SIZE);
        string_view_u8 printer_pub_key_hash_file(decrypted_key_block + HASH_SIZE, HASH_SIZE);
        if (memcmp(identity_pub_key_hash_file.data(), identity_public_key_hash, HASH_SIZE) != 0) {
            return std::nullopt;
        }
        if (memcmp(printer_pub_key_hash_file.data(), printer_public_key_hash, HASH_SIZE) != 0) {
            return std::nullopt;
        }

        SymmetricKeys keys;
        keys.extract_keys(decrypted_key_block + 2 * HASH_SIZE, 2 * KEY_SIZE);
        return keys;
    } else /*No encryption*/ {
        uint8_t plain_key_block[2 * KEY_SIZE];
        if (block_header.uncompressed_size != sizeof(plain_key_block)) {
            return std::nullopt;
        }
        if (!read_from_file(&plain_key_block, sizeof(plain_key_block), file)) {
            return std::nullopt;
        }
        SymmetricKeys keys;
        keys.extract_keys(plain_key_block, sizeof(plain_key_block));
        return keys;
    }
}

} // namespace e2ee
