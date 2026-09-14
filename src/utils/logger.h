#ifndef __LOGGER_H_
#define __LOGGER_H_

#include <whb/log.h>
#include <whb/log_udp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEBUG_LOGGER        1

#ifdef DEBUG_LOGGER
void log_init();
void log_deinit(void);
void log_print(const char *str);
void log_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
#else
#define log_init()        ((void) 0)
#define log_deinit()      ((void) 0)
#define log_print(s)      ((void) 0)
#define log_printf(...)   ((void) 0)
#endif

#ifdef __cplusplus
}
#endif

#endif
