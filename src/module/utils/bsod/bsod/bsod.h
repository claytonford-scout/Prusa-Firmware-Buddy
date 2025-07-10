/// \file
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

[[noreturn]] void __attribute__((noreturn, format(__printf__, 1, 4)))
_bsod(const char *fmt, const char *file_name, int line_number, ...);

#define bsod(fmt, ...) _bsod(fmt, __FILE_NAME__, __LINE__, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
