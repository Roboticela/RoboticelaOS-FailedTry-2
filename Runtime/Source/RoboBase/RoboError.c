/*
 * RoboError — Goal 1.3 implementation.
 */

#include "RoboBase/RoboError.h"

#include <stddef.h>

I32 ResultIsOk(Result r)
{
    return r == RoboOk ? 1 : 0;
}

I32 ResultIsError(Result r)
{
    return r != RoboOk ? 1 : 0;
}

I32 ResultIsKnown(Result r)
{
    return (r >= RoboResultFirst && r <= RoboResultLast) ? 1 : 0;
}

const char *ErrorString(Result r)
{
    switch (r) {
    case RoboOk:
        return "Success";
    case RoboError:
        return "Error";
    case RoboInvalidArgument:
        return "Invalid argument";
    case RoboOutOfMemory:
        return "Out of memory";
    case RoboNotFound:
        return "Not found";
    case RoboAlreadyExists:
        return "Already exists";
    case RoboPermissionDenied:
        return "Permission denied";
    case RoboNotSupported:
        return "Not supported";
    case RoboTimeout:
        return "Timeout";
    case RoboIoError:
        return "I/O error";
    case RoboParseError:
        return "Parse error";
    case RoboInitError:
        return "Initialization error";
    case RoboBusy:
        return "Busy";
    default:
        return "Unknown error";
    }
}

void ErrorInfoReset(ErrorInfo *info)
{
    if (!info) {
        return;
    }
    info->code = RoboOk;
    info->subsystem = NULL;
    info->message = NULL;
    info->file = NULL;
    info->line = 0;
    info->underlying = RoboOk;
}

Result ErrorInfoSet(ErrorInfo *info,
                                  Result code,
                                  const char *subsystem,
                                  const char *message,
                                  const char *file,
                                  int line,
                                  Result underlying)
{
    if (!info) {
        return RoboInvalidArgument;
    }
    if (code == RoboOk) {
        ErrorInfoReset(info);
        return RoboOk;
    }
    info->code = code;
    info->subsystem = subsystem;
    info->message = message;
    info->file = file;
    info->line = line;
    info->underlying = underlying;
    return code;
}
