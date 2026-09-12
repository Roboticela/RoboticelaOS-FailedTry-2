#ifndef ROBOCOMPOSITOR_COMPOSE_H
#define ROBOCOMPOSITOR_COMPOSE_H

#include "types.h"

/*
 * CPU composition core. No syscalls. Linked by RoboCompositor and
 * the host compositor tests. A future GPU backend replaces the
 * compose/present pair without changing this layer model.
 */

typedef struct RoboComposeLayer {
    int used;
    uint32_t window_id;
    uint64_t owner_pid;
    int32_t x;
    int32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t stride;
    uint32_t pixfmt;
    uint32_t *pixels;
    uint8_t opacity;
    uint8_t visible;
} RoboComposeLayer;

typedef struct RoboComposeScene {
    RoboComposeLayer layers[ROBO_COMPOSE_MAX_LAYERS];
    uint32_t zorder[ROBO_COMPOSE_MAX_LAYERS];
    unsigned nlayers;
    RoboDamageRect damage;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixfmt;
    uint32_t *back;
    uint32_t *front;
    uint32_t bg_argb;
    uint32_t frame;
    unsigned last_blits;
} RoboComposeScene;

int robo_damage_is_empty(const RoboDamageRect *d);
void robo_damage_clear(RoboDamageRect *d);
void robo_damage_add(RoboDamageRect *d, int32_t x, int32_t y,
                     uint32_t w, uint32_t h);
void robo_damage_merge(RoboDamageRect *d, const RoboDamageRect *o);
int robo_damage_contains(const RoboDamageRect *d, int32_t x, int32_t y);
void robo_damage_clip(RoboDamageRect *d, uint32_t dw, uint32_t dh);

uint32_t robo_compose_pack(uint32_t pixfmt, uint32_t argb);
uint32_t robo_compose_unpack(uint32_t pixfmt, uint32_t p);
uint32_t robo_compose_blend(uint32_t pixfmt, uint32_t dst, uint32_t src,
                            uint32_t opacity);

void robo_compose_scene_reset(RoboComposeScene *s);
int robo_compose_init(RoboComposeScene *s, uint32_t *back, uint32_t *front,
                      uint32_t w, uint32_t h, uint32_t stride, uint32_t pixfmt);
int robo_compose_insert(RoboComposeScene *s, uint32_t id, uint64_t pid,
                        int32_t x, int32_t y, uint32_t w, uint32_t h,
                        uint32_t stride, uint32_t *pixels, uint32_t opacity);
int robo_compose_remove(RoboComposeScene *s, uint32_t id);
int robo_compose_raise(RoboComposeScene *s, uint32_t id);
int robo_compose_lower(RoboComposeScene *s, uint32_t id);
int robo_compose_set_z(RoboComposeScene *s, uint32_t id, uint32_t z);
RoboComposeLayer *robo_compose_find(RoboComposeScene *s, uint32_t id);
int robo_compose_frame(RoboComposeScene *s);
uint32_t robo_compose_sample(const RoboComposeScene *s, int32_t x, int32_t y);

#endif
