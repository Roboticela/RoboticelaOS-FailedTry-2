#include "RoboWindow/RoboWindowBackendHost.h"

/*
 * Guest / non-Win32 stub. Real implementation is compiled only into the
 * Windows host test binary (platform/win32/RoboWindowBackendWin32.c).
 */

#if !defined(RoboWindowHasWin32) || !RoboWindowHasWin32

int WindowBackendHostAvailable(void)
{
    return 0;
}

const WindowBackendVtbl *WindowBackendHostVtbl(void)
{
    return NULL;
}

Result WindowBackendUseHost(void)
{
    return RoboNotSupported;
}

#endif /* !RoboWindowHasWin32 */
