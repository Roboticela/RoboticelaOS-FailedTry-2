/*
 * RoboDisplay backend contract (Goal 7.3).
 *
 * Platform backends will enumerate real monitors later. Goal 7.3 ships
 * null (empty) and placeholder (one architecture display) backends only.
 */

#ifndef ROBO_DISPLAY_BACKEND_H
#define ROBO_DISPLAY_BACKEND_H

#include "RoboBase/RoboError.h"
#include "RoboDisplay/RoboDisplayCapabilities.h"
#include "RoboDisplay/RoboDisplayInfo.h"
#include "RoboDisplay/RoboDisplayTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_display_backend_vtbl {
    const char *name;
    const robo_display_backend_caps_t *capabilities;

    robo_result_t (*initialize)(void *userdata);
    void (*shutdown)(void *userdata);

    /**
     * Fill up to @p capacity displays; write count to @p out_count.
     * Order is backend-defined; primary may not be index 0.
     */
    robo_result_t (*enumerate)(void *userdata,
                               robo_display_info_t *out,
                               size_t capacity,
                               size_t *out_count);

    robo_result_t (*get_primary)(void *userdata, robo_display_info_t *out);

    robo_result_t (*get_info)(void *userdata,
                              robo_display_id_t id,
                              robo_display_info_t *out);

    /**
     * Optional mode list. May be NULL → treated as ROBO_NOT_SUPPORTED.
     */
    robo_result_t (*get_modes)(void *userdata,
                               robo_display_id_t id,
                               robo_display_mode_t *out,
                               size_t capacity,
                               size_t *out_count);
} robo_display_backend_vtbl_t;

robo_result_t robo_display_backend_register(const robo_display_backend_vtbl_t *vtbl,
                                            void *userdata);

void robo_display_backend_clear(void);

int robo_display_backend_is_registered(void);

const char *robo_display_backend_name(void);

/** Empty enumeration (no host monitors). */
const robo_display_backend_vtbl_t *robo_display_backend_null_vtbl(void);
robo_result_t robo_display_backend_use_null(void);

/**
 * Single architecture placeholder primary (1280×800 @ scale 1).
 * Not a real host display.
 */
const robo_display_backend_vtbl_t *robo_display_backend_placeholder_vtbl(void);
robo_result_t robo_display_backend_use_placeholder(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DISPLAY_BACKEND_H */
