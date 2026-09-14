/*
 * RoboError — Goal 1.3 implementation.
 */

#include "RoboBase/RoboError.h"

#include <stddef.h>

robo_i32 robo_result_is_ok(robo_result_t r)
{
    return r == ROBO_OK ? 1 : 0;
}

robo_i32 robo_result_is_error(robo_result_t r)
{
    return r != ROBO_OK ? 1 : 0;
}

robo_i32 robo_result_is_known(robo_result_t r)
{
    return (r >= ROBO_RESULT_FIRST && r <= ROBO_RESULT_LAST) ? 1 : 0;
}

const char *robo_error_string(robo_result_t r)
{
    switch (r) {
    case ROBO_OK:
        return "Success";
    case ROBO_ERROR:
        return "Error";
    case ROBO_INVALID_ARGUMENT:
        return "Invalid argument";
    case ROBO_OUT_OF_MEMORY:
        return "Out of memory";
    case ROBO_NOT_FOUND:
        return "Not found";
    case ROBO_ALREADY_EXISTS:
        return "Already exists";
    case ROBO_PERMISSION_DENIED:
        return "Permission denied";
    case ROBO_NOT_SUPPORTED:
        return "Not supported";
    case ROBO_TIMEOUT:
        return "Timeout";
    case ROBO_IO_ERROR:
        return "I/O error";
    case ROBO_PARSE_ERROR:
        return "Parse error";
    case ROBO_INIT_ERROR:
        return "Initialization error";
    case ROBO_BUSY:
        return "Busy";
    default:
        return "Unknown error";
    }
}

void robo_error_info_reset(robo_error_info_t *info)
{
    if (!info) {
        return;
    }
    info->code = ROBO_OK;
    info->subsystem = NULL;
    info->message = NULL;
    info->file = NULL;
    info->line = 0;
    info->underlying = ROBO_OK;
}

robo_result_t robo_error_info_set(robo_error_info_t *info,
                                  robo_result_t code,
                                  const char *subsystem,
                                  const char *message,
                                  const char *file,
                                  int line,
                                  robo_result_t underlying)
{
    if (!info) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (code == ROBO_OK) {
        robo_error_info_reset(info);
        return ROBO_OK;
    }
    info->code = code;
    info->subsystem = subsystem;
    info->message = message;
    info->file = file;
    info->line = line;
    info->underlying = underlying;
    return code;
}
