/* Internal RoboDisplay glue — not a public API. */
#ifndef ROBO_DISPLAY_INTERNAL_H
#define ROBO_DISPLAY_INTERNAL_H

#include "RoboDisplay/RoboDisplayBackend.h"

const robo_display_backend_vtbl_t *robo_display_backend_vtbl(void);
void *robo_display_backend_userdata(void);

#endif /* ROBO_DISPLAY_INTERNAL_H */
