#ifndef ROBO_WS_SERVER_H
#define ROBO_WS_SERVER_H

#include <stdint.h>
#include <robowindow/protocol.h>
#include <robosys/input.h>

int ws_init(int headless, uint32_t pixfmt);
void ws_client_add(int handle, uint64_t pid);
void ws_client_drop(int handle);
int ws_handle(int handle, uint64_t pid, RoboWinMsg *m);
void ws_route_input(const RoboInputEvent *ev);
int ws_has_waiters(void);
int ws_live_windows(void);
void ws_present_if_dirty(void);

#endif
