#include "e2ee.hpp"

#include "heap.h"
#include "unique_file_ptr.hpp"
#include <raii/deleter.hpp>

#include <mbedtls/config.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>

#include <cstdio>
#include <cstdlib>

using std::unique_ptr;

namespace {

// So we get RAII for all the init-free contexts of mbedtls
struct Contexts {
    mbedtls_rsa_context rsa;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_pk_context pk;

    Contexts() {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_rsa_init(&rsa, MBEDTLS_RSA_PKCS_V15, 0);
        mbedtls_pk_init(&pk);
        mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
        pk.pk_ctx = &rsa;
    }
    ~Contexts() {
        pk.pk_ctx = nullptr;
        mbedtls_pk_free(&pk);
        mbedtls_rsa_free(&rsa);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
    }
};

// Size discovered by experimental means.
const size_t buffer_size = 2048;

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
        struct Contexts contexts;

        const char *pers = "ecp_keypair";

        if (mbedtls_ctr_drbg_seed(&contexts.ctr_drbg, mbedtls_entropy_func, &contexts.entropy, (const unsigned char *)pers, strlen(pers)) != 0) {
            return LoopResult::Failed;
        }

        if (mbedtls_rsa_gen_key(&contexts.rsa, mbedtls_ctr_drbg_random, &contexts.ctr_drbg, 2048, 65537) != 0) {
            return LoopResult::Failed;
        }

        buffer.reset(reinterpret_cast<uint8_t *>(malloc_fallible(buffer_size)));

        if (!buffer) {
            return LoopResult::Failed;
        }

        export_res = mbedtls_pk_write_key_der(&contexts.pk, buffer.get(), buffer_size);
        if (export_res <= 0) {
            return LoopResult::Failed;
        }
    } // Free the mbedtls contexts, better chance to have RAM for the file

    unique_file_ptr fout(fopen(key_path, "wb"));
    if (!fout) {
        return LoopResult::Failed;
    }

    // Note: The mbedtls_pk_write_key_der writes to the _end_ of the buffer.
    if (fwrite(buffer.get() + buffer_size - export_res, export_res, 1, fout.get()) != 1) {
        return LoopResult::Failed;
    }

    return LoopResult::Done;
}

} // namespace e2ee
