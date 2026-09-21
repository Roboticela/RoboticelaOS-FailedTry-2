#ifndef ROBO_WS_COMPOSITOR_H
#define ROBO_WS_COMPOSITOR_H

#include <stdint.h>

int WsCompositorConnect(void);
int WsCompositorReady(void);
uint32_t WsCompositorPixfmt(void);
uint32_t WsCompositorWidth(void);
uint32_t WsCompositorHeight(void);
void WsCompositorSync(uint32_t id, uint64_t shmId, uint32_t shmBytes,
                        int32_t x, int32_t y, uint32_t w, uint32_t h,
                        uint32_t stride, uint32_t pixfmt, int visible,
                        uint32_t opacity);
void WsCompositorRemove(uint32_t id);
void WsCompositorRaise(uint32_t id);
void WsCompositorDamage(int32_t x, int32_t y, uint32_t w, uint32_t h);
int WsCompositorFrame(void);
int WsCompositorSample(int32_t x, int32_t y, uint32_t *px);

#endif
