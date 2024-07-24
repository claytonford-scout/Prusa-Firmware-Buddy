#include <mbedtls/pk.h>

struct Pk {
    mbedtls_pk_context pk;
    Pk() {
        mbedtls_pk_init(&pk);
    }
    ~Pk() {
        mbedtls_pk_free(&pk);
    }
};
