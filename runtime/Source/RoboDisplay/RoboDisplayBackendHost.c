#include "RoboDisplay/RoboDisplayBackendHost.h"

/*
 * Guest / non-Win32 stub. Real implementation is compiled only into the
 * Windows host test binary (platform/win32/RoboDisplayBackendWin32.c).
 */

#if !defined(ROBO_WINDOW_HAS_WIN32) || !ROBO_WINDOW_HAS_WIN32

int robo_display_backend_host_available(void)
{
    return 0;
}

const robo_display_backend_vtbl_t *robo_display_backend_host_vtbl(void)
{
    return NULL;
}

robo_result_t robo_display_backend_use_host(void)
{
    return ROBO_NOT_SUPPORTED;
}

#endif /* !ROBO_WINDOW_HAS_WIN32 */
