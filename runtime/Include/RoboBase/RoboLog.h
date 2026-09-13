/*
 * RoboLog — public logging API (Goal 1.2), part of RoboBase.
 */

#ifndef ROBO_LOG_H
#define ROBO_LOG_H

#include "RoboBase/RoboBase.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robo_log_level {
    ROBO_LOG_DEBUG = 0,
    ROBO_LOG_INFO  = 1,
    ROBO_LOG_WARN  = 2,
    ROBO_LOG_ERROR = 3
} robo_log_level_t;

typedef void (*robo_log_writer_fn)(robo_log_level_t level,
                                   const char *line,
                                   void *userdata);

void robo_log_set_min_level(robo_log_level_t level);
robo_log_level_t robo_log_get_min_level(void);
void robo_log_set_writer(robo_log_writer_fn writer, void *userdata);

void robo_log_write(robo_log_level_t level,
                    const char *file,
                    int line,
                    const char *fmt,
                    ...);

#define robo_log_debug(...) \
    robo_log_write(ROBO_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define robo_log_info(...) \
    robo_log_write(ROBO_LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define robo_log_warn(...) \
    robo_log_write(ROBO_LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define robo_log_error(...) \
    robo_log_write(ROBO_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LOG_H */
