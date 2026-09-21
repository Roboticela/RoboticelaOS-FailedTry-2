#ifndef ROBO_WS_SERVER_H
#define ROBO_WS_SERVER_H

#include <stdint.h>
#include <robowindow/protocol.h>
#include <robosys/input.h>

int WsInit(int headless, uint32_t pixfmt);
void WsClientAdd(int handle, uint64_t pid);
void WsClientDrop(int handle);
int WsHandle(int handle, uint64_t pid, RoboWinMsg *m);
void WsRouteInput(const RoboInputEvent *ev);
int WsHasWaiters(void);
int WsLiveWindows(void);
void WsPresentIfDirty(void);

#endif
