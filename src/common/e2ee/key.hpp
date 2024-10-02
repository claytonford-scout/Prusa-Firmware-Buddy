#include <crash_dump/secret.hpp>
#include <mbedtls/pk.h>

#include <async_job/async_job_execution_control.hpp>

namespace e2ee {
struct Pk {
    mbedtls_pk_context pk;
    Pk();
    ~Pk();
};

void generate_key(AsyncJobExecutionControl &control, bool &result);

bool export_key();

} // namespace e2ee
