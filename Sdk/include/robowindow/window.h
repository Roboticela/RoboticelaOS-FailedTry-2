#ifndef ROBOWINDOW_WINDOW_H
#define ROBOWINDOW_WINDOW_H

#include "types.h"
#include "event.h"
#include "surface.h"
#include "protocol.h"

int WindowConnect(void);
void WindowDisconnect(void);

RoboWindowID WindowCreate(uint32_t width, uint32_t height, uint32_t flags);
int WindowDestroy(RoboWindowID window);
int WindowShow(RoboWindowID window);
int WindowHide(RoboWindowID window);
int WindowMove(RoboWindowID window, int32_t x, int32_t y);
int WindowResize(RoboWindowID window, uint32_t width, uint32_t height);
int WindowSetTitle(RoboWindowID window, const char *title);
int WindowGetSurface(RoboWindowID window, RoboWindowSurface *out);
int WindowRequestFocus(RoboWindowID window);
int WindowWaitEvent(RoboWindowID window, RoboWindowEvent *out);
int WindowPollEvent(RoboWindowID window, RoboWindowEvent *out);
int WindowSubmit(RoboWindowID window);
int WindowStats(uint32_t *liveWindows);
int WindowShutdown(void);
int WindowTestInject(const RoboWindowEvent *ev);
RoboWindowID WindowHitTest(int32_t x, int32_t y);
RoboWindowID WindowGetFocus(void);
int WindowSetOpacity(RoboWindowID window, uint32_t opacity);
int WindowSamplePixel(int32_t x, int32_t y, uint32_t *px);

#endif
