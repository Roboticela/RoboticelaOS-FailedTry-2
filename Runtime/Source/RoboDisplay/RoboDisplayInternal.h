/* Internal RoboDisplay glue — not a public API. */
#ifndef ROBO_DISPLAY_INTERNAL_H
#define ROBO_DISPLAY_INTERNAL_H

#include "RoboDisplay/RoboDisplayBackend.h"

const DisplayBackendVtbl *GetDisplayBackendVtbl(void);
void *DisplayBackendUserdata(void);

#endif /* ROBO_DISPLAY_INTERNAL_H */
