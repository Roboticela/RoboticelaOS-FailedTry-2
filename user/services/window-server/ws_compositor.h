#ifndef ROBO_WS_COMPOSITOR_H
#define ROBO_WS_COMPOSITOR_H

#include <stdint.h>

int ws_compositor_connect(void);
int ws_compositor_ready(void);
uint32_t ws_compositor_pixfmt(void);
uint32_t ws_compositor_width(void);
uint32_t ws_compositor_height(void);
void ws_compositor_sync(uint32_t id, uint64_t shm_id, uint32_t shm_bytes,
                        int32_t x, int32_t y, uint32_t w, uint32_t h,
                        uint32_t stride, uint32_t pixfmt, int visible,
                        uint32_t opacity);
void ws_compositor_remove(uint32_t id);
void ws_compositor_raise(uint32_t id);
void ws_compositor_damage(int32_t x, int32_t y, uint32_t w, uint32_t h);
int ws_compositor_frame(void);
int ws_compositor_sample(int32_t x, int32_t y, uint32_t *px);

#endif
