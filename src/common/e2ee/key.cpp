#include "key.hpp"
#include "e2ee.hpp"

#include <path_utils.h>

#include <unique_file_ptr.hpp>
#include <raii/deleter.hpp>
#include <heap.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <memory>
#include <cstring>

using std::unique_ptr;

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
} // namespace

namespace e2ee {

Pk::Pk() {
    mbedtls_pk_init(&pk);
}

Pk::~Pk() {
    mbedtls_pk_free(&pk);
}

void generate_key(AsyncJobExecutionControl &control, bool &result) {
    result = false;
    unique_ptr<uint8_t, FreeDeleter> buffer(reinterpret_cast<uint8_t *>(malloc_fallible(PRIVATE_KEY_BUFFER_SIZE)));
    if (!buffer) {
        return;
    }
    crash_dump::ManualSecret secret(buffer.get(), PRIVATE_KEY_BUFFER_SIZE);

    int export_res = 0;
    {
        struct KeyGenContexts contexts;

        const char *pers = "ecp_keypair";

        if (mbedtls_ctr_drbg_seed(&contexts.ctr_drbg, mbedtls_entropy_func, &contexts.entropy, (const unsigned char *)pers, strlen(pers)) != 0) {
            return;
        }

        mbedtls_rsa_context *rsa = mbedtls_pk_rsa(contexts.pk);
        if (mbedtls_rsa_gen_key(rsa, mbedtls_ctr_drbg_random, &contexts.ctr_drbg, 2048, 65537) != 0) {
            return;
        }

        export_res = mbedtls_pk_write_key_der(&contexts.pk, buffer.get(), PRIVATE_KEY_BUFFER_SIZE);
        if (export_res <= 0) {
            return;
        }
    } // Free the mbedtls contexts, better chance to have RAM for the file

    if (control.is_discarded()) {
        // We aborted the generation, so do not save the key
        return;
    }

    make_dirs(private_key_path);
    unique_file_ptr fout(fopen(private_key_path, "wb"));
    if (!fout) {
        return;
    }

    // Note: The mbedtls_pk_write_key_der writes to the _end_ of the buffer.
    if (fwrite(buffer.get() + PRIVATE_KEY_BUFFER_SIZE - export_res, export_res, 1, fout.get()) != 1) {
        return;
    }

    result = true;
    return;
}

bool export_key() {
    unique_ptr<uint8_t, FreeDeleter> buffer(reinterpret_cast<uint8_t *>(malloc_fallible(PRIVATE_KEY_BUFFER_SIZE)));
    if (!buffer) {
        return false;
    }

    unique_file_ptr inf(fopen(private_key_path, "rb"));
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

    unique_file_ptr outf(fopen(public_key_path, "wb"));
    if (!outf) {
        return false;
    }

    // Note: mbedtls writes to the _end_ of the buffer.
    if (fwrite(buffer.get() + PRIVATE_KEY_BUFFER_SIZE - ret, ret, 1, outf.get()) != 1) {
        outf.reset();
        // Result not checked - no way we can fail twice anyway.
        remove(public_key_path);
        return false;
    }

    return true;
}

} // namespace e2ee
