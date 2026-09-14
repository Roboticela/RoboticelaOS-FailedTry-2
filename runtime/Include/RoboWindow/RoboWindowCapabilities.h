/*
 * RoboWindow backend capabilities (Goals 7.2 / 7.4).
 *
 * Explicit flags — do not infer support from null function pointers alone.
 * Null architecture backend reports in-process lifecycle support without
 * native OS presentation or focus.
 */

#ifndef ROBO_WINDOW_CAPABILITIES_H
#define ROBO_WINDOW_CAPABILITIES_H

#include "RoboBase/RoboError.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_window_backend_caps {
    int create_window;
    int destroy_window;
    int show_hide;
    int resize;
    int move;
    int set_title;
    int focus;
    int query_state;
    int present;
    int process_events;
    /**
     * Non-zero: backend may overwrite actual geometry/visibility via
     * query_state; core must not assume request == actual.
     */
    int reports_actual_state;

    /**
     * Goal 7.4: bitmask of presentable RoboGraphics formats
     * (ROBO_WINDOW_PRESENT_FMT_*). Meaningful when present != 0.
     * Zero with present set → no formats (all presents fail as unsupported).
     */
    uint32_t present_format_mask;

    /**
     * Optional present size ceiling (physical pixels). 0 = no backend limit
     * beyond RoboGraphics surface limits.
     */
    uint32_t present_max_width;
    uint32_t present_max_height;
} robo_window_backend_caps_t;

void robo_window_backend_caps_clear(robo_window_backend_caps_t *caps);

/** Null-architecture defaults: lifecycle + title/size/move; no present/focus. */
void robo_window_backend_caps_init_null(robo_window_backend_caps_t *caps);

/**
 * Query active backend capabilities.
 * No backend → ROBO_NOT_SUPPORTED (out zeroed if non-NULL).
 */
robo_result_t robo_window_backend_capabilities(robo_window_backend_caps_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_CAPABILITIES_H */
