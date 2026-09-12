#ifndef ROBOWINDOW_WINDOW_H
#define ROBOWINDOW_WINDOW_H

#include "types.h"
#include "event.h"
#include "surface.h"
#include "protocol.h"

int robo_window_connect(void);
void robo_window_disconnect(void);

RoboWindowID robo_window_create(uint32_t width, uint32_t height, uint32_t flags);
int robo_window_destroy(RoboWindowID window);
int robo_window_show(RoboWindowID window);
int robo_window_hide(RoboWindowID window);
int robo_window_move(RoboWindowID window, int32_t x, int32_t y);
int robo_window_resize(RoboWindowID window, uint32_t width, uint32_t height);
int robo_window_set_title(RoboWindowID window, const char *title);
int robo_window_get_surface(RoboWindowID window, RoboWindowSurface *out);
int robo_window_request_focus(RoboWindowID window);
int robo_window_wait_event(RoboWindowID window, RoboWindowEvent *out);
int robo_window_poll_event(RoboWindowID window, RoboWindowEvent *out);
int robo_window_submit(RoboWindowID window);
int robo_window_stats(uint32_t *live_windows);
int robo_window_shutdown(void);
int robo_window_test_inject(const RoboWindowEvent *ev);
RoboWindowID robo_window_hit_test(int32_t x, int32_t y);
RoboWindowID robo_window_get_focus(void);
int robo_window_set_opacity(RoboWindowID window, uint32_t opacity);
int robo_window_sample_pixel(int32_t x, int32_t y, uint32_t *px);

#endif
