/* Internal RoboWindow glue (Goals 7.1 / 7.2) — not a public API. */
#ifndef ROBO_WINDOW_INTERNAL_H
#define ROBO_WINDOW_INTERNAL_H

#include "RoboWindow/RoboWindowBackend.h"

void robo_window_display_setup_placeholders(void);
void robo_window_display_reset_placeholders(void);

void robo_window_event_queue_reset(void);

void robo_window_thread_reset(void);

const robo_window_backend_vtbl_t *robo_window_backend_vtbl(void);
void *robo_window_backend_userdata(void);

#endif /* ROBO_WINDOW_INTERNAL_H */
