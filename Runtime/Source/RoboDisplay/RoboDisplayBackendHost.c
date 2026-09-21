#include "RoboDisplay/RoboDisplayBackendHost.h"

/*
 * Guest / non-Win32 stub. Real implementation is compiled only into the
 * Windows host test binary (platform/win32/RoboDisplayBackendWin32.c).
 */

#if !defined(RoboWindowHasWin32) || !RoboWindowHasWin32

int DisplayBackendHostAvailable(void)
{
    return 0;
}

const DisplayBackendVtbl *DisplayBackendHostVtbl(void)
{
    return NULL;
}

Result DisplayBackendUseHost(void)
{
    return RoboNotSupported;
}

#endif /* !RoboWindowHasWin32 */
