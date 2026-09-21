/*
 * RoboLog — public logging API (Goal 1.2), part of RoboBase.
 */

#ifndef ROBO_LOG_H
#define ROBO_LOG_H

#include "RoboBase/RoboBase.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum LogLevel {
    RoboLogDebug = 0,
    RoboLogInfo  = 1,
    RoboLogWarn  = 2,
    RoboLogError = 3
} LogLevel;

typedef void (*LogWriterFn)(LogLevel level,
                                   const char *line,
                                   void *userdata);

void LogSetMinLevel(LogLevel level);
LogLevel LogGetMinLevel(void);
void LogSetWriter(LogWriterFn writer, void *userdata);

void LogWrite(LogLevel level,
                    const char *file,
                    int line,
                    const char *fmt,
                    ...);

#define LogDebug(...) \
    LogWrite(RoboLogDebug, __FILE__, __LINE__, __VA_ARGS__)
#define LogInfo(...) \
    LogWrite(RoboLogInfo, __FILE__, __LINE__, __VA_ARGS__)
#define LogWarn(...) \
    LogWrite(RoboLogWarn, __FILE__, __LINE__, __VA_ARGS__)
#define LogError(...) \
    LogWrite(RoboLogError, __FILE__, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LOG_H */
