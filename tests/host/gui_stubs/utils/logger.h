#ifndef HOST_STUB_LOGGER_H
#define HOST_STUB_LOGGER_H
#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif
void log_printf(const char* format, ...) __attribute__((format(printf, 1, 2)));
#ifdef __cplusplus
}
#endif
#endif
